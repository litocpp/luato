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
    auto state = luato::State::create(luato::StateOptions::base()).unwrap();
    auto globals = state.globals().unwrap();
    auto again = state.globals().unwrap();
    check(globals.same_object(again).unwrap());
    auto table = state.create_table().unwrap();
    auto number_key = luato::LuaValue::Integer(i64(1));
    auto number = luato::LuaValue::Integer(i64(42));
    check(table.raw_set(number_key, number).is_ok());
    auto key = luato::LuaValue::Bytes(Vec<u8>::from("object"_bytes));
    auto value = luato::LuaValue::Table(table.clone().unwrap());
    check(globals.raw_set(key, value).is_ok());
    check(state
              .execute_entry(
                  typed_source("assert(object[1] == 42); object[2] = 7"_str))
              .is_ok());
    auto second_key = luato::LuaValue::Integer(i64(2));
    check(table.raw_get(second_key).unwrap().as_Integer().value == i64(7));
    auto read = globals.raw_get(key).unwrap();
    check(read.is_Table() && table.same_object(read.as_Table().value).unwrap());
    auto evaluated =
        state
            .evaluate_entry(typed_source(
                "return object, nil, function(x) return x end"_str))
            .unwrap();
    check(evaluated.len() == usize(3));
    check(evaluated.values()[usize()].is_Table() &&
          table.same_object(evaluated.values()[usize()].as_Table().value)
              .unwrap());
    check(evaluated.values()[usize(1)].is_Nil());
    auto arguments = luato::MultiValue::make();
    arguments.push(luato::LuaValue::Integer(i64(9)));
    auto function =
        evaluated.values()[usize(2)].as_Function().value.clone().unwrap();
    auto same_function = function.clone().unwrap();
    auto different =
        state.evaluate_entry(typed_source("return function() end"_str))
            .unwrap();
    auto different_function =
        different.values()[usize()].as_Function().value.clone().unwrap();
    check(function.same_object(same_function).unwrap());
    check(!function.same_object(different_function).unwrap());
    auto other_state =
        luato::State::create(luato::StateOptions::base()).unwrap();
    auto foreign =
        other_state.evaluate_entry(typed_source("return function() end"_str))
            .unwrap();
    auto foreign_function =
        foreign.values()[usize()].as_Function().value.clone().unwrap();
    check(function.same_object(foreign_function).is_err());
    auto returned = function.call_values(rstd::move(arguments)).unwrap();
    check(returned.len() == usize(1) &&
          returned.values()[usize()].as_Integer().value == i64(9));
    check(state.evaluate_entry(typed_source("return"_str)).unwrap().len() ==
          usize());
    auto text =
        state.evaluate_entry(typed_source("return 'a\\000b'"_str)).unwrap();
    check(!text.is_empty());
    auto text_values = rstd::move(text).into_values();
    auto lua_text = text_values.pop().unwrap();
    auto utf8 = lua_text.as_utf8().unwrap();
    check(utf8.len() == usize(3));
    auto not_text = luato::LuaValue::Integer(i64(1));
    check(not_text.as_utf8().is_err());
    check(state.evaluate_entry(typed_source("error('failure')"_str)).is_err());
    check(state.evaluate_entry(typed_source("return 3"_str))
              .unwrap()
              .values()[usize()]
              .as_Integer()
              .value == i64(3));
    auto moved = rstd::move(state);
    check(state.globals().is_err() && state.create_table().is_err());
    check(moved.globals().unwrap().same_object(globals).unwrap());
    check(moved.set_memory_limit(usize(1)).is_ok());
    auto failed = moved.create_table();
    check(failed.is_err());
    if (failed.is_err())
      check(failed.unwrap_err().kind == luato::ErrorKind::Memory);
    auto failed_evaluation =
        moved.evaluate_entry(typed_source("return {}"_str));
    check(failed_evaluation.is_err());
    if (failed_evaluation.is_err())
      check(failed_evaluation.unwrap_err().kind == luato::ErrorKind::Memory);
    check(moved.set_memory_limit(usize()).is_ok());
    check(moved.create_table().is_ok());
    check(moved.evaluate_entry(typed_source("return {}"_str)).is_ok());
  }
  {
    auto make_function = [&]() -> luato::Result<luato::FunctionHandle> {
      auto state =
          luato::State::create(luato::StateOptions::build_script()).unwrap();
      state
          .set_module_resolver(luato::ModuleResolverSpec::make(
              [](luato::ModuleRequest)
                  -> luato::Result<luato::LuaModuleSource> {
                return Ok(typed_source(
                    "local n = 0; local t={next=function() n=n+1; return n,nil,1.5 end, fail=function() error('failure') end, roundtrip=function(...) return ... end, empty=function() end, blob=function() return string.rep('x',1048576) end, binary='\\255\\000', mixed={[1]='x',name='x'}, sparse={[2]='x'}}; t.nested=t; return setmetatable(t,{__index=function(_,key) if key=='alias' then return t.next elseif key=='child' then return t else error('lookup error') end end, __newindex=function(t,k,v) rawset(t,'intercepted',v) end})"_str));
              }))
          .unwrap();
      auto table = state.module_table("module"_str).unwrap();
      auto duplicate = state.module_table("module"_str).unwrap();
      check(table.same_object(duplicate).unwrap());
      auto entry = typed_source("return require('module')"_str);
      entry.identity = "entry:v1"_Str;
      auto required = state.evaluate_entry(rstd::move(entry)).unwrap();
      check(required.len() == usize(1) &&
            required.values()[usize()].is_Table() &&
            table.same_object(required.values()[usize()].as_Table().value)
                .unwrap());
      {
        auto copy = table.clone().unwrap();
        check(copy.same_object(table).unwrap());
      }
      check(table.raw_get_function("alias"_str).is_err());
      check(table.get_function("alias"_str).is_ok());
      check(table.raw_get_table("child"_str).is_err());
      check(table.get_table("child"_str).is_ok());
      auto nested = table.raw_get_table("nested"_str).unwrap();
      check(table.same_object(nested).unwrap());
      auto another = luato::State::create(luato::StateOptions::base()).unwrap();
      another
          .set_module_resolver(luato::ModuleResolverSpec::make(
              [](luato::ModuleRequest)
                  -> luato::Result<luato::LuaModuleSource> {
                return Ok(typed_source("return {}"_str));
              }))
          .unwrap();
      auto foreign = another.module_table("module"_str).unwrap();
      check(table.same_object(foreign).is_err());
      auto nested_value = table.raw_get_value("nested"_str).unwrap();
      check(nested_value.is_Table() &&
            table.same_object(nested_value.as_Table().value).unwrap());
      check(state.snapshot(nested_value).is_err());
      auto mixed = table.raw_get_value("mixed"_str).unwrap();
      auto sparse = table.raw_get_value("sparse"_str).unwrap();
      check(state.snapshot(mixed).is_err());
      check(state.snapshot(sparse).is_err());
      auto foreign_value = luato::LuaValue::Table(foreign.clone().unwrap());
      check(state.snapshot(foreign_value).is_err());
      auto host_table = luato::Table::make();
      host_table.set("answer"_Str, i64(42)).unwrap();
      auto host_value = luato::Value::Table(rstd::move(host_table));
      auto converted = state.from_snapshot(host_value).unwrap();
      check(converted.is_Table());
      auto recovered = state.snapshot(converted).unwrap();
      check(recovered.is_Table() &&
            recovered.as_Table().value->required<i64>("answer"_str).unwrap() ==
                i64(42));
      auto nil_values = Vec<luato::Value>::make();
      nil_values.push(luato::Value::Nil());
      auto nil_array =
          luato::Value::Array(luato::Array::from(rstd::move(nil_values)));
      check(state.from_snapshot(nil_array).is_err());
      auto empty_array = luato::Value::Array(luato::Array::make());
      auto empty_table = state.from_snapshot(empty_array).unwrap();
      check(state.snapshot(empty_table).unwrap().is_Table());
      auto scalar = luato::Value::Number(f64(1.5));
      auto lua_scalar = state.from_snapshot(scalar).unwrap();
      check(state.snapshot(lua_scalar).unwrap().as_Number().value == f64(1.5));
      auto nil_table = luato::Table::make();
      nil_table.insert("missing"_Str, luato::Value::Nil()).unwrap();
      auto nil_map = luato::Value::Table(rstd::move(nil_table));
      check(state.from_snapshot(nil_map).is_err());
      auto binary = table.raw_get_value("binary"_str).unwrap();
      check(binary.as_utf8().is_err());
      check(state.snapshot(binary).is_err());
      check(binary.is_Bytes() && binary.as_Bytes().value.len() == usize(2));
      auto binary_copy = binary.clone().unwrap();
      check(binary_copy.is_Bytes() &&
            binary_copy.as_Bytes().value.len() == usize(2));
      if (binary.is_Bytes() && binary.as_Bytes().value.len() == usize(2)) {
        check(binary.as_Bytes().value[usize()] == u8(255));
        check(binary.as_Bytes().value[usize(1)] == u8());
      }
      check(table.raw_get_value("missing"_str).unwrap().is_Nil());
      check(table.get_value("missing"_str).is_err());
      auto numeric_key = luato::LuaValue::Integer(i64(7));
      auto answer = luato::LuaValue::Integer(i64(42));
      check(table.raw_set(numeric_key, answer).is_ok());
      check(table.raw_get(numeric_key).unwrap().as_Integer().value == i64(42));
      auto object_key = luato::LuaValue::Table(table.clone().unwrap());
      check(table.raw_set(object_key, answer).is_ok());
      check(table.raw_get(object_key).unwrap().as_Integer().value == i64(42));
      auto field_key = luato::LuaValue::Bytes(Vec<u8>::from("new"_bytes));
      check(table.set(field_key, answer).is_ok());
      check(
          table.raw_get_value("intercepted"_str).unwrap().as_Integer().value ==
          i64(42));
      check(table.raw_get(field_key).unwrap().is_Nil());
      auto nil_key = luato::LuaValue::Nil();
      check(table.raw_set(nil_key, answer).is_err());
      auto foreign_key = luato::LuaValue::Table(foreign.clone().unwrap());
      check(table.raw_get(foreign_key).is_err());
      check(table.raw_set(numeric_key, foreign_key).is_err());
      auto roundtrip = table.raw_get_function("roundtrip"_str).unwrap();
      auto function_value =
          luato::LuaValue::Function(roundtrip.clone().unwrap());
      check(state.snapshot(function_value).is_err());
      auto empty = table.raw_get_function("empty"_str).unwrap();
      auto blob = table.raw_get_function("blob"_str).unwrap();
      auto allocated = state.used_memory().unwrap();
      check(state.set_memory_limit(allocated + usize(8192)).is_ok());
      auto allocation_failure = blob.call_values(luato::MultiValue::make());
      check(allocation_failure.is_err());
      if (allocation_failure.is_err())
        check(allocation_failure.unwrap_err().kind == luato::ErrorKind::Memory);
      check(state.set_memory_limit(usize()).is_ok());
      check(empty.call_values(luato::MultiValue::make()).unwrap().len() ==
            usize());
      auto arguments = luato::MultiValue::make();
      arguments.push(luato::LuaValue::Table(table.clone().unwrap()));
      arguments.push(luato::LuaValue::Function(roundtrip.clone().unwrap()));
      arguments.push(luato::LuaValue::Nil());
      arguments.push(rstd::move(binary));
      auto returned = roundtrip.call_values(rstd::move(arguments)).unwrap();
      check(returned.len() == usize(4));
      if (returned.len() == usize(4)) {
        check(returned.values()[usize()].is_Table());
        check(table.same_object(returned.values()[usize()].as_Table().value)
                  .unwrap());
        check(returned.values()[usize(1)].is_Function());
        check(returned.values()[usize(2)].is_Nil());
        check(returned.values()[usize(3)].is_Bytes() &&
              returned.values()[usize(3)].as_Bytes().value.len() == usize(2));
      }
      auto invalid = luato::MultiValue::make();
      invalid.push(luato::LuaValue::Table(rstd::move(foreign)));
      check(roundtrip.call_values(rstd::move(invalid)).is_err());
      check(empty.call_values(luato::MultiValue::make()).is_ok());
      check(table.get_function("missing"_str).is_err());
      auto first = table.raw_get_function("next"_str).unwrap();
      auto second = duplicate.raw_get_function("next"_str).unwrap();
      auto values = first.call_multi(luato::Array::make()).unwrap();
      check(values.len() == usize(3) &&
            values.values()[usize()].as_Integer().value == i64(1));
      check(values.values()[usize(1)].is_Nil() &&
            values.values()[usize(2)].is_Number());
      {
        auto copy = first.clone().unwrap();
        check(copy.call_multi(luato::Array::make())
                  .unwrap()
                  .values()[usize()]
                  .as_Integer()
                  .value == i64(2));
      }
      auto moved = rstd::move(state);
      check(state.module_table("module"_str).is_err());
      check(duplicate.raw_get_function("missing"_str).is_err());
      auto failure = table.raw_get_function("fail"_str).unwrap();
      check(failure.call_multi(luato::Array::make()).is_err());
      auto again = second.call_multi(luato::Array::make()).unwrap();
      check(again.values()[usize()].as_Integer().value == i64(3));
      auto moved_function = rstd::move(first);
      check(first.call_multi(luato::Array::make()).is_err());
      check(moved_function.call_multi(luato::Array::make())
                .unwrap()
                .values()[usize()]
                .as_Integer()
                .value == i64(4));
      return Ok(rstd::move(moved_function));
    };
    auto function = make_function().unwrap();
    check(function.call_multi(luato::Array::make()).is_err());
    auto moved = rstd::move(function);
    check(function.call_multi(luato::Array::make()).is_err());
    check(moved.call_multi(luato::Array::make()).is_err());
  }
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
