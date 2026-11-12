import luato;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace {
struct AsyncChecks {
  int failures{};
  void expect(bool condition, char const *message) {
    if (condition)
      return;
    ++failures;
    __builtin_printf("FAIL: %s\n", message);
  }
};
struct Gate {
  int started{};
  int dropped{};
  bool release{};
};
struct DropCount {
  Gate &gate;
  ~DropCount() { ++gate.dropped; }
};

auto wait_at_gate(Gate &gate, i64 value)
    -> rstd::async::coro<luato::Result<i64>> {
  DropCount guard{gate};
  ++gate.started;
  while (!gate.release)
    co_await rstd::async::yield_now();
  co_return Ok(value);
}

auto arguments(i64 value) -> luato::MultiValue {
  auto result = luato::MultiValue::make();
  result.push(luato::LuaValue::Integer(value));
  return result;
}

auto overlapping_calls(luato::FunctionHandle &wait, luato::FunctionHandle &ping,
                       Gate &gate) -> rstd::async::coro<bool> {
  auto first_handle = wait.clone();
  auto second_handle = wait.clone();
  if (first_handle.is_err() || second_handle.is_err())
    co_return false;
  auto first =
      rstd::async::spawn_local(first_handle->call_async(arguments(i64(10))));
  auto second =
      rstd::async::spawn_local(second_handle->call_async(arguments(i64(20))));
  while (gate.started < 2 && !first.is_finished() && !second.is_finished())
    co_await rstd::async::yield_now();
  bool ready = gate.started == 2;
  auto synchronous = ping.call_values(luato::MultiValue::make());
  gate.release = true;
  auto a = co_await rstd::move(first);
  auto b = co_await rstd::move(second);
  if (!ready || synchronous.is_err() || a.is_err() || b.is_err() ||
      (*a).is_err() || (*b).is_err())
    co_return false;
  auto av = rstd::move(**a).into_values();
  auto bv = rstd::move(**b).into_values();
  co_return av.len() == usize(1) && bv.len() == usize(1) &&
      av[usize()].is_Integer() && bv[usize()].is_Integer() &&
      av[usize()].as_Integer().value == i64(10) &&
      bv[usize()].as_Integer().value == i64(20);
}

auto cancelled_call(luato::FunctionHandle &wait, Gate &gate)
    -> rstd::async::coro<bool> {
  auto expected = gate.started + 1;
  auto task = rstd::async::spawn_local(wait.call_async(arguments(i64(30))));
  while (gate.started < expected && !task.is_finished())
    co_await rstd::async::yield_now();
  if (gate.started != expected) {
    (void)co_await rstd::move(task);
    co_return false;
  }
  task.abort();
  auto result = co_await rstd::move(task);
  co_return result.is_err() && result.unwrap_err().is_aborted();
}
} // namespace

auto expect_async_contract() -> int {
  AsyncChecks checks;
  auto created = luato::State::create(luato::StateOptions::build_script());
  if (created.is_err())
    return 1;
  auto state = rstd::move(*created);
  Gate gate;

  luato::ModuleSpec host(String::make("host"_Str));
  host.async_function(String::make("wait"_Str),
                      [&gate](i64 value) { return wait_at_gate(gate, value); });
  host.async_function(
      String::make("fetch"_Str),
      [](String value) -> rstd::async::coro<luato::Result<String>> {
        co_await rstd::async::sleep(rstd::time::Duration::from_millis(u64(2)));
        auto output = String::make(value.as_str());
        output.push_str("!"_Str);
        co_return Ok(rstd::move(output));
      });
  host.raw_function(String::make("fail_sync"_Str), usize(0),
                    [](luato::CallFrame &) -> luato::BindingResult {
                      return Err(luato::Error::structured(
                          luato::ErrorKind::Runtime, String::make("host"_str),
                          String::make("timed out"_str),
                          String::make("http"_str),
                          String::make("timeout"_str)));
                    });
  host.async_function(String::make("fail_async"_Str),
                      []() -> rstd::async::coro<luato::Result<String>> {
                        co_return Err(luato::Error::structured(
                            luato::ErrorKind::Runtime, String::make("host"_str),
                            String::make("timed out"_str),
                            String::make("http"_str),
                            String::make("timeout"_str)));
                      });
  checks.expect(state.register_module(rstd::move(host)).is_ok(),
                "register async module");

  auto globals = state.globals();
  checks.expect(globals.is_ok(), "globals");
  if (globals.is_err())
    return checks.failures;
  auto table = globals->get_table("host"_Str);
  checks.expect(table.is_ok(), "host module table");
  if (table.is_err())
    return checks.failures;
  auto function = table->get_function("fetch"_Str);
  checks.expect(function.is_ok(), "fetch function");
  if (function.is_err())
    return checks.failures;

  auto make_arguments = [] {
    auto arguments = luato::MultiValue::make();
    arguments.push(luato::LuaValue::Bytes(Vec<u8>::from("x"_str.as_bytes())));
    return arguments;
  };

  auto runtime =
      rstd::async::RuntimeBuilder::current_thread().enable_all().build();
  if (runtime.is_err())
    return 1;
  auto result = runtime->block_on(
      rstd::async::into_future(function->call_async(make_arguments())));
  checks.expect(result.is_ok(), "async call succeeds");
  if (result.is_ok()) {
    auto values = rstd::move(*result).into_values();
    checks.expect(values.len() == usize(1), "async result count");
    if (values.len() == usize(1)) {
      auto text = values[usize()].as_utf8();
      checks.expect(text.is_ok() && *text == "x!"_str, "async result value");
    }
  }

  auto sync = function->call_values(make_arguments());
  checks.expect(sync.is_err(), "sync call of async function fails");
  auto entry = state.evaluate_entry(luato::LuaModuleSource{
      String::make("errors"_str), String::make("test:errors"_str),
      String::make("errors.lua"_str), Vec<u8>::from(R"LUA(
        return {check=function()
          for _, f in ipairs({host.fail_sync, host.fail_async}) do
            local ok, err = pcall(f)
            assert(not ok and err.domain == 'http' and err.code == 'timeout'
              and err.message == 'timed out')
          end
          host.fail_async()
        end}
      )LUA"_str.as_bytes())});
  checks.expect(entry.is_ok(), "structured error fixture loads");
  if (entry.is_ok() && entry->len() == usize(1) &&
      entry->values()[usize()].is_Table()) {
    auto target = rstd::move(*entry).into_values();
    auto module = rstd::move(target[usize()]).as_Table().value;
    auto check = module.get_function("check"_str);
    checks.expect(check.is_ok(), "structured error fixture function");
    if (check.is_ok()) {
      auto result = runtime->block_on(rstd::async::into_future(
          check->call_async(luato::MultiValue::make())));
      checks.expect(result.is_err(), "structured async error reaches host");
      if (result.is_err()) {
        auto error = rstd::move(result).unwrap_err();
        checks.expect(error.details.is_some() &&
                          error.details->domain.as_str() == "http"_str &&
                          error.details->code.as_str() == "timeout"_str &&
                          error.message.as_str() == "timed out"_str,
                      "structured async error retains details");
      }
    }
  }
  auto handle_module = luato::ModuleSpec(String::make("host_handle"_Str));
  handle_module.async_handle_function(
      String::make("fetch"_Str), usize(0),
      [](luato::CallFrame &frame)
          -> luato::Result<
              rstd::async::coro<luato::Result<luato::OpaqueHandle>>> {
        auto created = frame.new_table();
        if (created.is_err())
          return Err(rstd::move(created).unwrap_err());
        auto value = luato::take_lua_value(*created);
        auto table = rstd::move(value).as_Table().value;
        return Ok([](luato::TableHandle target)
                      -> rstd::async::coro<luato::Result<luato::OpaqueHandle>> {
          co_await rstd::async::sleep(
              rstd::time::Duration::from_millis(u64(1)));
          auto stored = target.set(
              luato::LuaValue::Bytes(Vec<u8>::from("x"_str.as_bytes())),
              luato::LuaValue::Integer(i64(9)));
          if (stored.is_err())
            co_return Err(rstd::move(stored).unwrap_err());
          co_return Ok(luato::OpaqueHandle{
              new luato::LuaValue(luato::LuaValue::Table(rstd::move(target)))});
        }(rstd::move(table)));
      });
  checks.expect(state.register_module(rstd::move(handle_module)).is_ok(),
                "register async handle module");
  auto handle_globals = state.globals();
  if (handle_globals.is_ok()) {
    auto handle_table = handle_globals->get_table("host_handle"_Str);
    checks.expect(handle_table.is_ok(), "async handle module table");
    if (handle_table.is_ok()) {
      auto fetch = handle_table->get_function("fetch"_Str);
      checks.expect(fetch.is_ok(), "async handle function");
      if (fetch.is_ok()) {
        auto result = runtime->block_on(rstd::async::into_future(
            fetch->call_async(luato::MultiValue::make())));
        checks.expect(result.is_ok(), "async handle call");
        if (result.is_ok()) {
          auto values = rstd::move(*result).into_values();
          checks.expect(values.len() == usize(1) && values[usize()].is_Table(),
                        "async handle result");
          if (values.len() == usize(1) && values[usize()].is_Table()) {
            auto table = rstd::move(values[usize()]).as_Table().value;
            auto value = table.get(
                luato::LuaValue::Bytes(Vec<u8>::from("x"_str.as_bytes())));
            checks.expect(value.is_ok() && value->is_Integer() &&
                              value->as_Integer().value == i64(9),
                          "async handle mutation while suspended");
          }
        }
      }
    }
  }

  auto concurrent = state.evaluate_entry(luato::LuaModuleSource{
      String::make("concurrent"_str), String::make("test:concurrent"_str),
      String::make("concurrent.lua"_str), Vec<u8>::from(R"LUA(
        local closed = 0
        return {
          wait = function(value)
            local guard <close> = setmetatable({}, {__close=function() closed=closed+1 end})
            return host.wait(value)
          end,
          ping = function() collectgarbage('collect'); return closed end
        }
      )LUA"_str.as_bytes())});
  checks.expect(concurrent.is_ok(), "concurrent fixture loads");
  if (concurrent.is_ok() && concurrent->len() == usize(1) &&
      concurrent->values()[usize()].is_Table()) {
    auto values = rstd::move(*concurrent).into_values();
    auto module = rstd::move(values[usize()]).as_Table().value;
    auto wait = module.get_function("wait"_str);
    auto ping = module.get_function("ping"_str);
    checks.expect(wait.is_ok() && ping.is_ok(), "concurrent functions exist");
    if (wait.is_ok() && ping.is_ok()) {
      checks.expect(runtime->block_on(rstd::async::into_future(
                        overlapping_calls(*wait, *ping, gate))),
                    "overlapping calls and collection");
      checks.expect(gate.dropped == 2, "completed operations released");
      gate.release = false;
      checks.expect(runtime->block_on(
                        rstd::async::into_future(cancelled_call(*wait, gate))),
                    "suspended call cancels");
      checks.expect(gate.dropped == 3, "cancelled operation released");
      auto closed = ping->call_values(luato::MultiValue::make());
      checks.expect(closed.is_ok() && closed->len() == usize(1) &&
                        closed->values()[usize()].is_Integer() &&
                        closed->values()[usize()].as_Integer().value == i64(3),
                    "cancel closes coroutine and preserves state");
      gate.release = true;
      auto deferred = [&] {
        auto temporary = wait->clone().unwrap();
        return temporary.call_async(arguments(i64(40)));
      }();
      auto deferred_result =
          runtime->block_on(rstd::async::into_future(rstd::move(deferred)));
      checks.expect(
          deferred_result.is_ok() && deferred_result->len() == usize(1) &&
              deferred_result->values()[usize()].is_Integer() &&
              deferred_result->values()[usize()].as_Integer().value == i64(40),
          "async call owns function reference before first poll");
    }
  }

  auto destroyed_state_call = [] {
    auto vm =
        luato::State::create(luato::StateOptions::build_script()).unwrap();
    auto value =
        vm
            .evaluate_entry(luato::LuaModuleSource{
                String::make("orphan"_str), String::make("test:orphan"_str),
                String::make("orphan.lua"_str),
                Vec<u8>::from("return function() return 1 end"_str.as_bytes())})
            .unwrap();
    auto values = rstd::move(value).into_values();
    auto function = rstd::move(values[usize()]).as_Function().value;
    return function.call_async(luato::MultiValue::make());
  }();
  auto destroyed_state_result = runtime->block_on(
      rstd::async::into_future(rstd::move(destroyed_state_call)));
  checks.expect(destroyed_state_result.is_err(),
                "async call detects destroyed state");

  auto completed_handle = [&] {
    auto vm =
        luato::State::create(luato::StateOptions::build_script()).unwrap();
    luato::ModuleSpec module(String::make("lifetime"_Str));
    module.async_function(String::make("run"_Str),
                          []() -> rstd::async::coro<luato::Result<i64>> {
                            co_await rstd::async::yield_now();
                            co_return Ok(i64(1));
                          });
    (void)vm.register_module(rstd::move(module)).unwrap();
    auto globals = vm.globals().unwrap();
    auto table = globals.get_table("lifetime"_str).unwrap();
    auto function = table.get_function("run"_str).unwrap();
    auto result = runtime->block_on(rstd::async::into_future(
        function.call_async(luato::MultiValue::make())));
    checks.expect(result.is_ok(), "lifetime async call completes");
    return function;
  }();
  auto after_destruction = runtime->block_on(rstd::async::into_future(
      completed_handle.call_async(luato::MultiValue::make())));
  checks.expect(after_destruction.is_err() &&
                    after_destruction.as_ref().unwrap_err().message.as_str() ==
                        "Lua state was destroyed"_str,
                "completed async callback does not keep state alive");

  auto raised_handle = [&] {
    auto vm =
        luato::State::create(luato::StateOptions::build_script()).unwrap();
    luato::ModuleSpec module(String::make("raising"_Str));
    module.raw_function(String::make("run"_Str), usize(0),
                        [](luato::CallFrame &frame) -> luato::BindingResult {
                          auto value = frame.new_table();
                          if (value.is_err())
                            return Err(rstd::move(value).unwrap_err());
                          frame.raise_handle(rstd::move(value).unwrap());
                          return Ok(usize(0));
                        });
    (void)vm.register_module(rstd::move(module)).unwrap();
    auto function = vm.globals()
                        .unwrap()
                        .get_table("raising"_str)
                        .unwrap()
                        .get_function("run"_str)
                        .unwrap();
    checks.expect(function.call_values(luato::MultiValue::make()).is_err(),
                  "native callback raises Lua value");
    return function;
  }();
  auto raised_after_destruction =
      raised_handle.call_values(luato::MultiValue::make());
  checks.expect(
      raised_after_destruction.is_err() &&
          raised_after_destruction.as_ref().unwrap_err().message.as_str() ==
              "Lua state was destroyed"_str,
      "raising native callback does not keep state alive");

  auto memory_state =
      luato::State::create(luato::StateOptions::build_script()).unwrap();
  auto memory_entry =
      memory_state
          .evaluate_entry(luato::LuaModuleSource{
              String::make("memory"_str), String::make("test:memory"_str),
              String::make("memory.lua"_str),
              Vec<u8>::from("return function() return 42 end"_str.as_bytes())})
          .unwrap();
  auto memory_values = rstd::move(memory_entry).into_values();
  auto memory_function = rstd::move(memory_values[usize()]).as_Function().value;
  checks.expect(memory_state.set_memory_limit(usize(1)).is_ok(),
                "set async memory limit");
  auto memory_error = runtime->block_on(rstd::async::into_future(
      memory_function.call_async(luato::MultiValue::make())));
  checks.expect(memory_error.is_err(),
                "async coroutine allocation reports memory failure");
  checks.expect(memory_state.set_memory_limit(usize()).is_ok(),
                "clear async memory limit");
  checks.expect(memory_function.call_values(luato::MultiValue::make()).is_ok(),
                "state remains usable after async allocation failure");
  auto used = memory_state.used_memory().unwrap();
  checks.expect(memory_state.set_memory_limit(used + usize(8192)).is_ok(),
                "set async argument memory limit");
  auto bytes = Vec<u8>::with_capacity(usize(100000));
  for (usize i{}; i < usize(100000); ++i)
    bytes.push(u8('x'));
  auto large_arguments = luato::MultiValue::make();
  large_arguments.push(luato::LuaValue::Bytes(rstd::move(bytes)));
  auto argument_error = runtime->block_on(rstd::async::into_future(
      memory_function.call_async(rstd::move(large_arguments))));
  checks.expect(argument_error.is_err(),
                "async argument allocation reports memory failure");
  checks.expect(memory_state.set_memory_limit(usize()).is_ok(),
                "clear argument memory limit");
  checks.expect(memory_function.call_values(luato::MultiValue::make()).is_ok(),
                "state remains usable after argument allocation failure");

  auto limited_created =
      luato::State::create(luato::StateOptions::restricted(u64(5000)));
  checks.expect(limited_created.is_ok(), "limited state creates");
  if (limited_created.is_ok()) {
    auto limited = rstd::move(*limited_created);
    int ticks = 0;
    luato::ModuleSpec timer(String::make("timer"_Str));
    timer.async_function(String::make("tick"_Str),
                         [&ticks]() -> rstd::async::coro<luato::Result<i64>> {
                           ++ticks;
                           co_await rstd::async::yield_now();
                           co_return Ok(i64());
                         });
    checks.expect(limited.register_module(rstd::move(timer)).is_ok(),
                  "limited async module registers");
    auto entry = limited.evaluate_entry(luato::LuaModuleSource{
        String::make("budget"_str), String::make("test:budget"_str),
        String::make("budget.lua"_str), Vec<u8>::from(R"LUA(
          return {run=function()
            local total = 0
            for i=1,20 do
              for j=1,200 do total = total + j end
              timer.tick()
            end
            return total
          end}
        )LUA"_str.as_bytes())});
    checks.expect(entry.is_ok(), "limited fixture loads");
    if (entry.is_ok() && entry->len() == usize(1) &&
        entry->values()[usize()].is_Table()) {
      auto values = rstd::move(*entry).into_values();
      auto module = rstd::move(values[usize()]).as_Table().value;
      auto run = module.get_function("run"_str);
      checks.expect(run.is_ok(), "limited function exists");
      if (run.is_ok()) {
        auto outcome = runtime->block_on(rstd::async::into_future(
            run->call_async(luato::MultiValue::make())));
        checks.expect(outcome.is_err() && ticks > 1 && ticks < 20,
                      "instruction budget survives async suspension");
      }
    }
  }

  return checks.failures;
}
