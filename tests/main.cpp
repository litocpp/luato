import luato;

auto expect_i18n_contract() -> int;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace {

struct Checks {
  int failures{};

  void expect(bool condition, char const *message) {
    if (condition)
      return;
    ++failures;
    __builtin_printf("FAIL: %s\n", message);
  }
};

struct DropProbe {
  int *drops;

  explicit DropProbe(int &count) noexcept : drops(rstd::addressof(count)) {}
  DropProbe(const DropProbe &) = delete;
  DropProbe &operator=(const DropProbe &) = delete;
  DropProbe(DropProbe &&other) noexcept
      : drops(rstd::exchange(other.drops, nullptr)) {}
  DropProbe &operator=(DropProbe &&) = delete;
  ~DropProbe() {
    if (drops != nullptr)
      ++*drops;
  }
};

auto borrowed_str(char const *value) noexcept -> ref<str> {
  auto bytes = slice<u8>::from_raw_parts(reinterpret_cast<byte const *>(value),
                                         usize(rstd::strlen(value)));
  return rstd::str_::from_utf8_unchecked(bytes);
}

auto fixture(ref<str> name) -> rstd::path::PathBuf {
  auto result = rstd::path::PathBuf::from(borrowed_str(LUATO_FIXTURE_DIR));
  auto child = rstd::path::PathBuf::from(name);
  result.push(child.as_path());
  return result;
}

auto lua_source(ref<str> logical_name, ref<str> identity, ref<str> display_path,
                ref<str> source) -> luato::LuaModuleSource {
  return luato::LuaModuleSource{
      String::make(logical_name), String::make(identity),
      String::make(display_path), Vec<u8>::from(source.as_bytes())};
}

void expect_module_loader(Checks &checks) {
  auto created = luato::State::create(luato::StateOptions::base());
  checks.expect(created.is_ok(), "module loader state should be created");
  if (created.is_err())
    return;
  auto state = rstd::move(created).unwrap_unchecked();
  auto resolutions = usize();
  auto configured = state.set_module_resolver(luato::ModuleResolverSpec::make(
      [&resolutions](luato::ModuleRequest request)
          -> luato::Result<luato::LuaModuleSource> {
        ++resolutions;
        if (request.requested == "pkg.api"_str) {
          return Ok(lua_source("pkg.api"_str, "builtin:pkg/api"_str,
                               "pkg/lua/api.lua"_str,
                               "module_runs = (module_runs or 0) + 1\n"
                               "local child = require('./child')\n"
                               "return { value = 42, child = child }\n"_str));
        }
        if (request.requested == "./child"_str &&
            request.importer_identity == "builtin:pkg/api"_str) {
          return Ok(lua_source("pkg.api:./child"_str, "builtin:pkg/child"_str,
                               "pkg/lua/child.lua"_str,
                               "return 'nested'\n"_str));
        }
        if (request.requested == "工具.模块"_str) {
          return Ok(lua_source("工具.模块"_str, "builtin:pkg/utf8"_str,
                               "pkg/lua/工具.lua"_str, "return 'utf8'\n"_str));
        }
        if (request.requested == "pkg.syntax"_str) {
          return Ok(lua_source("pkg.syntax"_str, "builtin:pkg/syntax"_str,
                               "pkg/lua/syntax.lua"_str,
                               "return function(\n"_str));
        }
        return Err(luato::Error::make(
            luato::ErrorKind::Module, request.importer_path.clone(),
            rstd::format("module '{}' was not found", request.requested)));
      }));
  checks.expect(configured.is_ok(), "module resolver should configure");

  auto duplicate = state.set_module_resolver(luato::ModuleResolverSpec::make(
      [](luato::ModuleRequest request)
          -> luato::Result<luato::LuaModuleSource> {
        return Err(luato::Error::make(luato::ErrorKind::Module,
                                      rstd::move(request.importer_path),
                                      String::make("unused"_str)));
      }));
  checks.expect(duplicate.is_err(), "module resolver should configure once");

  auto executed = state.execute_entry(lua_source(
      "entry"_str, "entry:module-contract"_str, "tests/module-entry.lua"_str,
      "local first = require('pkg.api')\n"
      "local second = require('pkg.api')\n"
      "assert(first == second)\n"
      "assert(first.value == 42)\n"
      "assert(first.child == 'nested')\n"
      "assert(module_runs == 1)\n"_str));
  checks.expect(executed.is_ok(), "public and relative modules should load");
  auto utf8 = state.execute_entry(
      lua_source("utf8-entry"_str, "entry:utf8"_str, "tests/utf8-entry.lua"_str,
                 "assert(require('工具.模块') == 'utf8')\n"_str));
  checks.expect(utf8.is_ok(), "UTF-8 module names and paths should load");
  checks.expect(
      resolutions == usize(4),
      "cached modules may resolve identity but should not execute again");
  auto loaded = state.loaded_modules();
  checks.expect(loaded.len() == usize(3),
                "loaded module report should contain unique executed modules");
  if (loaded.len() == usize(3)) {
    checks.expect(loaded[usize()].identity == "builtin:pkg/child"_str,
                  "relative module should be reported");
    checks.expect(loaded[usize(1)].identity == "builtin:pkg/api"_str,
                  "public module should be reported");
    checks.expect(loaded[usize(2)].identity == "builtin:pkg/utf8"_str,
                  "UTF-8 module should be reported");
  }

  auto syntax = state.execute_entry(
      lua_source("syntax-entry"_str, "entry:syntax"_str,
                 "tests/syntax-entry.lua"_str, "require('pkg.syntax')\n"_str));
  checks.expect(syntax.is_err(), "imported module syntax errors should fail");
  if (syntax.is_err()) {
    auto error = rstd::move(syntax).unwrap_err_unchecked();
    checks.expect(error.kind == luato::ErrorKind::Syntax,
                  "imported syntax error should preserve its kind");
    checks.expect(error.message.as_str().contains("syntax-entry"_str),
                  "imported syntax error should preserve the import chain");
  }

  auto missing = state.execute_entry(lua_source(
      "missing-entry"_str, "entry:missing"_str, "tests/missing-entry.lua"_str,
      "require('pkg.missing')\n"_str));
  checks.expect(missing.is_err(), "missing module should fail");
  if (missing.is_err()) {
    auto error = rstd::move(missing).unwrap_err_unchecked();
    checks.expect(error.kind == luato::ErrorKind::Module,
                  "missing module should use module error kind");
    checks.expect(error.message.as_str().contains("pkg.missing"_str),
                  "missing module error should preserve requested name");
    checks.expect(error.message.as_str().contains("missing-entry"_str),
                  "missing module error should preserve import chain");
  }

  auto cycle_state =
      luato::State::create(luato::StateOptions::base()).unwrap_unchecked();
  auto cycle_resolver =
      cycle_state.set_module_resolver(luato::ModuleResolverSpec::make(
          [](luato::ModuleRequest request)
              -> luato::Result<luato::LuaModuleSource> {
            if (request.requested == "cycle.a"_str ||
                request.requested == "./a"_str) {
              return Ok(lua_source("cycle.a"_str, "cycle:a"_str,
                                   "cycle/a.lua"_str,
                                   "return require('./b')\n"_str));
            }
            if (request.requested == "./b"_str) {
              return Ok(lua_source("cycle.b"_str, "cycle:b"_str,
                                   "cycle/b.lua"_str,
                                   "return require('./a')\n"_str));
            }
            return Err(luato::Error::make(
                luato::ErrorKind::Module, rstd::move(request.importer_path),
                String::make("cycle module was not found"_str)));
          }));
  checks.expect(cycle_resolver.is_ok(), "cycle resolver should configure");
  auto cycle = cycle_state.execute_entry(
      lua_source("cycle-entry"_str, "entry:cycle"_str,
                 "tests/cycle-entry.lua"_str, "require('cycle.a')\n"_str));
  checks.expect(cycle.is_err(), "module cycle should fail");
  if (cycle.is_err()) {
    auto error = rstd::move(cycle).unwrap_err_unchecked();
    checks.expect(error.kind == luato::ErrorKind::Module,
                  "module cycle should use module error kind");
    checks.expect(
        error.message.as_str().contains("cycle.a -> cycle.b -> cycle.a"_str),
        "module cycle should report the import chain");
  }
}

void expect_native_require_module(Checks &checks) {
  auto created = luato::State::create(luato::StateOptions::build_script());
  checks.expect(created.is_ok(), "native require state should be created");
  if (created.is_err())
    return;
  auto state = rstd::move(created).unwrap_unchecked();

  auto host = luato::ModuleSpec(String::make("lito"_str));
  host.set(String::make("api"_str), String::make("test"_str));
  auto specification = luato::NativeRequireModuleSpec(
      String::make("@lito"_str), String::make("host:lito:test"_str),
      rstd::move(host));
  specification.set_global_alias(String::make("lito"_str));
  auto registered =
      state.register_native_require_module(rstd::move(specification));
  checks.expect(registered.is_ok(), "native require module should register");

  auto resolver_calls = usize();
  auto configured = state.set_module_resolver(luato::ModuleResolverSpec::make(
      [&resolver_calls](luato::ModuleRequest request)
          -> luato::Result<luato::LuaModuleSource> {
        ++resolver_calls;
        return Err(luato::Error::make(
            luato::ErrorKind::Module, rstd::move(request.importer_path),
            rstd::format("source module '{}' was not found",
                         request.requested)));
      }));
  checks.expect(configured.is_ok(),
                "native require test resolver should configure");

  auto executed = state.execute_entry(lua_source(
      "native-entry"_str, "entry:native"_str, "tests/native-entry.lua"_str,
      "local first = require('@lito')\n"
      "local second = require('@lito')\n"
      "assert(first == second and first == lito)\n"
      "assert(first.api == 'test')\n"
      "assert(package == nil)\n"_str));
  checks.expect(executed.is_ok(), "native require module should be cached");
  checks.expect(resolver_calls == usize(),
                "native require module should bypass the source resolver");
  auto loaded = state.loaded_modules();
  checks.expect(loaded.len() == usize(1),
                "native require module should be reported once");
  if (loaded.len() == usize(1)) {
    checks.expect(loaded[usize()].logical_name == "@lito"_str,
                  "native require report should preserve the require name");
    checks.expect(loaded[usize()].identity == "host:lito:test"_str,
                  "native require report should preserve the API identity");
  }

  auto duplicate_name_module = luato::ModuleSpec(String::make("other"_str));
  auto duplicate_name =
      state.register_native_require_module(luato::NativeRequireModuleSpec(
          String::make("@lito"_str), String::make("host:other"_str),
          rstd::move(duplicate_name_module)));
  checks.expect(duplicate_name.is_err(),
                "duplicate native require names should be rejected");

  auto duplicate_identity_module =
      luato::ModuleSpec(String::make("another"_str));
  auto duplicate_identity =
      state.register_native_require_module(luato::NativeRequireModuleSpec(
          String::make("@another"_str), String::make("host:lito:test"_str),
          rstd::move(duplicate_identity_module)));
  checks.expect(duplicate_identity.is_err(),
                "duplicate native require identities should be rejected");

  auto shared_library = state.execute_entry(lua_source(
      "shared-library-entry"_str, "entry:shared-library"_str,
      "tests/shared-library-entry.lua"_str, "require('custom.so')\n"_str));
  checks.expect(shared_library.is_err(),
                "shared library requests should use the source resolver");
  checks.expect(resolver_calls == usize(1),
                "shared library requests should not use a native loader");
}

void expect_lua_ffi(Checks &checks) {
  checks.expect(LUA_VERSION_NUM == 505,
                "Lua FFI should export version constants");

  auto *state = luaL_newstate();
  checks.expect(state != nullptr, "Lua FFI should create a state");
  if (state == nullptr)
    return;

  lua_pushliteral(state, "ffi");
  checks.expect(lua_isstring(state, -1), "Lua FFI should export stack helpers");
  checks.expect(lua_gettop(state) == 1, "Lua FFI should expose the Lua stack");
  lua_pop(state, 1);
  lua_close(state);
}

void expect_script_error(Checks &checks, luato::State &state,
                         ref<rstd::path::Path> path, luato::ErrorKind kind,
                         ref<str> source, ref<str> message,
                         ref<str> traceback) {
  auto result = state.execute_file(path);
  checks.expect(result.is_err(), "script execution should return an error");
  if (result.is_ok())
    return;

  auto error = rstd::move(result).unwrap_err_unchecked();
  checks.expect(error.kind == kind, "script error kind should match");
  checks.expect(error.source.as_str().contains(source),
                "script error source should match");
  checks.expect(error.message.as_str().contains(message),
                "script error message should match");
  if (!traceback.is_empty()) {
    checks.expect(error.traceback.as_str().contains(traceback),
                  "script traceback should contain the Lua callsite");
  }
}

} // namespace

int main() {
  Checks checks;
  checks.failures += expect_i18n_contract();
  int callback_drops{};
  int invocation_drops{};
  int rejected_callback_drops{};

  expect_lua_ffi(checks);
  expect_module_loader(checks);
  expect_native_require_module(checks);

  {
    auto minimal = luato::State::create(luato::StateOptions::none());
    checks.expect(minimal.is_ok(),
                  "state without standard libraries should be created");
  }

  {
    auto created = luato::State::create(luato::StateOptions::build_script());
    checks.expect(created.is_ok(), "build script Lua state should be created");
    if (created.is_ok()) {
      auto state = rstd::move(created).unwrap_unchecked();
      auto executed = state.execute_entry(lua_source(
          "entry"_str, "entry:build-script-libraries"_str,
          "tests/build-script-libraries.lua"_str,
          "assert(string.gsub('a-b', '-', '_') == 'a_b')\n"
          "local values = { 'b', 'a' }\n"
          "table.sort(values)\n"
          "assert(values[1] == 'a')\n"
          "assert(package == nil and io == nil and os == nil)\n"_str));
      checks.expect(
          executed.is_ok(),
          "build script state should expose only safe helper libraries");
    }
  }

  {
    auto created = luato::State::create(luato::StateOptions::base());
    checks.expect(created.is_ok(), "base Lua state should be created");
    if (created.is_err())
      return 1;

    auto original = rstd::move(created).unwrap_unchecked();
    auto state = rstd::move(original);
    auto moved_from =
        original.execute_file(fixture("native-add.lua"_str).as_path());
    checks.expect(moved_from.is_err(),
                  "moved-from state should reject execution");
    if (moved_from.is_err()) {
      auto error = rstd::move(moved_from).unwrap_err_unchecked();
      checks.expect(error.kind == luato::ErrorKind::PanicInvariant,
                    "moved-from state should report its invariant error");
    }

    auto host = luato::ModuleSpec(String::make("host"_str));
    auto opaque_identity = i32(42);
    host.set(String::make("profile"_str), String::make("debug"_str));
    host.add(luato::NativeFunctionSpec::make(
        String::make("handle"_str), usize{},
        [&opaque_identity](luato::CallFrame &frame) -> luato::BindingResult {
          frame.push(luato::OpaqueHandle{
              .identity =
                  static_cast<const void *>(rstd::addressof(opaque_identity)),
          });
          return Ok(usize(1));
        }));
    host.add(luato::NativeFunctionSpec::make(
        String::make("consume"_str), usize(1),
        [&opaque_identity](luato::CallFrame &frame) -> luato::BindingResult {
          auto handle = frame.required<luato::OpaqueHandle>(usize{});
          if (handle.is_err())
            return Err(rstd::move(handle).unwrap_err_unchecked());
          frame.push(handle->identity == static_cast<const void *>(
                                             rstd::addressof(opaque_identity)));
          return Ok(usize(1));
        }));
    host.add(luato::NativeFunctionSpec::make(
        String::make("consume_table"_str), usize(1),
        [&opaque_identity](luato::CallFrame &frame) -> luato::BindingResult {
          auto request = frame.required<luato::Table>(usize{});
          if (request.is_err())
            return Err(rstd::move(request).unwrap_err_unchecked());
          auto handle = request->required<luato::OpaqueHandle>("tool"_str);
          if (handle.is_err())
            return Err(rstd::move(handle).unwrap_err_unchecked());
          frame.push(handle->identity == static_cast<const void *>(
                                             rstd::addressof(opaque_identity)));
          return Ok(usize(1));
        }));
    host.add(luato::NativeFunctionSpec::make(
        String::make("add"_str), usize(2),
        [lifetime = DropProbe(callback_drops)](
            luato::CallFrame &frame) mutable -> luato::BindingResult {
          auto lhs = frame.required<i64>(usize{});
          if (lhs.is_err()) {
            return Err(rstd::move(lhs).unwrap_err_unchecked());
          }
          auto rhs = frame.required<i64>(usize(1));
          if (rhs.is_err()) {
            return Err(rstd::move(rhs).unwrap_err_unchecked());
          }
          auto sum = rstd::move(lhs).unwrap_unchecked() +
                     rstd::move(rhs).unwrap_unchecked();
          frame.push(sum);
          return Ok(usize(1));
        }));
    host.add(luato::NativeFunctionSpec::make(
        String::make("echo"_str), usize(1),
        [](luato::CallFrame &frame) -> luato::BindingResult {
          auto value = frame.required<String>(usize{});
          if (value.is_err()) {
            return Err(rstd::move(value).unwrap_err_unchecked());
          }
          frame.push(rstd::move(value).unwrap_unchecked());
          return Ok(usize(1));
        }));
    host.add(luato::NativeFunctionSpec::make(
        String::make("truth"_str), usize{},
        [](luato::CallFrame &frame) -> luato::BindingResult {
          frame.push(true);
          return Ok(usize(1));
        }));
    host.add(luato::NativeFunctionSpec::make(
        String::make("nothing"_str), usize{},
        [](luato::CallFrame &frame) -> luato::BindingResult {
          frame.push_nil();
          return Ok(usize(1));
        }));
    host.add(luato::NativeFunctionSpec::make(
        String::make("invert"_str), usize(1),
        [](luato::CallFrame &frame) -> luato::BindingResult {
          auto value = frame.required<bool>(usize{});
          if (value.is_err())
            return Err(rstd::move(value).unwrap_err_unchecked());
          frame.push(!rstd::move(value).unwrap_unchecked());
          return Ok(usize(1));
        }));
    host.add(luato::NativeFunctionSpec::make(
        String::make("configure"_str), usize(1),
        [](luato::CallFrame &frame) -> luato::BindingResult {
          auto request = frame.required<luato::Table>(usize{});
          if (request.is_err())
            return Err(rstd::move(request).unwrap_err_unchecked());
          auto table = rstd::move(request).unwrap_unchecked();

          auto known = Vec<String>::make();
          known.push(String::make("package"_str));
          known.push(String::make("enabled"_str));
          known.push(String::make("values"_str));
          auto checked = table.reject_unknown_fields(known.as_slice());
          if (checked.is_err())
            return Err(rstd::move(checked).unwrap_err_unchecked());

          auto package = table.required<String>("package"_str);
          if (package.is_err())
            return Err(rstd::move(package).unwrap_err_unchecked());
          auto enabled = table.required<bool>("enabled"_str);
          if (enabled.is_err())
            return Err(rstd::move(enabled).unwrap_err_unchecked());
          auto values = table.required<luato::Table>("values"_str);
          if (values.is_err())
            return Err(rstd::move(values).unwrap_err_unchecked());
          auto scalars = values->scalar_entries();
          if (scalars.is_err())
            return Err(rstd::move(scalars).unwrap_err_unchecked());
          if (scalars->len() != usize(3)) {
            return Err(luato::Error::binding(
                String::make("values should contain three scalar fields"_str)));
          }

          auto result = luato::Table::make();
          auto inserted = result.set(String::make("output"_str),
                                     rstd::move(package).unwrap_unchecked());
          if (inserted.is_err())
            return Err(rstd::move(inserted).unwrap_err_unchecked());
          inserted = result.set(String::make("changed"_str),
                                rstd::move(enabled).unwrap_unchecked());
          if (inserted.is_err())
            return Err(rstd::move(inserted).unwrap_err_unchecked());
          frame.push(rstd::move(result));
          return Ok(usize(1));
        }));
    host.add(luato::NativeFunctionSpec::make(
        String::make("collect"_str), usize(1),
        [](luato::CallFrame &frame) -> luato::BindingResult {
          auto input = frame.required<luato::Array>(usize{});
          if (input.is_err())
            return Err(rstd::move(input).unwrap_err_unchecked());
          auto values = rstd::move(input).unwrap_unchecked();
          if (values.len() != usize(2)) {
            return Err(luato::Error::binding(
                String::make("collect expects two entries"_str)));
          }
          auto total = i64{};
          for (const auto &value : values.values()) {
            if (!value.is_Table()) {
              return Err(luato::Error::binding(
                  String::make("collect entries must be tables"_str)));
            }
            auto amount = value.as_Table().value->required<i64>("amount"_str);
            if (amount.is_err())
              return Err(rstd::move(amount).unwrap_err_unchecked());
            total += rstd::move(amount).unwrap_unchecked();
          }
          frame.push(total);
          return Ok(usize(1));
        }));
    host.add(luato::NativeFunctionSpec::make(
        String::make("fail"_str), usize(1),
        [lifetime = DropProbe(callback_drops), &invocation_drops](
            luato::CallFrame &frame) mutable -> luato::BindingResult {
          auto invocation = DropProbe(invocation_drops);
          auto reason = frame.required<String>(usize{});
          if (reason.is_err()) {
            return Err(rstd::move(reason).unwrap_err_unchecked());
          }
          auto value = rstd::move(reason).unwrap_unchecked();
          return Err(luato::Error::binding(
              rstd::format("native failure: {}", value.as_str())));
        }));

    auto registration = state.register_module(rstd::move(host));
    checks.expect(registration.is_ok(), "host module should register");
    if (registration.is_err())
      return 1;

    auto duplicate = luato::ModuleSpec(String::make("host"_str));
    duplicate.add(luato::NativeFunctionSpec::make(
        String::make("unused"_str), usize{},
        [lifetime = DropProbe(rejected_callback_drops)](
            luato::CallFrame &) mutable -> luato::BindingResult {
          return Ok(usize{});
        }));
    auto duplicate_result = state.register_module(rstd::move(duplicate));
    checks.expect(duplicate_result.is_err(),
                  "duplicate module should be rejected");
    if (duplicate_result.is_err()) {
      auto error = rstd::move(duplicate_result).unwrap_err_unchecked();
      checks.expect(error.kind == luato::ErrorKind::Binding,
                    "duplicate module should be a binding error");
    }
    checks.expect(
        rejected_callback_drops == 1,
        "rejected module should destroy its callback context exactly once");

    auto native_add = fixture("native-add.lua"_str);
    auto success = state.execute_file(native_add.as_path());
    checks.expect(success.is_ok(), "native add fixture should pass");

    auto structured = fixture("structured.lua"_str);
    auto structured_result = state.execute_file(structured.as_path());
    checks.expect(structured_result.is_ok(),
                  "structured binding fixture should pass");

    auto structured_array = fixture("structured-array.lua"_str);
    auto structured_array_result =
        state.execute_file(structured_array.as_path());
    checks.expect(structured_array_result.is_ok(),
                  "structured array binding fixture should pass");

    auto opaque_handle =
        state.execute_file(fixture("opaque-handle.lua"_str).as_path());
    checks.expect(opaque_handle.is_ok(), "opaque handle fixture should pass");

    auto missing = fixture("missing.lua"_str);
    expect_script_error(checks, state, missing.as_path(),
                        luato::ErrorKind::File, "missing.lua"_str,
                        "missing.lua"_str, ""_str);

    auto syntax = fixture("syntax-error.lua"_str);
    expect_script_error(checks, state, syntax.as_path(),
                        luato::ErrorKind::Syntax, "syntax-error.lua"_str,
                        "syntax-error.lua"_str, ""_str);

    auto runtime = fixture("runtime-error.lua"_str);
    expect_script_error(checks, state, runtime.as_path(),
                        luato::ErrorKind::Runtime, "runtime-error.lua"_str,
                        "boom"_str, "runtime-error.lua"_str);

    auto after_runtime = state.execute_file(native_add.as_path());
    checks.expect(after_runtime.is_ok(),
                  "state should remain usable after runtime error");

    auto binding_type = fixture("binding-type-error.lua"_str);
    expect_script_error(checks, state, binding_type.as_path(),
                        luato::ErrorKind::Type, "host.add"_str,
                        "host.add argument 1"_str,
                        "binding-type-error.lua"_str);

    auto binding_arity = fixture("binding-arity-error.lua"_str);
    expect_script_error(checks, state, binding_arity.as_path(),
                        luato::ErrorKind::Type, "host.add"_str,
                        "expects 2 arguments, received 1"_str,
                        "binding-arity-error.lua"_str);

    auto binding_domain = fixture("binding-domain-error.lua"_str);
    expect_script_error(checks, state, binding_domain.as_path(),
                        luato::ErrorKind::Binding, "host.fail"_str,
                        "native failure: reason"_str,
                        "binding-domain-error.lua"_str);
    checks.expect(invocation_drops == 1,
                  "binding error callback temporaries should be destroyed "
                  "before Lua raises");

    auto after_binding = state.execute_file(native_add.as_path());
    checks.expect(after_binding.is_ok(),
                  "state should remain usable after binding errors");

    auto unknown_field = fixture("structured-unknown-field.lua"_str);
    expect_script_error(checks, state, unknown_field.as_path(),
                        luato::ErrorKind::Binding, "host.configure"_str,
                        "host.configure argument 1.extra"_str,
                        "structured-unknown-field.lua"_str);

    auto invalid_scalar = fixture("structured-invalid-scalar.lua"_str);
    expect_script_error(checks, state, invalid_scalar.as_path(),
                        luato::ErrorKind::Type, "host.configure"_str,
                        "host.configure argument 1.values.NESTED"_str,
                        "structured-invalid-scalar.lua"_str);

    auto invalid_key = fixture("structured-invalid-key.lua"_str);
    expect_script_error(
        checks, state, invalid_key.as_path(), luato::ErrorKind::Type,
        "host.configure"_str,
        "host.configure argument 1.values must be a table, received array"_str,
        "structured-invalid-key.lua"_str);

    auto after_structured_errors = state.execute_file(structured.as_path());
    checks.expect(after_structured_errors.is_ok(),
                  "state should remain usable after structured binding errors");
  }

  checks.expect(callback_drops == 2,
                "state should destroy each callback context exactly once");
  return checks.failures == 0 ? 0 : 1;
}
