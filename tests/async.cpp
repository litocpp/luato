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
  return checks.failures;
}
