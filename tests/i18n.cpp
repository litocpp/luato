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
  callee.push(String::make("W"_str));
  callee.push(String::make("I18n"_str));
  callee.push(String::make("tr"_str));
  return luato::i18n::ExtractionOptions{luato::i18n::CallSpec{
      rstd::move(callee), usize(), usize(1), usize(2),
      String::make("TRANSLATORS:"_str), Some(String::make("W"_str))}};
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
      "local label <const> = W.I18n.tr(\"account.label\", \"Sta\\u{74}us\")\n"
      "local fake = 'W.I18n.tr(\\\"ignored\\\", \\\"Ignored\\\")'\n"
      "local computed = W.I18n[\"tr\"](\"ignored\", \"Ignored\")\n"
      "local function render(value, ...)\n"
      "  if value then\n"
      "    return { text = W.I18n.tr([=[account.help]=], [[Help]]) }\n"
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
  checks.expect(value.messages[usize()].id == "account.help"_str,
                "messages should use stable id order");
  checks.expect(value.messages[usize(1)].fallback == "Status"_str,
                "short string escapes should decode once");
  checks.expect(value.messages[usize(1)].translator_notes.len() == usize(1),
                "adjacent translator comments should attach");
}

void expect_errors(Checks &checks) {
  auto dynamic = extraction(
      "dynamic.lua"_str,
      "local id = 'dynamic'\nreturn W.I18n.tr(id, 'Fallback')\n"_str);
  checks.expect(dynamic.is_err(), "dynamic canonical arguments should fail");
  if (dynamic.is_err()) {
    checks.expect(dynamic.unwrap_err().code ==
                      luato::i18n::DiagnosticCode::InvalidTranslationCall,
                  "dynamic argument should use a stable diagnostic code");
  }

  auto shadow = extraction(
      "shadow.lua"_str, "local W = {}\nreturn W.I18n.tr('id', 'Text')\n"_str);
  checks.expect(shadow.is_err(),
                "reserved host globals should not be shadowed");

  auto malformed = extraction("broken.lua"_str, "return function(\n"_str);
  checks.expect(malformed.is_err(),
                "truncated Lua should fail syntax validation");

  auto invalid_escape =
      extraction("escape.lua"_str, "return W.I18n.tr('id', '\\q')\n"_str);
  checks.expect(invalid_escape.is_err(),
                "invalid Lua escapes should fail lexing");

  auto sources = Vec<luato::i18n::SourceFile>::make();
  sources.push(luato::i18n::SourceFile{
      rstd::parse::SourceId("first.lua"_str),
      "return W.I18n.tr('same', 'First')\n"_str.as_bytes()});
  sources.push(luato::i18n::SourceFile{
      rstd::parse::SourceId("second.lua"_str),
      "return W.I18n.tr('same', 'Second')\n"_str.as_bytes()});
  auto settings = options();
  auto conflict = luato::i18n::extract(sources.as_slice(), settings);
  checks.expect(conflict.is_err(),
                "different fallbacks for one id should fail");
  if (conflict.is_err()) {
    auto error = rstd::move(conflict).unwrap_err_unchecked();
    checks.expect(error.code == luato::i18n::DiagnosticCode::DuplicateFallback,
                  "fallback conflict should have a stable code");
    checks.expect(error.related.len() == usize(1),
                  "fallback conflict should point to the first source");
  }
}

void expect_no_execution(Checks &checks) {
  auto parsed =
      extraction("effects.lua"_str, "while true do end\n"
                                    "os.execute('touch should-not-exist')\n"
                                    "error('must not run')\n"
                                    "return W.I18n.tr('safe', 'Safe')\n"_str);
  checks.expect(parsed.is_ok(), "static extraction should not execute Lua");
}

void expect_catalog(Checks &checks) {
  auto extracted =
      extraction("plugin/main.lua"_str,
                 "return W.I18n.tr('account.label', 'Status')\n"_str);
  checks.expect(extracted.is_ok(), "catalog fixture should extract");
  if (extracted.is_err())
    return;
  auto messages = rstd::move(extracted).unwrap_unchecked();
  auto created =
      luato::i18n::update_catalog(rstd::parse::SourceId("i18n/zh-CN.json"_str),
                                  "zh-CN"_str, None(), messages);
  checks.expect(created.is_ok(), "catalog should be created");
  if (created.is_err())
    return;
  auto catalog = rstd::move(created).unwrap_unchecked();
  checks.expect(catalog.as_str().contains("\"version\": 1"_str),
                "catalog should declare schema version 1");
  checks.expect(catalog.as_str().contains("plugin/main.lua:1"_str),
                "catalog should own stable references");

  auto checked =
      luato::i18n::check_catalog(rstd::parse::SourceId("i18n/zh-CN.json"_str),
                                 "zh-CN"_str, catalog.as_str(), messages);
  checks.expect(checked.is_ok(), "fresh catalog should pass check");

  auto translation = catalog.as_str().find("\"translation\": \"\""_str);
  checks.expect(translation.is_some(),
                "new catalog should have an empty translation");
  if (translation.is_none())
    return;
  const usize value_begin = *translation;
  catalog.replace_range(value_begin,
                        value_begin + "\"translation\": \"\""_str.size(),
                        "\"translation\": \"状态\""_str);

  auto changed_source =
      extraction("plugin/main.lua"_str,
                 "return W.I18n.tr('account.label', 'State')\n"_str);
  checks.expect(changed_source.is_ok(),
                "changed fallback fixture should extract");
  if (changed_source.is_err())
    return;
  auto changed = rstd::move(changed_source).unwrap_unchecked();
  auto updated =
      luato::i18n::update_catalog(rstd::parse::SourceId("i18n/zh-CN.json"_str),
                                  "zh-CN"_str, Some(catalog.as_str()), changed);
  checks.expect(updated.is_ok(), "translated catalog should update");
  if (updated.is_err())
    return;
  checks.expect(updated->as_str().contains("状态"_str),
                "catalog update should preserve translation");
  checks.expect(updated->as_str().contains("\"needs_review\": true"_str),
                "fallback changes should require review");

  auto empty = extraction("plugin/empty.lua"_str, "return {}\n"_str);
  checks.expect(empty.is_ok(), "empty catalog fixture should parse");
  if (empty.is_err())
    return;
  auto obsolete =
      luato::i18n::update_catalog(rstd::parse::SourceId("i18n/zh-CN.json"_str),
                                  "zh-CN"_str, Some(updated->as_str()), *empty);
  checks.expect(obsolete.is_ok(), "removed messages should update catalog");
  if (obsolete.is_ok())
    checks.expect(obsolete->as_str().contains(
                      "\"obsolete\": {\n    \"account.label\""_str),
                  "removed messages should remain obsolete");

  auto wrong_locale = luato::i18n::parse_catalog(
      rstd::parse::SourceId("i18n/en.json"_str), "en"_str, updated->as_str());
  checks.expect(wrong_locale.is_err(), "catalog locale mismatch should fail");
}

} // namespace

auto expect_i18n_contract() -> int {
  Checks checks;
  expect_extraction(checks);
  expect_errors(checks);
  expect_no_execution(checks);
  expect_catalog(checks);
  return checks.failures;
}
