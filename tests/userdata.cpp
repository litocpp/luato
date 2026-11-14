import luato;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace {

struct Checks {
  int assertions{};
  int failures{};

  void expect(bool condition, const char *message) {
    ++assertions;
    if (!condition) {
      ++failures;
      __builtin_printf("FAIL: userdata: %s\n", message);
    }
  }

  template <typename T>
  auto ok(const luato::Result<T> &result, const char *message) -> bool {
    expect(result.is_ok(), message);
    if (result.is_err()) {
      auto text = result.as_ref().unwrap_err().message.as_str();
      __builtin_printf(
          "  %.*s\n", static_cast<int>(text.len().to_primitive()),
          reinterpret_cast<const char *>(text.as_bytes().as_raw_ptr()));
    }
    return result.is_ok();
  }

  template <typename T>
  void error(const luato::Result<T> &result, luato::ErrorKind kind,
             const char *message) {
    expect(result.is_err(), message);
    if (result.is_err())
      expect(result.as_ref().unwrap_err().kind == kind, message);
  }
};

struct Counter {
  i64 value;
};

struct OtherCounter {
  i64 value;
};

struct Tracked {
  int *drops;
  i64 value;

  explicit Tracked(int &count, i64 number = i64())
      : drops(&count), value(number) {}
  Tracked(const Tracked &) = delete;
  auto operator=(const Tracked &) -> Tracked & = delete;
  Tracked(Tracked &&other) noexcept
      : drops(rstd::exchange(other.drops, nullptr)), value(other.value) {}
  ~Tracked() {
    if (drops)
      ++*drops;
  }
};

struct UnregisteredTracked {
  Tracked value;
};

auto source(ref<str> text) -> luato::LuaModuleSource {
  return {"userdata"_Str, "test:userdata"_Str, "userdata.lua"_Str,
          Vec<u8>::from(text.as_bytes())};
}

auto key(ref<str> text) -> luato::LuaValue {
  return luato::LuaValue::Bytes(Vec<u8>::from(text.as_bytes()));
}

auto read_counter(const Counter &counter) -> luato::Result<i64> {
  return Ok(counter.value);
}

auto counter_spec() -> luato::UserDataSpec<Counter> {
  auto spec = luato::UserDataSpec<Counter>("Counter"_Str);
  spec.method("read"_Str, [](const Counter &counter) { return counter.value; });
  spec.method("add"_Str,
              [](Counter &counter, i64 amount) -> luato::Result<i64> {
                if (amount < i64())
                  return Err(luato::Error::binding("negative increment"_Str));
                counter.value += amount;
                return Ok(counter.value);
              });
  spec.method("set"_Str,
              [](Counter &counter, i64 value) { counter.value = value; });
  spec.method("optional"_Str,
              [](const Counter &, Option<String> value) { return value; });
  spec.method("identity"_Str, [](const Counter &, luato::UserDataHandle value) {
    return value;
  });
  spec.raw_method(
      "raw_read"_Str, [](luato::CallFrame &frame) -> luato::BindingResult {
        auto self = luato::argument<luato::UserDataHandle>(frame, usize());
        if (self.is_err())
          return Err(rstd::move(self).unwrap_err());
        return luato::return_value(frame, self->with<Counter>(read_counter));
      });
  return spec;
}

auto publish(Checks &checks, luato::State &state, ref<str> name,
             const luato::UserDataHandle &handle) -> bool {
  auto globals = state.globals();
  auto copy = handle.clone();
  if (!checks.ok(globals, "get globals for userdata") ||
      !checks.ok(copy, "clone userdata for global"))
    return false;
  return checks.ok(
      globals->set(key(name), luato::LuaValue::UserData(rstd::move(*copy))),
      "publish userdata");
}

auto function(luato::State &state, ref<str> text) -> luato::FunctionHandle {
  auto values = state.evaluate_entry(source(text)).unwrap().into_values();
  return rstd::move(values[usize()]).as_Function().value;
}

void collect(Checks &checks, luato::State &state) {
  checks.ok(state.execute_entry(source(
                "collectgarbage('collect'); collectgarbage('collect')"_str)),
            "collect native userdata");
}

void fields_and_methods(Checks &checks) {
  auto state = luato::State::create(luato::StateOptions::base()).unwrap();
  auto other = luato::State::create(luato::StateOptions::base()).unwrap();
  checks.ok(other.register_userdata(counter_spec()),
            "register foreign getter type");
  auto foreign = other.create_userdata(Counter{i64(8)}).unwrap();
  auto spec = counter_spec();
  spec.field_get("value"_Str,
                 [](const Counter &counter) { return counter.value; });
  spec.field_get("empty"_Str,
                 [](const Counter &) -> Option<i64> { return None(); });
  spec.field_get("broken"_Str, [](const Counter &) -> luato::Result<i64> {
    return Err(luato::Error::make(luato::ErrorKind::Type, "getter"_Str,
                                  "getter rejected"_Str));
  });
  spec.raw_field_get("foreign"_Str, [&foreign](luato::CallFrame &frame) {
    return luato::return_value(frame, foreign.clone());
  });
  spec.raw_field_get("large"_Str, [](luato::CallFrame &frame) {
    auto text = String::make();
    for (usize index{}; index < usize(131072); ++index)
      text.push_ascii('x');
    return luato::return_value(frame, rstd::move(text));
  });
  checks.ok(state.register_userdata(rstd::move(spec)),
            "register native field getters");
  auto handle = state.create_userdata(Counter{i64(4)}).unwrap();
  auto read = function(state, R"(
return function(value)
  assert(value.value == 4 and value:read() == 4)
  value:add(3)
  assert(value.value == 7 and value.empty == nil and value.missing == nil)
  assert(getmetatable(value) == false)
  assert(not pcall(function() value.value = 1 end))
  assert(not pcall(function() value.read = function() end end))
  assert(not pcall(function() value.new_field = 1 end))
  return value.value
end
)"_str);
  auto result = read.call<i64>(handle.clone().unwrap());
  checks.expect(result.is_ok() && *result == i64(7),
                "fields stay live readonly and coexist with methods");
  auto broken =
      function(state, "return function(value) return value.broken end"_str);
  auto rejected = broken.call<i64>(handle.clone().unwrap());
  checks.error(rejected, luato::ErrorKind::Type,
               "getter preserves native error kind");
  auto wrong =
      function(state, "return function(value) return value.foreign end"_str);
  checks.error(wrong.call<luato::LuaValue>(handle.clone().unwrap()),
               luato::ErrorKind::Binding, "getter rejects foreign VM output");
  auto large =
      function(state, "return function(value) return value.large end"_str);
  state.set_memory_limit(state.used_memory().unwrap() + usize(8192)).unwrap();
  auto exhausted = large.call<luato::LuaValue>(handle.clone().unwrap());
  state.set_memory_limit(usize()).unwrap();
  checks.error(exhausted, luato::ErrorKind::Memory,
               "getter result allocation stays protected");
  auto again =
      function(state, "return function(value) return value.value end"_str);
  checks.expect(again.call<i64>(handle.clone().unwrap()).unwrap() == i64(7),
                "getter VM recovers after allocation failure");
  auto occupied = luato::UserDataSpec<OtherCounter>("Duplicate"_Str);
  occupied.field_get("same"_Str,
                     [](const OtherCounter &value) { return value.value; });
  occupied.method("same"_Str,
                  [](const OtherCounter &value) { return value.value; });
  checks.error(state.register_userdata(rstd::move(occupied)),
               luato::ErrorKind::Binding,
               "getter and method names cannot collide");
  checks.ok(state.register_userdata(
                luato::UserDataSpec<OtherCounter>("Recovered"_Str)),
            "failed getter registration leaves type available");
  auto borrowing =
      handle.with_mut<Counter>([&](Counter &) -> luato::Result<empty> {
        checks.error(again.call<i64>(handle.clone().unwrap()),
                     luato::ErrorKind::Binding,
                     "getter cannot bypass an exclusive native borrow");
        return Ok(empty{});
      });
  checks.ok(borrowing, "getter borrow failure releases the outer borrow");
}

void registration_and_methods(Checks &checks) {
  int rejected_drops{};
  auto state = luato::State::create(luato::StateOptions::base()).unwrap();
  auto unregistered = state.create_userdata(Tracked(rejected_drops));
  checks.error(unregistered, luato::ErrorKind::Binding,
               "reject unregistered type");
  checks.expect(rejected_drops == 1,
                "failed creation destroys its owned value once");
  if (!checks.ok(state.register_userdata(counter_spec()),
                 "register native type"))
    return;
  checks.error(
      state.register_userdata(luato::UserDataSpec<Counter>("Renamed"_Str)),
      luato::ErrorKind::Binding, "reject duplicate C++ type registration");
  if (!checks.ok(state.register_userdata(
                     luato::UserDataSpec<OtherCounter>("Counter"_Str)),
                 "register a distinct C++ type with the same display name"))
    return;

  auto first = state.create_userdata(Counter{i64(4)});
  auto second = state.create_userdata(Counter{i64(20)});
  auto other = state.create_userdata(OtherCounter{i64(30)});
  if (!checks.ok(first, "create first userdata") ||
      !checks.ok(second, "create second userdata") ||
      !checks.ok(other, "create other userdata"))
    return;
  auto is_counter = first->is<Counter>();
  auto is_other = first->is<OtherCounter>();
  checks.expect(is_counter.is_ok() && *is_counter, "recognize native C++ type");
  checks.expect(is_other.is_ok() && !*is_other,
                "different C++ type does not match");
  if (!publish(checks, state, "first"_str, *first) ||
      !publish(checks, state, "second"_str, *second) ||
      !publish(checks, state, "other"_str, *other))
    return;

  checks.ok(state.execute_entry(source(R"LUA(
    assert(type(first) == 'userdata')
    assert(getmetatable(first) == false)
    assert(first.__gc == nil and first.__index == nil)
    assert(first.read == second.read and first.add == second.add)
    assert(first:read() == 4 and first:raw_read() == 4)
    assert(first:add(3) == 7 and second:read() == 20)
    assert(select('#', first:set(11)) == 0 and first:read() == 11)
    assert(first:optional(nil) == nil and first:optional('label') == 'label')
    assert(first:identity(first) == first)
    assert(not pcall(first.add, first, 'bad'))
    assert(not pcall(first.read, first, 1))
    assert(not pcall(first.read, {}))
    assert(not pcall(first.read, other))
    assert(not pcall(first.raw_read, other))
    assert(not pcall(first.add, first, -1))
    assert(first:read() == 11 and first:add(1) == 12)
  )LUA"_str)),
            "typed and raw methods preserve state and reject invalid calls");
  auto value = first->with<Counter>(read_counter);
  checks.expect(value.is_ok() && *value == i64(12),
                "Lua methods mutate native storage");
}

void borrows_and_take(Checks &checks) {
  auto state = luato::State::create(luato::StateOptions::base()).unwrap();
  if (!checks.ok(state.register_userdata(counter_spec()),
                 "register borrow test type"))
    return;
  auto handle = state.create_userdata(Counter{i64(5)}).unwrap();
  auto alias = handle.clone().unwrap();
  auto same = handle.same_object(alias);
  checks.expect(same.is_ok() && *same,
                "cloned handle retains userdata identity");
  bool wrong_type_called{};
  checks.error(handle.with<OtherCounter>(
                   [&](const OtherCounter &) -> luato::Result<i64> {
                     wrong_type_called = true;
                     return Ok(i64());
                   }),
               luato::ErrorKind::Type, "wrong type cannot borrow storage");
  checks.error(handle.take<OtherCounter>(), luato::ErrorKind::Type,
               "wrong type cannot consume storage");
  checks.expect(!wrong_type_called, "wrong-type borrow never invokes callback");

  bool conflicting_call{};
  checks.ok(
      handle.with<Counter>([&](const Counter &counter) -> luato::Result<empty> {
        auto nested = alias.with<Counter>(read_counter);
        checks.expect(nested.is_ok() && *nested == counter.value,
                      "shared borrows can nest");
        checks.error(
            alias.with_mut<Counter>([&](Counter &) -> luato::Result<empty> {
              conflicting_call = true;
              return Ok(empty{});
            }),
            luato::ErrorKind::Binding, "shared borrow excludes mutable borrow");
        checks.error(alias.take<Counter>(), luato::ErrorKind::Binding,
                     "shared borrow excludes take");
        return Ok(empty{});
      }),
      "shared borrow completes");

  checks.ok(
      handle.with_mut<Counter>([&](Counter &counter) -> luato::Result<empty> {
        checks.error(
            alias.with<Counter>([&](const Counter &) -> luato::Result<i64> {
              conflicting_call = true;
              return Ok(i64());
            }),
            luato::ErrorKind::Binding, "mutable borrow excludes shared borrow");
        checks.error(
            alias.with_mut<Counter>([&](Counter &) -> luato::Result<empty> {
              conflicting_call = true;
              return Ok(empty{});
            }),
            luato::ErrorKind::Binding,
            "mutable borrow excludes another mutable borrow");
        checks.error(alias.take<Counter>(), luato::ErrorKind::Binding,
                     "mutable borrow excludes take");
        counter.value = i64(9);
        return Ok(empty{});
      }),
      "mutable borrow completes");
  checks.expect(!conflicting_call,
                "conflicting borrows never invoke callbacks");
  checks.error(handle.with_mut<Counter>([](Counter &) -> luato::Result<empty> {
    return Err(luato::Error::binding("borrow callback failed"_Str));
  }),
               luato::ErrorKind::Binding, "borrow callback error propagates");
  auto recovered = alias.with<Counter>(read_counter);
  checks.expect(recovered.is_ok() && *recovered == i64(9),
                "error releases mutable borrow");

  checks.error(
      handle.with<Counter>([](const Counter &) -> luato::Result<empty> {
        return Err(luato::Error::binding("shared callback failed"_Str));
      }),
      luato::ErrorKind::Binding, "shared callback error propagates");
  checks.ok(alias.with_mut<Counter>(
                [](Counter &) -> luato::Result<empty> { return Ok(empty{}); }),
            "error releases shared borrow");

  if (!publish(checks, state, "consumed"_str, handle))
    return;
  auto taken = alias.take<Counter>();
  checks.expect(taken.is_ok() && taken->value == i64(9),
                "take returns the owned native value");
  checks.error(handle.take<Counter>(), luato::ErrorKind::Binding,
               "take succeeds only once");
  checks.error(handle.with<Counter>(read_counter), luato::ErrorKind::Binding,
               "consumed value cannot be read");
  checks.error(alias.with_mut<Counter>([](Counter &) -> luato::Result<empty> {
    return Ok(empty{});
  }),
               luato::ErrorKind::Binding, "consumed value cannot be mutated");
  checks.ok(
      state.execute_entry(source(
          "assert(not pcall(consumed.read, consumed)); assert(2 + 2 == 4)"_str)),
      "Lua method rejects consumed userdata without poisoning VM");
}

void destruction_and_gc(Checks &checks) {
  int drops{};
  auto state = luato::State::create(luato::StateOptions::base()).unwrap();
  if (!checks.ok(
          state.register_userdata(luato::UserDataSpec<Tracked>("Tracked"_Str)),
          "register tracked type"))
    return;
  Option<luato::UserDataHandle> retained;
  {
    auto handle = state.create_userdata(Tracked(drops)).unwrap();
    retained = Some(handle.clone().unwrap());
    collect(checks, state);
    checks.expect(drops == 0,
                  "registry handles keep userdata reachable during GC");
  }
  collect(checks, state);
  checks.expect(drops == 0,
                "cloned handle keeps userdata alive after original drops");
  retained = None();
  collect(checks, state);
  checks.expect(drops == 1, "last handle release permits one GC destruction");
  collect(checks, state);
  checks.expect(drops == 1, "repeated collection does not destroy twice");

  {
    auto handle = state.create_userdata(Tracked(drops, i64(42))).unwrap();
    auto alias = handle.clone().unwrap();
    {
      auto owned = alias.take<Tracked>();
      checks.expect(owned.is_ok() && owned->value == i64(42),
                    "take moves tracked native value");
      checks.expect(drops == 1, "take does not destroy the moved owned value");
      checks.error(handle.take<Tracked>(), luato::ErrorKind::Binding,
                   "aliases observe tracked value consumption");
      collect(checks, state);
      checks.expect(drops == 1, "GC cannot destroy the taken value");
    }
    checks.expect(drops == 2, "taken value is destroyed once by its new owner");
  }
  collect(checks, state);
  checks.expect(drops == 2,
                "collected consumed userdata cannot destroy moved value again");
}

void destroyed_vm(Checks &checks) {
  int drops{};
  Option<luato::UserDataHandle> survivor;
  Option<luato::UserDataHandle> alias;
  {
    auto state = luato::State::create(luato::StateOptions::base()).unwrap();
    if (!checks.ok(state.register_userdata(
                       luato::UserDataSpec<Tracked>("Tracked"_Str)),
                   "register VM lifetime type"))
      return;
    survivor = Some(state.create_userdata(Tracked(drops)).unwrap());
    alias = Some(survivor->clone().unwrap());
    checks.expect(drops == 0, "live VM owns native value");
  }
  checks.expect(drops == 1,
                "VM destruction collects userdata despite surviving handles");
  checks.error(survivor->clone(), luato::ErrorKind::Binding,
               "destroyed VM rejects clone");
  checks.error(survivor->is<Tracked>(), luato::ErrorKind::Binding,
               "destroyed VM rejects type query");
  checks.error(survivor->same_object(*alias), luato::ErrorKind::Binding,
               "destroyed VM rejects identity query");
  bool called{};
  checks.error(
      survivor->with<Tracked>([&](const Tracked &) -> luato::Result<empty> {
        called = true;
        return Ok(empty{});
      }),
      luato::ErrorKind::Binding, "destroyed VM rejects read borrow");
  checks.error(
      survivor->with_mut<Tracked>([&](Tracked &) -> luato::Result<empty> {
        called = true;
        return Ok(empty{});
      }),
      luato::ErrorKind::Binding, "destroyed VM rejects write borrow");
  checks.error(survivor->take<Tracked>(), luato::ErrorKind::Binding,
               "destroyed VM rejects consumption");
  checks.expect(!called, "destroyed VM never invokes native borrow callback");
  survivor = None();
  alias = None();
  checks.expect(drops == 1, "dropping expired handles does not destroy twice");
}

void typed_calls(Checks &checks) {
  auto state = luato::State::create(luato::StateOptions::base()).unwrap();
  if (!checks.ok(state.register_userdata(counter_spec()),
                 "register typed call type"))
    return;
  auto handle = state.create_userdata(Counter{i64(17)}).unwrap();
  auto identity =
      function(state, "return function(value) return value end"_str);
  auto returned = identity.call<luato::UserDataHandle>(handle.clone().unwrap());
  if (checks.ok(returned, "typed call returns userdata handle")) {
    auto same = handle.same_object(*returned);
    checks.expect(same.is_ok() && *same,
                  "typed call preserves userdata identity");
  }
  auto table = state.create_table().unwrap();
  auto returned_table =
      identity.call<luato::TableHandle>(table.clone().unwrap());
  if (checks.ok(returned_table, "typed call returns table handle")) {
    auto same = table.same_object(*returned_table);
    checks.expect(same.is_ok() && *same, "typed call preserves table identity");
  }
  auto returned_function =
      identity.call<luato::FunctionHandle>(identity.clone().unwrap());
  if (checks.ok(returned_function, "typed call returns function handle")) {
    auto same = identity.same_object(*returned_function);
    checks.expect(same.is_ok() && *same,
                  "typed call preserves function identity");
  }
  auto absent = identity.call<Option<luato::UserDataHandle>>(
      Option<luato::UserDataHandle>{});
  checks.expect(absent.is_ok() && absent->is_none(),
                "typed optional handle maps nil");
  auto present = identity.call<Option<luato::UserDataHandle>>(
      Some(handle.clone().unwrap()));
  if (checks.ok(present, "typed optional handle maps a present object") &&
      present->is_some()) {
    auto same = handle.same_object(**present);
    checks.expect(same.is_ok() && *same, "optional handle preserves identity");
  } else {
    checks.expect(false, "present optional userdata is not absent");
  }

  auto scalar = identity.call<i64>(i64(23));
  checks.expect(scalar.is_ok() && *scalar == i64(23),
                "typed scalar call succeeds");
  auto boolean = identity.call<bool>(true);
  checks.expect(boolean.is_ok() && *boolean, "typed boolean call succeeds");
  auto text = identity.call<String>("hello"_Str);
  checks.expect(text.is_ok() && text->as_str() == "hello"_str,
                "typed string call succeeds");
  auto bytes = Vec<u8>::make();
  bytes.push(u8(0));
  bytes.push(u8(255));
  auto binary = identity.call<Vec<u8>>(rstd::move(bytes));
  checks.expect(binary.is_ok() && binary->len() == usize(2) &&
                    (*binary)[usize()] == u8(0) &&
                    (*binary)[usize(1)] == u8(255),
                "typed bytes preserve NUL and non-UTF8 data");
  checks.error(identity.call<luato::UserDataHandle>(i64(1)),
               luato::ErrorKind::Type,
               "typed call rejects mismatched userdata result");

  auto multiple =
      function(state, "return function(value) return value, 9, nil end"_str);
  auto values = multiple.call<luato::MultiValue>(handle.clone().unwrap());
  if (checks.ok(values, "typed call supports MultiValue") &&
      values->len() == usize(3)) {
    auto entries = rstd::move(*values).into_values();
    checks.expect(entries[usize()].is_UserData() &&
                      entries[usize(1)].is_Integer() &&
                      entries[usize(1)].as_Integer().value == i64(9) &&
                      entries[usize(2)].is_Nil(),
                  "MultiValue preserves handle, scalar and nil");
    if (entries[usize()].is_UserData()) {
      auto same = handle.same_object(entries[usize()].as_UserData().value);
      checks.expect(same.is_ok() && *same,
                    "MultiValue retains userdata identity");
    }
  } else {
    checks.expect(false, "typed MultiValue return count");
  }
  checks.error(multiple.call<luato::UserDataHandle>(handle.clone().unwrap()),
               luato::ErrorKind::Type,
               "single typed return rejects multiple values");
  auto nothing = function(state, "return function() end"_str);
  checks.ok(nothing.call<empty>(), "typed empty return accepts no values");
  checks.error(nothing.call<luato::UserDataHandle>(), luato::ErrorKind::Type,
               "typed handle return rejects no values");
  auto recovered = identity.call<i64>(i64(42));
  checks.expect(recovered.is_ok() && *recovered == i64(42),
                "conversion errors leave VM usable");
}

void cross_vm(Checks &checks) {
  auto first = luato::State::create(luato::StateOptions::base()).unwrap();
  auto second = luato::State::create(luato::StateOptions::base()).unwrap();
  if (!checks.ok(first.register_userdata(counter_spec()),
                 "register first VM type") ||
      !checks.ok(second.register_userdata(counter_spec()),
                 "register second VM type"))
    return;
  auto foreign = first.create_userdata(Counter{i64(5)}).unwrap();
  auto local = second.create_userdata(Counter{i64(8)}).unwrap();
  auto same = foreign.same_object(local);
  checks.expect(same.is_ok() && !*same,
                "different VMs never share userdata identity");
  auto globals = second.globals().unwrap();
  auto value = luato::LuaValue::UserData(foreign.clone().unwrap());
  checks.error(globals.set(key("foreign"_str), value),
               luato::ErrorKind::Binding,
               "table insertion rejects foreign userdata");
  auto echo = function(second, R"LUA(
    calls = 0
    return function(value) calls = calls + 1; return value end
  )LUA"_str);
  checks.error(echo.call<luato::UserDataHandle>(foreign.clone().unwrap()),
               luato::ErrorKind::Binding,
               "typed call rejects foreign userdata argument");
  checks.ok(
      second.execute_entry(source("assert(foreign == nil and calls == 0)"_str)),
      "cross-VM rejection happens before Lua call executes");
  auto valid = echo.call<luato::UserDataHandle>(local.clone().unwrap());
  checks.ok(valid,
            "local userdata still passes after foreign argument rejection");

  auto host = luato::ModuleSpec("host"_Str);
  host.raw_function(
      "foreign"_Str, usize(),
      [&foreign](luato::CallFrame &frame) -> luato::BindingResult {
        return luato::return_value(frame, foreign.clone());
      });
  if (!checks.ok(second.register_module(rstd::move(host)),
                 "register foreign-return callback"))
    return;
  auto callback =
      function(second, "return function() return host.foreign() end"_str);
  checks.error(callback.call<luato::MultiValue>(), luato::ErrorKind::Binding,
               "callback return rejects foreign userdata owner");
  auto first_value = foreign.with<Counter>(read_counter);
  auto second_value = local.with<Counter>(read_counter);
  checks.expect(first_value.is_ok() && *first_value == i64(5) &&
                    second_value.is_ok() && *second_value == i64(8),
                "foreign return rejection preserves both native objects");
  checks.ok(
      second.execute_entry(source("assert(calls == 1 and 3 + 4 == 7)"_str)),
      "foreign callback error leaves destination VM usable");
}

void return_policies(Checks &checks) {
  auto state =
      luato::State::create(luato::StateOptions::build_script().with_coroutine())
          .unwrap();
  auto runtime = rstd::async::RuntimeBuilder::current_thread()
                     .enable_all()
                     .build()
                     .unwrap();
  auto nothing = function(state, "return function() end"_str);
  auto nil = function(state, "return function() return nil end"_str);
  auto multiple =
      function(state, "return function() return 1, nil, {} end"_str);
  auto opaque = function(
      state,
      "return function() return coroutine.create(function() end) end"_str);
  auto failure =
      function(state, "return function() error('return failure') end"_str);
  auto options = [] {
    return luato::ReturnOptions{Some(luato::Error::make(
        luato::ErrorKind::Module, "return-test"_Str, "invalid result"_Str))};
  };
  auto module = luato::ModuleSpec("return_host"_Str);
  module.function("fail"_Str, []() -> luato::Result<empty> {
    auto error = luato::Error::structured(luato::ErrorKind::Runtime,
                                          "native"_Str, "execution failed"_Str,
                                          "returns"_Str, "failure"_Str)
                     .with_cause(luato::Error::binding("native cause"_Str));
    error.traceback = "native traceback"_Str;
    return Err(rstd::move(error));
  });
  module.async_function("wait"_Str,
                        []() -> rstd::async::coro<luato::Result<i64>> {
                          co_await rstd::async::yield_now();
                          co_return Ok(i64(42));
                        });
  checks.ok(state.register_module(rstd::move(module)),
            "register return policy callbacks");

  checks.ok(nothing.call<empty>(), "empty accepts exactly zero returns");
  checks.error(nil.call<empty>(), luato::ErrorKind::Type,
               "empty rejects a single nil");
  checks.error(multiple.call<empty>(), luato::ErrorKind::Type,
               "empty rejects multiple returns");
  auto value = nil.call<luato::LuaValue>();
  checks.expect(value.is_ok() && value->is_Nil(),
                "single LuaValue preserves nil");
  checks.error(nothing.call<luato::LuaValue>(), luato::ErrorKind::Type,
               "single LuaValue rejects missing returns");
  checks.error(multiple.call<luato::LuaValue>(), luato::ErrorKind::Type,
               "single LuaValue rejects extra returns");
  checks.error(multiple.call<luato::LuaValue>(options()),
               luato::ErrorKind::Module,
               "count error uses the supplied policy");
  auto mismatch = nil.call<luato::UserDataHandle>(options());
  checks.error(mismatch, luato::ErrorKind::Module,
               "type error uses the supplied policy");
  if (mismatch.is_err())
    checks.expect(mismatch.as_ref().unwrap_err().source == "return-test"_str &&
                      mismatch.as_ref().unwrap_err().message ==
                          "invalid result"_str,
                  "conversion policy preserves source and message");
  checks.error(failure.call<luato::LuaValue>(options()),
               luato::ErrorKind::Runtime,
               "policy does not replace execution errors");
  auto native_failure =
      function(state, "return function() return return_host.fail() end"_str);
  auto check_cause = [&](const auto &result) {
    checks.error(result, luato::ErrorKind::Runtime,
                 "execution error retains its kind");
    if (result.is_err()) {
      const auto &error = result.as_ref().unwrap_err();
      checks.expect(
          error.source == "native"_str &&
              error.message == "execution failed"_str &&
              error.details.is_some() &&
              error.details->domain == "returns"_str &&
              error.traceback == "native traceback"_str,
          "conversion policy preserves execution error details and traceback");
      auto cause = error.native_cause();
      checks.expect(cause.is_some(),
                    "conversion policy preserves execution error cause");
      if (cause.is_some()) {
        auto original = rstd::error::downcast_ref<luato::Error>(*cause);
        checks.expect(
            original.is_some() && (*original)->message == "native cause"_str,
            "execution error cause keeps its native type and message");
      }
    }
  };
  check_cause(native_failure.call<luato::LuaValue>(options()));
  check_cause(native_failure.call<luato::IgnoreReturns>(options()));
  check_cause(runtime.block_on(rstd::async::into_future(
      native_failure.call_async_typed<luato::IgnoreReturns>(options()))));

  for (auto *target : {&nothing, &nil, &multiple, &opaque}) {
    checks.ok(target->call<luato::IgnoreReturns>(),
              "sync ignore accepts arbitrary return values");
    checks.ok(runtime.block_on(rstd::async::into_future(
                  target->call_async_typed<luato::IgnoreReturns>())),
              "async ignore accepts arbitrary return values");
  }
  checks.error(runtime.block_on(rstd::async::into_future(
                   failure.call_async_typed<luato::IgnoreReturns>(options()))),
               luato::ErrorKind::Runtime,
               "async ignore propagates execution errors");
  checks.ok(runtime.block_on(
                rstd::async::into_future(nothing.call_async_typed<empty>())),
            "async empty accepts zero returns");
  checks.error(
      runtime.block_on(rstd::async::into_future(nil.call_async_typed<empty>())),
      luato::ErrorKind::Type, "async empty rejects nil");
  checks.error(runtime.block_on(rstd::async::into_future(
                   multiple.call_async_typed<luato::LuaValue>(options()))),
               luato::ErrorKind::Module,
               "async single return preserves count policy");
  checks.error(runtime.block_on(rstd::async::into_future(
                   nothing.call_async_typed<luato::LuaValue>())),
               luato::ErrorKind::Type,
               "async single return rejects missing returns");
  auto async_nil = runtime.block_on(
      rstd::async::into_future(nil.call_async_typed<luato::LuaValue>()));
  checks.expect(async_nil.is_ok() && async_nil->is_Nil(),
                "async single LuaValue preserves nil");

  auto objects = function(state, R"LUA(
    return_refs = setmetatable({}, {__mode='v'})
    return function()
      local value = setmetatable({}, {__index=function() error('unexpected snapshot') end})
      value.self = value
      local callback = function() return value end
      local thread = coroutine.create(callback)
      return_refs[1], return_refs[2], return_refs[3] = value, callback, thread
      return callback, thread, value
    end
  )LUA"_str);
  for (int repeat = 0; repeat < 3; ++repeat) {
    checks.ok(
        objects.call<luato::IgnoreReturns>(),
        "sync ignore never snapshots functions, threads or cyclic tables");
    checks.ok(
        state.execute_entry(source(
            "collectgarbage('collect'); assert(next(return_refs) == nil)"_str)),
        "sync ignore releases returned objects and restores the stack");
    checks.ok(
        runtime.block_on(rstd::async::into_future(
            objects.call_async_typed<luato::IgnoreReturns>())),
        "async ignore never snapshots functions, threads or cyclic tables");
    checks.ok(
        state.execute_entry(source(
            "collectgarbage('collect'); assert(next(return_refs) == nil)"_str)),
        "async ignore releases its thread registry reference and returned "
        "objects");
  }

  auto owned = [&] {
    auto temporary =
        function(state, "return function() return return_host.wait() end"_str);
    return temporary.call_async_typed<luato::LuaValue>(options());
  }();
  auto result = runtime.block_on(rstd::async::into_future(rstd::move(owned)));
  checks.expect(result.is_ok() && result->is_Integer() &&
                    result->as_Integer().value == i64(42),
                "unpolled typed call owns its function and conversion policy");
  auto owned_userdata = [&] {
    state.register_userdata(counter_spec()).unwrap();
    auto temporary = function(state, R"LUA(
      return function(value) return_host.wait(); return value end
    )LUA"_str);
    auto handle = state.create_userdata(Counter{i64(17)}).unwrap();
    return temporary.call_async_typed<luato::UserDataHandle>(
        options(), rstd::move(handle));
  }();
  auto userdata =
      runtime.block_on(rstd::async::into_future(rstd::move(owned_userdata)));
  if (checks.ok(userdata, "unpolled typed call owns its userdata argument"))
    checks.expect(userdata->with<Counter>(read_counter).unwrap() == i64(17),
                  "userdata stays valid after the original function and handle "
                  "are destroyed");
  checks.ok(state.execute_entry(source("assert(6*7==42)"_str)),
            "return policies leave the VM usable");
}

void frame_creation_and_errors(Checks &checks) {
  int drops{};
  int unregistered_drops{};
  auto state = luato::State::create(luato::StateOptions::base()).unwrap();
  if (!checks.ok(state.register_userdata(counter_spec()),
                 "register frame-created type") ||
      !checks.ok(
          state.register_userdata(luato::UserDataSpec<Tracked>("Tracked"_Str)),
          "register frame lifetime type"))
    return;
  auto host = luato::ModuleSpec("host"_Str);
  host.raw_function("make"_Str, usize(1),
                    [](luato::CallFrame &frame) -> luato::BindingResult {
                      auto number = luato::argument<i64>(frame, usize());
                      if (number.is_err())
                        return Err(rstd::move(number).unwrap_err());
                      return luato::return_value(
                          frame, frame.create_userdata(Counter{*number}));
                    });
  host.raw_function(
      "echo"_Str, usize(1),
      [](luato::CallFrame &frame) -> luato::BindingResult {
        return luato::return_value(
            frame, luato::argument<luato::UserDataHandle>(frame, usize()));
      });
  host.raw_function("fail"_Str, usize(),
                    [&drops](luato::CallFrame &frame) -> luato::BindingResult {
                      auto created = frame.create_userdata(Tracked(drops));
                      if (created.is_err())
                        return Err(rstd::move(created).unwrap_err());
                      return Err(luato::Error::binding(
                          "failure after userdata creation"_Str));
                    });
  host.raw_function(
      "staged_fail"_Str, usize(),
      [&drops](luato::CallFrame &frame) -> luato::BindingResult {
        auto created = frame.create_userdata(Tracked(drops));
        if (created.is_err())
          return Err(rstd::move(created).unwrap_err());
        auto staged = luato::return_value(frame, rstd::move(*created));
        if (staged.is_err())
          return staged;
        return Err(luato::Error::binding("failure after staging userdata"_Str));
      });
  host.raw_function(
      "unregistered"_Str, usize(),
      [&unregistered_drops](luato::CallFrame &frame) -> luato::BindingResult {
        return luato::return_value(
            frame, frame.create_userdata(
                       UnregisteredTracked{Tracked(unregistered_drops)}));
      });
  host.raw_function("state_create"_Str, usize(),
                    [&state](luato::CallFrame &frame) -> luato::BindingResult {
                      return luato::return_value(
                          frame, state.create_userdata(Counter{i64(1)}));
                    });
  if (!checks.ok(state.register_module(rstd::move(host)),
                 "register userdata frame callbacks"))
    return;
  checks.ok(state.execute_entry(source(R"LUA(
    local object = host.make(14)
    assert(object:read() == 14 and host.echo(object) == object)
    assert(not pcall(host.make, 'wrong'))
    assert(not pcall(host.echo, {}))
    assert(not pcall(host.state_create))
    assert(not pcall(host.unregistered))
    assert(host.make(15):read() == 15)
  )LUA"_str)),
            "frame creation and argument errors preserve current Lua thread");
  checks.expect(unregistered_drops == 1,
                "failed frame creation destroys unregistered owned value once");
  auto fail = function(state, "return function() host.fail() end"_str);
  auto failure = fail.call<empty>();
  checks.error(failure, luato::ErrorKind::Binding,
               "frame creation error reaches caller");
  if (failure.is_err())
    checks.expect(failure.as_ref().unwrap_err().message.as_str().contains(
                      "failure after userdata creation"_str),
                  "creation error preserves message");
  collect(checks, state);
  checks.expect(drops == 1,
                "failed callback releases its newly created userdata");

  auto staged =
      function(state, "return function() return host.staged_fail() end"_str);
  {
    auto staged_failure = staged.call<luato::MultiValue>();
    checks.error(staged_failure, luato::ErrorKind::Binding,
                 "callback error takes precedence over staged handle returns");
    if (staged_failure.is_err())
      checks.expect(
          staged_failure.as_ref().unwrap_err().message.as_str().contains(
              "failure after staging userdata"_str),
          "staged error preserves message");
  }
  collect(checks, state);
  checks.expect(drops == 2,
                "failed callback discards staged userdata without leaking");
  checks.ok(
      state.execute_entry(source("assert(host.make(42):add(1) == 43)"_str)),
      "VM remains usable after callback failures");
}

struct Gate {
  int started{};
  int dropped{};
  bool release{};
};

struct PendingDrop {
  Gate &gate;
  ~PendingDrop() { ++gate.dropped; }
};

auto wait_owned(Gate &gate, luato::UserDataHandle handle, i64 amount)
    -> rstd::async::coro<luato::Result<luato::MultiValue>> {
  PendingDrop guard{gate};
  ++gate.started;
  while (!gate.release)
    co_await rstd::async::yield_now();
  auto changed = handle.with_mut<Counter>(
      [amount](Counter &counter) -> luato::Result<empty> {
        counter.value += amount;
        return Ok(empty{});
      });
  if (changed.is_err())
    co_return Err(rstd::move(changed).unwrap_err());
  auto values = luato::MultiValue::make();
  values.push(luato::LuaValue::UserData(rstd::move(handle)));
  co_return Ok(rstd::move(values));
}

auto cancel_typed(luato::FunctionHandle &function, luato::UserDataHandle handle,
                  Gate &gate) -> rstd::async::coro<bool> {
  auto task =
      rstd::async::spawn_local(function.call_async_typed<luato::UserDataHandle>(
          handle.clone().unwrap()));
  for (unsigned attempts = 0;
       gate.started == 0 && !task.is_finished() && attempts < 10000; ++attempts)
    co_await rstd::async::yield_now();
  if (gate.started != 1) {
    task.abort();
    (void)co_await rstd::move(task);
    co_return false;
  }
  auto access = handle.with_mut<Counter>(
      [](Counter &) -> luato::Result<empty> { return Ok(empty{}); });
  task.abort();
  auto result = co_await rstd::move(task);
  co_return access.is_ok() && result.is_err() &&
      result.unwrap_err().is_aborted();
}

void async_typed_and_cancel(Checks &checks) {
  Gate gate;
  auto state = luato::State::create(luato::StateOptions::base()).unwrap();
  auto spec = counter_spec();
  spec.async_method("wait"_Str,
                    [&gate](luato::UserDataHandle self, i64 amount)
                        -> rstd::async::coro<luato::Result<luato::MultiValue>> {
                      return wait_owned(gate, rstd::move(self), amount);
                    });
  if (!checks.ok(state.register_userdata(rstd::move(spec)),
                 "register async userdata method"))
    return;
  auto handle = state.create_userdata(Counter{i64(10)}).unwrap();
  auto wait =
      function(state, "return function(value) return value:wait(3) end"_str);
  auto runtime =
      rstd::async::RuntimeBuilder::current_thread().enable_all().build();
  checks.expect(runtime.is_ok(), "create userdata async runtime");
  if (runtime.is_err())
    return;
  checks.expect(runtime->block_on(rstd::async::into_future(
                    cancel_typed(wait, handle.clone().unwrap(), gate))),
                "typed async call cancels without retaining a native borrow");
  checks.expect(gate.dropped == 1,
                "cancellation destroys pending userdata coroutine once");
  auto unchanged = handle.with<Counter>(read_counter);
  checks.expect(unchanged.is_ok() && *unchanged == i64(10),
                "cancelled userdata method does not mutate after await");
  gate.release = true;
  auto result = runtime->block_on(rstd::async::into_future(
      wait.call_async_typed<luato::UserDataHandle>(handle.clone().unwrap())));
  if (checks.ok(result, "typed async method returns owned userdata")) {
    auto same = handle.same_object(*result);
    checks.expect(same.is_ok() && *same,
                  "typed async result preserves userdata identity");
  }
  checks.expect(gate.started == 2 && gate.dropped == 2,
                "successful async call also destroys its coroutine once");
  auto changed = handle.with<Counter>(read_counter);
  checks.expect(changed.is_ok() && *changed == i64(13),
                "fresh borrow after suspension mutates native userdata");
  checks.ok(state.execute_entry(source("assert(6 * 7 == 42)"_str)),
            "VM remains usable after typed async cancellation and completion");
}

struct AsyncPayload {
  i64 value;
  Vec<String> labels;
};
constexpr auto rstd_serde_definition(AsyncPayload *) {
  using namespace rstd::serde;
  return RecordDefinition<AsyncPayload, "AsyncPayload", Direction::Both,
                          UnknownFieldPolicy::Reject,
                          Member<&AsyncPayload::value, "value">,
                          Member<&AsyncPayload::labels, "labels">>{};
}

void async_results(Checks &checks) {
  auto state = luato::State::create(luato::StateOptions::base()).unwrap();
  auto foreign_state =
      luato::State::create(luato::StateOptions::base()).unwrap();
  checks.ok(foreign_state.register_userdata(counter_spec()),
            "register foreign counter");
  auto foreign = foreign_state.create_userdata(Counter{i64(1)}).unwrap();
  auto spec = counter_spec();
  int failures = 0;
  int argument_failures = 0, execution_failures = 0, return_failures = 0;
  int result_drops = 0;
  auto failed = [&](const luato::UserDataHandle &, const luato::Error &,
                    luato::AsyncMethodErrorKind kind) {
    ++failures;
    switch (kind) {
    case luato::AsyncMethodErrorKind::Arguments:
      ++argument_failures;
      break;
    case luato::AsyncMethodErrorKind::Execution:
      ++execution_failures;
      break;
    case luato::AsyncMethodErrorKind::Return:
      ++return_failures;
      break;
    }
  };
  spec.async_method(
      "sum"_Str,
      [](luato::UserDataHandle self, i64 amount,
         Option<i64> extra) -> rstd::async::coro<luato::Result<i64>> {
        co_await rstd::async::yield_now();
        co_return self.with_mut<Counter>(
            [&](Counter &counter) -> luato::Result<i64> {
              counter.value += amount + extra.unwrap_or(i64());
              return Ok(counter.value);
            });
      },
      failed);
  spec.async_method(
      "count"_Str,
      [](luato::UserDataHandle, luato::Variadic<luato::LuaValue> values)
          -> rstd::async::coro<luato::Result<i64>> {
        co_await rstd::async::yield_now();
        co_return Ok(i64(values.values.len().to_primitive()));
      });
  spec.async_method(
      "payload"_Str,
      [](luato::UserDataHandle,
         i64 number) -> rstd::async::coro<luato::Result<luato::AsyncReturn>> {
        co_await rstd::async::yield_now();
        AsyncPayload payload{number, {}};
        payload.labels.push("first"_Str);
        co_return Ok(luato::AsyncReturn::make([payload = rstd::move(payload)](
                                                  luato::CallFrame &frame) {
          return luato::return_value(frame, luato::to_value(frame, payload));
        }));
      },
      failed);
  spec.async_method(
      "bad"_Str,
      [](luato::UserDataHandle)
          -> rstd::async::coro<luato::Result<luato::AsyncReturn>> {
        co_await rstd::async::yield_now();
        co_return Ok(luato::AsyncReturn::make(
            [](luato::CallFrame &) -> luato::BindingResult {
              return Err(luato::Error::make(luato::ErrorKind::Type,
                                            "encoder"_Str,
                                            "cannot encode result"_Str));
            }));
      },
      failed);
  spec.async_method(
      "foreign"_Str,
      [foreign = rstd::move(foreign)](luato::UserDataHandle) mutable
          -> rstd::async::coro<luato::Result<luato::UserDataHandle>> {
        co_await rstd::async::yield_now();
        co_return foreign.clone();
      },
      failed);
  spec.async_method(
      "task_error"_Str,
      [](luato::UserDataHandle) -> rstd::async::coro<luato::Result<i64>> {
        co_await rstd::async::yield_now();
        co_return Err(luato::Error::make(luato::ErrorKind::Runtime,
                                         "worker"_Str, "worker rejected"_Str));
      },
      failed);
  spec.async_method(
      "limited"_Str,
      [&state, &result_drops](luato::UserDataHandle)
          -> rstd::async::coro<luato::Result<luato::AsyncReturn>> {
        co_await rstd::async::yield_now();
        auto text = String::make();
        for (usize index{}; index < usize(131072); ++index)
          text.push_ascii('x');
        auto used = state.used_memory().unwrap();
        state.set_memory_limit(used + usize(4096)).unwrap();
        co_return Ok(luato::AsyncReturn::make(
            [text = rstd::move(text),
             tracked = Tracked(result_drops)](luato::CallFrame &frame) mutable {
              return luato::return_value(frame, rstd::move(text));
            }));
      },
      failed);
  checks.ok(state.register_userdata(rstd::move(spec)),
            "register typed async results");
  auto handle = state.create_userdata(Counter{i64(10)}).unwrap();
  auto run = function(state, R"(
return function(value)
  assert(value:sum(2) == 12)
  assert(value:sum(1,3) == 16)
  assert(value:count(1,'two',{}) == 3)
  local first = value:payload(7)
  local second = value:payload(9)
  assert(first.value == 7 and second.value == 9 and first ~= second)
  assert(first.labels[1] == 'first')
  assert(not pcall(function() value:sum('bad') end))
  assert(not pcall(function() value:sum() end))
  local ok, err = pcall(function() value:bad() end)
  assert(not ok)
  return first
end
)"_str);
  auto runtime = rstd::async::RuntimeBuilder::current_thread()
                     .enable_all()
                     .build()
                     .unwrap();
  auto result = runtime.block_on(
      run.call_async_typed<luato::TableHandle>(handle.clone().unwrap()));
  if (checks.ok(result, "typed userdata results encode inside continuation")) {
    auto payload = luato::from_value<AsyncPayload>(
        luato::LuaValue::Table(rstd::move(*result)));
    checks.expect(payload.is_ok() && payload->value == i64(7),
                  "decode resumed structured result");
  }
  checks.expect(failures == 3,
                "observe argument and protected result errors once");
  auto bad =
      function(state, "return function(value) return value:bad() end"_str);
  auto rejected = runtime.block_on(
      bad.call_async_typed<luato::LuaValue>(handle.clone().unwrap()));
  checks.error(rejected, luato::ErrorKind::Type,
               "preserve continuation encoding error kind");
  checks.expect(failures == 4 && rejected.is_err() &&
                    rejected.as_ref().unwrap_err().message.as_str().contains(
                        "cannot encode result"_str),
                "preserve encoding cause and notify once");
  auto foreign_call =
      function(state, "return function(value) return value:foreign() end"_str);
  auto wrong_vm = runtime.block_on(
      foreign_call.call_async_typed<luato::LuaValue>(handle.clone().unwrap()));
  checks.error(wrong_vm, luato::ErrorKind::Binding,
               "typed async return rejects foreign userdata");
  checks.expect(failures == 5, "foreign async return notifies once");
  auto limited =
      function(state, "return function(value) return value:limited() end"_str);
  auto exhausted = runtime.block_on(
      limited.call_async_typed<luato::LuaValue>(handle.clone().unwrap()));
  checks.ok(state.set_memory_limit(usize()),
            "clear async result allocation limit");
  checks.error(exhausted, luato::ErrorKind::Memory,
               "async result allocation failure stays protected");
  checks.expect(failures == 6 && result_drops == 1,
                "allocation failure notifies and drops owned result once");
  auto task_error = function(
      state, "return function(value) return value:task_error() end"_str);
  auto execution = runtime.block_on(
      task_error.call_async_typed<luato::LuaValue>(rstd::move(handle)));
  checks.error(execution, luato::ErrorKind::Runtime,
               "async execution error reaches caller");
  checks.expect(
      failures == 7 && argument_failures == 2 && execution_failures == 1 &&
          return_failures == 4,
      "async observer distinguishes argument execution and return failures");
  auto table = state.create_table().unwrap();
  auto long_key = String::make();
  for (usize index{}; index < usize(131072); ++index)
    long_key.push_ascii('k');
  state.set_memory_limit(state.used_memory().unwrap() + usize(4096)).unwrap();
  auto lookup = table.get_in_call(
      luato::LuaValue::Bytes(rstd::move(long_key).into_bytes()));
  checks.ok(state.set_memory_limit(usize()),
            "clear suspended table lookup limit");
  checks.error(
      lookup, luato::ErrorKind::Memory,
      "owned table lookup outside a callback protects allocation failures");
  checks.ok(state.execute_entry(source("assert(6*7==42)"_str)),
            "state recovers after typed async encoding errors");
}

} // namespace

auto expect_userdata_contract() -> int {
  Checks checks;
  registration_and_methods(checks);
  fields_and_methods(checks);
  borrows_and_take(checks);
  destruction_and_gc(checks);
  destroyed_vm(checks);
  typed_calls(checks);
  return_policies(checks);
  cross_vm(checks);
  frame_creation_and_errors(checks);
  async_typed_and_cancel(checks);
  async_results(checks);
  __builtin_printf("userdata: %d assertions, %d failures\n", checks.assertions,
                   checks.failures);
  return checks.failures;
}
