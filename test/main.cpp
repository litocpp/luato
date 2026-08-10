import luato;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{

struct Checks {
    int failures {};

    void expect(bool condition, char const* message) {
        if (condition) return;
        ++failures;
        __builtin_printf("FAIL: %s\n", message);
    }
};

struct DropProbe {
    int* drops;

    explicit DropProbe(int& count) noexcept: drops(rstd::addressof(count)) {}
    DropProbe(const DropProbe&)            = delete;
    DropProbe& operator=(const DropProbe&) = delete;
    DropProbe(DropProbe&& other) noexcept: drops(rstd::exchange(other.drops, nullptr)) {}
    DropProbe& operator=(DropProbe&&) = delete;
    ~DropProbe() {
        if (drops != nullptr) ++*drops;
    }
};

auto borrowed_str(char const* value) noexcept -> ref<str> {
    auto bytes =
        slice<u8>::from_raw_parts(reinterpret_cast<byte const*>(value), usize(rstd::strlen(value)));
    return rstd::str_::from_utf8_unchecked(bytes);
}

auto fixture(ref<str> name) -> rstd::path::PathBuf {
    auto result = rstd::path::PathBuf::from(borrowed_str(LUATO_FIXTURE_DIR));
    auto child  = rstd::path::PathBuf::from(name);
    result.push(child.as_path());
    return result;
}

void expect_script_error(Checks&               checks,
                         luato::State&         state,
                         ref<rstd::path::Path> path,
                         luato::ErrorKind      kind,
                         ref<str>              source,
                         ref<str>              message,
                         ref<str>              traceback) {
    auto result = state.execute_file(path);
    checks.expect(result.is_err(), "script execution should return an error");
    if (result.is_ok()) return;

    auto error = rstd::move(result).unwrap_err_unchecked();
    checks.expect(error.kind == kind, "script error kind should match");
    checks.expect(error.source.as_str().contains(source), "script error source should match");
    checks.expect(error.message.as_str().contains(message), "script error message should match");
    if (! traceback.is_empty()) {
        checks.expect(error.traceback.as_str().contains(traceback),
                      "script traceback should contain the Lua callsite");
    }
}

} // namespace

int main() {
    Checks checks;
    int    callback_drops {};
    int    invocation_drops {};
    int    rejected_callback_drops {};

    {
        auto minimal = luato::State::create(luato::StateOptions::none());
        checks.expect(minimal.is_ok(), "state without standard libraries should be created");
    }

    {
        auto created = luato::State::create(luato::StateOptions::base());
        checks.expect(created.is_ok(), "base Lua state should be created");
        if (created.is_err()) return 1;

        auto original   = rstd::move(created).unwrap_unchecked();
        auto state      = rstd::move(original);
        auto moved_from = original.execute_file(fixture("native-add.lua"_str).as_path());
        checks.expect(moved_from.is_err(), "moved-from state should reject execution");
        if (moved_from.is_err()) {
            auto error = rstd::move(moved_from).unwrap_err_unchecked();
            checks.expect(error.kind == luato::ErrorKind::PanicInvariant,
                          "moved-from state should report its invariant error");
        }

        auto host = luato::ModuleSpec(String::make("host"_str));
        host.add(luato::NativeFunctionSpec::make(
            String::make("add"_str),
            usize(2),
            [lifetime = DropProbe(callback_drops)](
                luato::CallFrame& frame) mutable -> luato::BindingResult {
                auto lhs = frame.required<i64>(usize {});
                if (lhs.is_err()) {
                    return Err(rstd::move(lhs).unwrap_err_unchecked());
                }
                auto rhs = frame.required<i64>(usize(1));
                if (rhs.is_err()) {
                    return Err(rstd::move(rhs).unwrap_err_unchecked());
                }
                auto sum = rstd::move(lhs).unwrap_unchecked() + rstd::move(rhs).unwrap_unchecked();
                frame.push(sum);
                return Ok(usize(1));
            }));
        host.add(luato::NativeFunctionSpec::make(
            String::make("echo"_str),
            usize(1),
            [](luato::CallFrame& frame) -> luato::BindingResult {
                auto value = frame.required<String>(usize {});
                if (value.is_err()) {
                    return Err(rstd::move(value).unwrap_err_unchecked());
                }
                frame.push(rstd::move(value).unwrap_unchecked());
                return Ok(usize(1));
            }));
        host.add(
            luato::NativeFunctionSpec::make(String::make("truth"_str),
                                            usize {},
                                            [](luato::CallFrame& frame) -> luato::BindingResult {
                                                frame.push(true);
                                                return Ok(usize(1));
                                            }));
        host.add(luato::NativeFunctionSpec::make(
            String::make("fail"_str),
            usize(1),
            [lifetime = DropProbe(callback_drops),
             &invocation_drops](luato::CallFrame& frame) mutable -> luato::BindingResult {
                auto invocation = DropProbe(invocation_drops);
                auto reason     = frame.required<String>(usize {});
                if (reason.is_err()) {
                    return Err(rstd::move(reason).unwrap_err_unchecked());
                }
                auto value = rstd::move(reason).unwrap_unchecked();
                return Err(
                    luato::Error::binding(rstd::format("native failure: {}", value.as_str())));
            }));

        auto registration = state.register_module(rstd::move(host));
        checks.expect(registration.is_ok(), "host module should register");
        if (registration.is_err()) return 1;

        auto duplicate = luato::ModuleSpec(String::make("host"_str));
        duplicate.add(
            luato::NativeFunctionSpec::make(String::make("unused"_str),
                                            usize {},
                                            [lifetime = DropProbe(rejected_callback_drops)](
                                                luato::CallFrame&) mutable -> luato::BindingResult {
                                                return Ok(usize {});
                                            }));
        auto duplicate_result = state.register_module(rstd::move(duplicate));
        checks.expect(duplicate_result.is_err(), "duplicate module should be rejected");
        if (duplicate_result.is_err()) {
            auto error = rstd::move(duplicate_result).unwrap_err_unchecked();
            checks.expect(error.kind == luato::ErrorKind::Binding,
                          "duplicate module should be a binding error");
        }
        checks.expect(rejected_callback_drops == 1,
                      "rejected module should destroy its callback context exactly once");

        auto native_add = fixture("native-add.lua"_str);
        auto success    = state.execute_file(native_add.as_path());
        checks.expect(success.is_ok(), "native add fixture should pass");

        auto missing = fixture("missing.lua"_str);
        expect_script_error(checks,
                            state,
                            missing.as_path(),
                            luato::ErrorKind::File,
                            "missing.lua"_str,
                            "missing.lua"_str,
                            ""_str);

        auto syntax = fixture("syntax-error.lua"_str);
        expect_script_error(checks,
                            state,
                            syntax.as_path(),
                            luato::ErrorKind::Syntax,
                            "syntax-error.lua"_str,
                            "syntax-error.lua"_str,
                            ""_str);

        auto runtime = fixture("runtime-error.lua"_str);
        expect_script_error(checks,
                            state,
                            runtime.as_path(),
                            luato::ErrorKind::Runtime,
                            "runtime-error.lua"_str,
                            "boom"_str,
                            "runtime-error.lua"_str);

        auto after_runtime = state.execute_file(native_add.as_path());
        checks.expect(after_runtime.is_ok(), "state should remain usable after runtime error");

        auto binding_type = fixture("binding-type-error.lua"_str);
        expect_script_error(checks,
                            state,
                            binding_type.as_path(),
                            luato::ErrorKind::Type,
                            "host.add"_str,
                            "host.add argument 1"_str,
                            "binding-type-error.lua"_str);

        auto binding_arity = fixture("binding-arity-error.lua"_str);
        expect_script_error(checks,
                            state,
                            binding_arity.as_path(),
                            luato::ErrorKind::Type,
                            "host.add"_str,
                            "expects 2 arguments, received 1"_str,
                            "binding-arity-error.lua"_str);

        auto binding_domain = fixture("binding-domain-error.lua"_str);
        expect_script_error(checks,
                            state,
                            binding_domain.as_path(),
                            luato::ErrorKind::Binding,
                            "host.fail"_str,
                            "native failure: reason"_str,
                            "binding-domain-error.lua"_str);
        checks.expect(invocation_drops == 1,
                      "binding error callback temporaries should be destroyed before Lua raises");

        auto after_binding = state.execute_file(native_add.as_path());
        checks.expect(after_binding.is_ok(), "state should remain usable after binding errors");
    }

    checks.expect(callback_drops == 2, "state should destroy each callback context exactly once");
    return checks.failures == 0 ? 0 : 1;
}
