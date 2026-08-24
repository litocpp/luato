import luato.i18n;

using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::string::String;
using ::alloc::vec::Vec;

namespace {

struct Checks {
  int failures{};

  void expect(bool condition, const char *message) {
    if (condition)
      return;
    ++failures;
    __builtin_printf("FAIL: i18n %s\n", message);
  }
};

auto options() -> luato::i18n::ExtractionOptions {
  auto callee = Vec<String>::make();
  callee.push(String::make("tr"_str));
  return luato::i18n::ExtractionOptions{luato::i18n::CallSpec{
      rstd::move(callee), usize(), usize(1), String::make("TRANSLATORS:"_str),
      Some(String::make("tr"_str))}};
}

auto one_source(ref<str> path, ref<str> text) -> Vec<luato::i18n::SourceFile> {
  auto sources = Vec<luato::i18n::SourceFile>::make();
  sources.push(
      luato::i18n::SourceFile{rstd::parse::SourceId(path), text.as_bytes()});
  return sources;
}

auto extraction(ref<str> path, ref<str> text)
    -> luato::i18n::Result<luato::i18n::Extraction> {
  auto sources = one_source(path, text);
  auto settings = options();
  return luato::i18n::extract(sources.as_slice(), settings);
}

void expect_extraction(Checks &checks) {
  auto result = extraction(
      "plugin/main.lua"_str,
      "-- TRANSLATORS: shown in the account panel\n"
      "local label <const> = tr(\"Sta\\u{74}us\")\n"
      "local fake = 'tr(\\\"ignored\\\")'\n"
      "local function render(value, ...)\n"
      "  if value then\n"
      "    return { text = tr([=[Help]=]) }\n"
      "  elseif false then return {} else return {} end\n"
      "end\n"
      "for index = 1, 3 do label = label .. index end\n"
      "for key, value in pairs({ answer = 42 }) do render(value) end\n"
      "repeat label = label until true\n"
      "while false do break end\n"
      "::again::\n"
      "if false then goto again end\n"_str);
  checks.expect(result.is_ok(), "complete Lua chunk should parse");
  if (result.is_err())
    return;
  auto value = rstd::move(result).unwrap_unchecked();
  checks.expect(value.messages.len() == usize(2),
                "canonical calls should be extracted");
  if (value.messages.len() != usize(2))
    return;
  checks.expect(value.messages[usize()].msgid == "Help"_str,
                "messages should use stable msgid order");
  checks.expect(value.messages[usize(1)].msgid == "Status"_str,
                "short string escapes should decode once");
  checks.expect(value.messages[usize(1)].translator_notes.len() == usize(1),
                "adjacent translator comments should attach");
}

void expect_errors(Checks &checks) {
  auto dynamic = extraction("dynamic.lua"_str,
                            "local text = 'dynamic'\nreturn tr(text)\n"_str);
  checks.expect(dynamic.is_err(), "dynamic canonical argument should fail");
  if (dynamic.is_err()) {
    checks.expect(dynamic.unwrap_err().code ==
                      luato::i18n::DiagnosticCode::InvalidTranslationCall,
                  "dynamic argument should use a stable diagnostic code");
  }

  auto arity = extraction("arity.lua"_str, "return tr('one', 'two')\n"_str);
  checks.expect(arity.is_err(), "translation call arity should be exact");

  auto empty = extraction("empty.lua"_str, "return tr('')\n"_str);
  checks.expect(empty.is_err(), "empty message ids should fail");
  if (empty.is_err()) {
    checks.expect(empty.unwrap_err().code ==
                      luato::i18n::DiagnosticCode::EmptyMessageId,
                  "empty message should use a stable diagnostic code");
  }

  auto shadow = extraction("shadow.lua"_str,
                           "local tr = function(value) return value end\n"
                           "return tr('Text')\n"_str);
  checks.expect(shadow.is_err(),
                "reserved host globals should not be shadowed");

  auto alias = extraction(
      "alias.lua"_str, "local translate = tr\nreturn translate('Text')\n"_str);
  checks.expect(alias.is_err(), "translation globals should not be aliased");

  auto computed =
      extraction("computed.lua"_str, "return _ENV['tr']('Text')\n"_str);
  checks.expect(computed.is_err(),
                "translation globals should not use computed access");

  auto qualified =
      extraction("qualified.lua"_str, "return _ENV.tr('Text')\n"_str);
  checks.expect(
      qualified.is_err(),
      "translation globals should not use qualified environment access");

  auto malformed = extraction("broken.lua"_str, "return function(\n"_str);
  checks.expect(malformed.is_err(),
                "truncated Lua should fail syntax validation");

  auto invalid_escape = extraction("escape.lua"_str, "return tr('\\q')\n"_str);
  checks.expect(invalid_escape.is_err(),
                "invalid Lua escapes should fail lexing");
}

void expect_no_execution(Checks &checks) {
  auto parsed = extraction("effects.lua"_str, "while true do end\n"
                                              "os.execute('touch nope')\n"
                                              "error('must not run')\n"
                                              "return tr('Safe')\n"_str);
  checks.expect(parsed.is_ok(), "static extraction should not execute Lua");
}

auto declared_extraction(ref<str> path, ref<str> msgid)
    -> luato::i18n::Extraction {
  auto occurrences = Vec<luato::i18n::Occurrence>::make();
  occurrences.push(
      luato::i18n::Occurrence{rstd::parse::SourceId(path),
                              {},
                              {},
                              rstd::parse::SourcePosition{usize(1), usize(1)},
                              None()});
  auto messages = Vec<luato::i18n::Message>::make();
  messages.push(luato::i18n::Message{
      String::make(msgid), rstd::move(occurrences), Vec<String>::make()});
  return luato::i18n::Extraction{rstd::move(messages),
                                 Vec<luato::i18n::Diagnostic>::make()};
}

void expect_merge(Checks &checks) {
  auto source = extraction("plugin/main.lua"_str, "return tr('Status')\n"_str);
  checks.expect(source.is_ok(), "merge source should extract");
  if (source.is_err())
    return;

  auto inputs = Vec<luato::i18n::Extraction>::make();
  inputs.push(rstd::move(source).unwrap_unchecked());
  inputs.push(declared_extraction("plugin.toml"_str, "Status"_str));
  inputs.push(declared_extraction("plugin.toml"_str, "Setting"_str));
  auto merged = luato::i18n::merge_extractions(rstd::move(inputs));
  checks.expect(merged.messages.len() == usize(2),
                "merged messages should be unique and sorted");
  checks.expect(merged.messages[usize(1)].occurrences.len() == usize(2),
                "matching messages should retain all occurrences");
}

} // namespace

auto expect_i18n_contract() -> int {
  Checks checks;
  expect_extraction(checks);
  expect_errors(checks);
  expect_no_execution(checks);
  expect_merge(checks);
  return checks.failures;
}
