import luato;

using namespace rstd::prelude;
using namespace rstd::literals;

template <typename F>
concept TypedFunction = requires(F f) {
  luato::NativeFunctionSpec::typed(String::make(), rstd::move(f));
};

static_assert(!TypedFunction<int (*)(int *)>);
static_assert(!TypedFunction<String &(*)()>);
static_assert(!TypedFunction<void (*)(String &)>);
static_assert(!TypedFunction<luato::BindingResult (*)()>);
static_assert(!TypedFunction<decltype([](auto value) { return value; })>);

auto typed_add(i64 a, i64 b) noexcept -> i64 { return a + b; }

struct TypedCapture {
  int *drops;
  explicit TypedCapture(int &count) : drops(&count) {}
  TypedCapture(const TypedCapture &) = delete;
  TypedCapture(TypedCapture &&other)
      : drops(rstd::exchange(other.drops, nullptr)) {}
  ~TypedCapture() {
    if (drops)
      ++*drops;
  }
};

auto typed_source(ref<str> text) -> luato::LuaModuleSource {
  return {"typed"_Str, "typed:v1"_Str, "typed.lua"_Str,
          Vec<u8>::from(text.as_bytes())};
}

auto expect_typed_contract() -> int {
  int failures = 0;
  int assertion = 0;
  auto check = [&](bool ok) {
    ++assertion;
    if (!ok) {
      ++failures;
      __builtin_printf("FAIL: typed binding assertion %d\n", assertion);
    }
  };
  int drops = 0;
  int rejected_drops = 0;
  int calls = 0;
  {
    auto state =
        luato::State::create(luato::StateOptions::build_script()).unwrap();
    auto host = luato::ModuleSpec("typed"_Str);
    host.function("add"_Str, &typed_add);
    host.function("echo"_Str, [](String value) { return value; });
    host.function("borrow"_Str,
                  [](const String &value) { return value.clone(); });
    host.function("table"_Str, [](luato::Table value) { return value; });
    host.function("array"_Str, [](luato::Array value) { return value; });
    host.function("none"_Str, []() -> Option<String> { return None(); });
    host.function("some"_Str, []() -> luato::Result<Option<String>> {
      return Ok(Some("value"_Str));
    });
    host.function("empty"_Str,
                  []() -> luato::Result<empty> { return Ok(empty{}); });
    host.function("void"_Str, [] {});
    auto identity = i64(42);
    host.function("handle"_Str,
                  [&identity]() { return luato::OpaqueHandle{&identity}; });
    host.function("consume"_Str,
                  [&identity](luato::OpaqueHandle value, bool enabled) {
                    return enabled && value.identity == &identity;
                  });
    host.function("next"_Str,
                  [capture = TypedCapture(drops), count = i64{}]() mutable {
                    count += i64(1);
                    return count;
                  });
    host.function<i64(i64)>("generic"_Str, [](auto value) { return value; });
    host.function("fail"_Str, [&calls](String, bool) -> luato::Result<String> {
      ++calls;
      return Err(luato::Error::binding("typed failure"_Str));
    });
    host.function("reenter"_Str, [&state]() -> bool {
      auto nested =
          state.execute_entry(typed_source("error('should not run')"_str));
      auto registration =
          state.register_module(luato::ModuleSpec("nested"_Str));
      auto file = state.execute_file(
          rstd::path::PathBuf::from("missing-reentrant-script.lua"_str)
              .as_path());
      return nested.is_err() && registration.is_err() && file.is_err() &&
             nested.unwrap_err().message ==
                 "Lua state is already executing"_str &&
             file.unwrap_err().kind == luato::ErrorKind::Binding;
    });
    check(state.register_module(rstd::move(host)).is_ok());
    auto duplicate = luato::ModuleSpec("typed"_Str);
    duplicate.function("capture"_Str,
                       [capture = TypedCapture(rejected_drops)] {});
    check(state.register_module(rstd::move(duplicate)).is_err());
    check(rejected_drops == 1);
    check(state
              .execute_entry(typed_source(R"lua(
assert(typed.add(2, 3) == 5)
assert(typed.echo('text') == 'text' and typed.borrow('text') == 'text')
assert(typed.table({name='value'}).name == 'value')
assert(typed.array({1,2})[2] == 2)
assert(typed.none() == nil and typed.some() == 'value')
assert(select('#', typed.empty()) == 0 and select('#', typed.void()) == 0)
assert(typed.next() == 1 and typed.next() == 2)
assert(typed.generic(7) == 7 and typed.reenter())
assert(typed.consume(typed.handle(), true))
)lua"_str))
              .is_ok());
    auto bad = state.execute_entry(typed_source("typed.fail({}, 1)"_str));
    check(bad.is_err() && calls == 0);
    if (bad.is_err()) {
      auto error = rstd::move(bad).unwrap_err();
      check(error.kind == luato::ErrorKind::Type);
      check(error.message.as_str().contains("argument 1"_str));
    }
    check(state.execute_entry(typed_source("typed.fail('text', true)"_str))
              .is_err());
    check(calls == 1);
    check(state.execute_entry(typed_source("typed.add(1)"_str)).is_err());
    auto moved = rstd::move(state);
    check(moved.execute_entry(typed_source("assert(typed.next() == 3)"_str))
              .is_ok());
    check(drops == 0);
  }
  check(drops == 1);
  return failures;
}
