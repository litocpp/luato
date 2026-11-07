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
    auto first =
        luato::State::create(luato::StateOptions::build_script()).unwrap();
    auto second =
        luato::State::create(luato::StateOptions::build_script()).unwrap();
    auto first_initial = first.used_memory().unwrap();
    auto second_initial = second.used_memory().unwrap();
    check(first.set_memory_limit(first_initial + usize(8192)).unwrap() ==
          usize());
    check(first
              .execute_entry(
                  typed_source("local s = string.rep('x', 1048576)"_str))
              .is_err());
    check(second
              .execute_entry(
                  typed_source("payload = string.rep('x', 1048576)"_str))
              .is_ok());
    check(second.used_memory().unwrap() > second_initial + usize(1048576));
    check(first.set_memory_limit(usize()).unwrap() ==
          first_initial + usize(8192));
    check(first.execute_entry(typed_source("return 42"_str)).is_ok());
  }
  {
    auto state =
        luato::State::create(luato::StateOptions::build_script()).unwrap();
    auto initial = state.used_memory().unwrap();
    check(initial > usize());
    check(state.set_memory_limit(initial + usize(8192)).unwrap() == usize());
    auto failure = state.execute_entry(
        typed_source("local s = string.rep('x', 1048576)"_str));
    check(failure.is_err());
    if (failure.is_err())
      check(failure.unwrap_err().kind == luato::ErrorKind::Memory);
    check(state.used_memory().unwrap() <= initial + usize(8192));
    check(state.set_memory_limit(usize()).unwrap() == initial + usize(8192));
    check(state
              .execute_entry(
                  typed_source("local s = string.rep('x', 1048576)"_str))
              .is_ok());
    auto moved = rstd::move(state);
    check(state.used_memory().is_err() &&
          state.set_memory_limit(usize()).is_err());
    check(moved.used_memory().unwrap() > usize());
  }
  {
    auto state =
        luato::State::create(luato::StateOptions::build_script()).unwrap();
    auto initial = state.used_memory().unwrap();
    check(state
              .execute_entry(
                  typed_source("payload = string.rep('x', 1048576)"_str))
              .is_ok());
    auto peak = state.used_memory().unwrap();
    check(peak > initial + usize(1048576));
    check(state
              .execute_entry(
                  typed_source("payload = nil; collectgarbage('collect')"_str))
              .is_ok());
    check(state.used_memory().unwrap() < peak - usize(1048576));
    check(state.set_memory_limit(usize(1)).unwrap() == usize());
    check(state.set_memory_limit(usize()).unwrap() == usize(1));
    check(state.execute_entry(typed_source("return 42"_str)).is_ok());
  }
  {
    auto options = luato::StateOptions::base().with_instruction_limit(u64(1));
    check(!options.text_only() && options.instruction_limit() == u64(1));
    auto state = luato::State::create(options).unwrap();
    auto result = state.execute_entry(typed_source("return 1"_str));
    check(result.is_err() && result.unwrap_err().message.as_str().contains(
                                 "instruction limit"_str));
    auto again = state.execute_entry(typed_source("return 1"_str));
    check(again.is_err());
    auto unlimited =
        luato::State::create(options.with_instruction_limit(u64())).unwrap();
    check(unlimited.execute_entry(typed_source("return 1"_str)).is_ok());
  }
  {
    auto options =
        luato::StateOptions::base().with_text_only().with_instruction_limit(
            u64(10000));
    check(options.text_only() && !options.is_restricted() &&
          options.instruction_limit() == u64(10000));
    auto state = luato::State::create(options).unwrap();
    state
        .set_module_resolver(luato::ModuleResolverSpec::make(
            [](luato::ModuleRequest) -> luato::Result<luato::LuaModuleSource> {
              return Ok(typed_source(
                  "return {none=function() end, values=function() return nil, 1.5, 7 end, failure=function() error('failure') end}"_str));
            }))
        .unwrap();
    auto empty =
        state.call_module_multi("multi"_str, "none"_str, luato::Array::make());
    check(empty.is_ok() && empty->len() == usize());
    auto values = state.call_module_multi("multi"_str, "values"_str,
                                          luato::Array::make());
    check(values.is_ok() && values->len() == usize(3));
    if (values.is_ok() && values->len() == usize(3)) {
      auto items = values->values();
      check(items[usize()].is_Nil());
      check(items[usize(1)].is_Number() &&
            items[usize(1)].as_Number().value == f64(1.5));
      check(items[usize(2)].is_Integer() &&
            items[usize(2)].as_Integer().value == i64(7));
    }
    check(
        state
            .call_module_multi("multi"_str, "failure"_str, luato::Array::make())
            .is_err());
    check(state.call_module_multi("multi"_str, "none"_str, luato::Array::make())
              .is_ok());
  }
  {
    auto state =
        luato::State::create(luato::StateOptions::restricted(u64(10000)))
            .unwrap();
    state
        .set_module_resolver(luato::ModuleResolverSpec::make(
            [](luato::ModuleRequest) -> luato::Result<luato::LuaModuleSource> {
              return Ok(typed_source(
                  "return {probe=function(item) assert(io==nil and os==nil and loadfile==nil and dofile==nil and load==nil and print==nil); return {width=item.width, value=1.5} end, loop=function() while true do end end, empty=function() return nil end, fail=function() error('test error') end}"_str));
            }))
        .unwrap();
    auto item = luato::Table::make();
    item.set("width"_Str, i64(1920)).unwrap();
    auto arguments = Vec<luato::Value>::make();
    arguments.push(luato::Value::Table(rstd::move(item)));
    auto result = state.call_module("test"_str, "probe"_str,
                                    luato::Array::from(rstd::move(arguments)));
    check(result.is_ok() && result->is_some() && (**result).is_Table());
    if (result.is_ok() && result->is_some())
      check((**result).as_Table().value->required<i64>("width"_str).unwrap() ==
            i64(1920));
    check(state.call_module("test"_str, "empty"_str, luato::Array::make())
              .unwrap()
              .is_none());
    check(state.call_module("test"_str, "fail"_str, luato::Array::make())
              .is_err());
    auto loop = state.call_module("test"_str, "loop"_str, luato::Array::make());
    check(loop.is_err() &&
          loop.unwrap_err().message.as_str().contains("instruction limit"_str));
    check(state.call_module("test"_str, "empty"_str, luato::Array::make())
              .is_ok());
  }
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
