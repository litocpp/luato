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
} // namespace

auto expect_async_contract() -> int {
  AsyncChecks checks;
  auto created = luato::State::create(luato::StateOptions::build_script());
  if (created.is_err())
    return 1;
  auto state = rstd::move(*created);

  luato::ModuleSpec host(String::make("host"_Str));
  host.async_function(
      String::make("fetch"_Str),
      [](String value) -> rstd::async::coro<luato::Result<String>> {
        co_await rstd::async::sleep(rstd::time::Duration::from_millis(u64(2)));
        auto output = String::make(value.as_str());
        output.push_str("!"_Str);
        co_return Ok(rstd::move(output));
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
          (void)target.set(
              luato::LuaValue::Bytes(Vec<u8>::from("x"_str.as_bytes())),
              luato::LuaValue::Integer(i64(9)));
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
        }
      }
    }
  }

  return checks.failures;
}
