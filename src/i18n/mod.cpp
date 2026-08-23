module luato.i18n;

using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::collections::BTreeMap;
using ::alloc::string::String;
using ::alloc::vec::Vec;

namespace luato::i18n {

auto subtext(ref<str> text, usize begin, usize end) noexcept -> ref<str> {
  if (begin == end)
    return {};
  return ref<str>::from_raw_parts_unchecked(text.data() + begin.to_primitive(),
                                            end - begin);
}

auto source_position(slice<u8> bytes, usize offset) noexcept
    -> rstd::parse::SourcePosition {
  auto position = rstd::parse::SourcePosition{};
  for (usize index{}; index < offset && index < bytes.len(); ++index) {
    if (bytes[index] == u8('\n')) {
      ++position.line;
      position.column = usize(1);
    } else {
      ++position.column;
    }
  }
  return position;
}

auto diagnostic(DiagnosticCode code, const rstd::parse::SourceId &source,
                slice<u8> bytes, rstd::parse::Span span, ref<str> message)
    -> Diagnostic {
  return Diagnostic{code,
                    source.clone(),
                    span,
                    source_position(bytes, span.begin),
                    String::make(message),
                    Vec<RelatedLocation>::make()};
}

enum class TokenKind : rstd::uint8_t { Name, Number, String, Symbol, Eof };

struct Token {
  TokenKind kind;
  rstd::parse::Span span;
  Option<String> decoded;
  Option<String> leading_note;
};

auto hex_value(u8 byte) noexcept -> u8 {
  return *rstd::ascii::digit_value(byte, u8(16));
}

auto is_name_start(u8 byte) noexcept -> bool {
  return byte == u8('_') || rstd::ascii::is_alpha(byte);
}

auto is_name_continue(u8 byte) noexcept -> bool {
  return byte == u8('_') || rstd::ascii::is_alnum(byte);
}

auto trim_ascii(ref<str> value) noexcept -> ref<str> {
  usize begin{};
  usize end = value.size();
  while (begin < end && rstd::ascii::is_blank(value[begin]))
    ++begin;
  while (end > begin && rstd::ascii::is_blank(value[end - usize(1)]))
    --end;
  return subtext(value, begin, end);
}

class Lexer {
  const rstd::parse::SourceId &source_;
  ref<str> text_;
  rstd::parse::TextCursor cursor_;
  const ExtractionOptions &options_;
  Vec<Token> tokens_;
  Option<String> pending_note_;
  bool pending_note_has_token_{};
  Option<Diagnostic> error_;

  auto byte(usize ahead = usize()) const noexcept -> Option<u8> {
    auto value = cursor_.peek(ahead);
    if (value.is_none())
      return None();
    return Some(**value);
  }

  auto begins(ref<str> value) const noexcept -> bool {
    if (cursor_.remaining() < value.size())
      return false;
    for (usize index{}; index < value.size(); ++index) {
      if (*byte(index) != value[index])
        return false;
    }
    return true;
  }

  void fail(rstd::parse::Span span, ref<str> message) {
    if (error_.is_none())
      error_ = Some(diagnostic(DiagnosticCode::LexicalError, source_,
                               text_.as_bytes(), span, message));
  }

  auto long_level_at(usize offset) const noexcept -> Option<usize> {
    if (offset >= cursor_.len() || cursor_.input()[offset] != u8('['))
      return None();
    usize index = offset + usize(1);
    while (index < cursor_.len() && cursor_.input()[index] == u8('='))
      ++index;
    if (index >= cursor_.len() || cursor_.input()[index] != u8('['))
      return None();
    return Some(index - offset - usize(1));
  }

  auto close_long(usize level) const noexcept -> bool {
    if (byte() != Some(u8(']')))
      return false;
    for (usize index{}; index < level; ++index) {
      if (byte(index + usize(1)) != Some(u8('=')))
        return false;
    }
    return byte(level + usize(1)) == Some(u8(']'));
  }

  auto scan_long(usize level, bool decode) -> Option<String> {
    const usize delimiter = level + usize(2);
    cursor_.advance(delimiter);
    if (byte() == Some(u8('\r'))) {
      cursor_.advance(usize(1));
      if (byte() == Some(u8('\n')))
        cursor_.advance(usize(1));
    } else if (byte() == Some(u8('\n'))) {
      cursor_.advance(usize(1));
    }
    const usize content_begin = cursor_.position();
    while (!cursor_.is_eof() && !close_long(level))
      cursor_.advance(usize(1));
    if (cursor_.is_eof()) {
      fail({content_begin, cursor_.position()},
           "unterminated long string or comment"_str);
      return None();
    }
    const usize content_end = cursor_.position();
    cursor_.advance(delimiter);
    if (!decode)
      return Some(String::make());
    return Some(String::make(subtext(text_, content_begin, content_end)));
  }

  void remember_comment(ref<str> content) {
    auto trimmed = trim_ascii(content);
    const auto &prefix = options_.call.translator_comment_prefix;
    if (prefix.is_empty() || !trimmed.starts_with(prefix.as_str()))
      return;
    auto note = trim_ascii(subtext(trimmed, prefix.len(), trimmed.size()));
    if (note.is_empty())
      return;
    if (pending_note_.is_none()) {
      pending_note_ = Some(String::make(note));
      return;
    }
    pending_note_->push_ascii('\n');
    pending_note_->push_str(note);
  }

  auto skip_trivia() -> bool {
    while (!cursor_.is_eof()) {
      if (rstd::ascii::is_space(*byte())) {
        if ((*byte() == u8('\n') || *byte() == u8('\r')) &&
            pending_note_.is_some() && pending_note_has_token_) {
          pending_note_ = None();
          pending_note_has_token_ = false;
        }
        cursor_.advance(usize(1));
        continue;
      }
      if (!begins("--"_str))
        return true;
      cursor_.advance(usize(2));
      auto level = long_level_at(cursor_.position());
      if (level.is_some()) {
        const usize delimiter = *level + usize(2);
        cursor_.advance(delimiter);
        if (byte() == Some(u8('\r'))) {
          cursor_.advance(usize(1));
          if (byte() == Some(u8('\n')))
            cursor_.advance(usize(1));
        } else if (byte() == Some(u8('\n'))) {
          cursor_.advance(usize(1));
        }
        const usize begin = cursor_.position();
        while (!cursor_.is_eof() && !close_long(*level))
          cursor_.advance(usize(1));
        if (cursor_.is_eof()) {
          fail({begin, cursor_.position()}, "unterminated long comment"_str);
          return false;
        }
        remember_comment(subtext(text_, begin, cursor_.position()));
        cursor_.advance(*level + usize(2));
        continue;
      }
      const usize begin = cursor_.position();
      while (!cursor_.is_eof() && byte() != Some(u8('\n')) &&
             byte() != Some(u8('\r')))
        cursor_.advance(usize(1));
      remember_comment(subtext(text_, begin, cursor_.position()));
    }
    return true;
  }

  void emit(TokenKind kind, usize begin, Option<String> decoded = None()) {
    Option<String> leading_note;
    if (pending_note_.is_some() && kind == TokenKind::Name &&
        !options_.call.callee.is_empty() &&
        subtext(text_, begin, cursor_.position()) ==
            options_.call.callee[usize()].as_str()) {
      leading_note = rstd::move(pending_note_);
      pending_note_ = None();
      pending_note_has_token_ = false;
    } else if (pending_note_.is_some()) {
      pending_note_has_token_ = true;
    }
    tokens_.push(Token{kind,
                       {begin, cursor_.position()},
                       rstd::move(decoded),
                       rstd::move(leading_note)});
  }

  auto scan_short_string() -> bool {
    const usize begin = cursor_.position();
    const u8 quote = *byte();
    cursor_.advance(usize(1));
    auto output = Vec<u8>::make();
    while (!cursor_.is_eof()) {
      const u8 current = *byte();
      if (current == quote) {
        cursor_.advance(usize(1));
        auto decoded = String::from_utf8(rstd::move(output));
        if (decoded.is_err()) {
          fail({begin, cursor_.position()},
               "string literal does not decode to UTF-8"_str);
          return false;
        }
        emit(TokenKind::String, begin,
             Some(rstd::move(decoded).unwrap_unchecked()));
        return true;
      }
      if (current == u8('\n') || current == u8('\r')) {
        fail({begin, cursor_.position()}, "unfinished short string"_str);
        return false;
      }
      if (current < u8(0x20) && current != u8('\t')) {
        fail({cursor_.position(), cursor_.position() + usize(1)},
             "control byte in short string"_str);
        return false;
      }
      cursor_.advance(usize(1));
      if (current != u8('\\')) {
        output.push(u8(current));
        continue;
      }
      if (cursor_.is_eof()) {
        fail({begin, cursor_.position()}, "unfinished string escape"_str);
        return false;
      }
      u8 escaped = *byte();
      cursor_.advance(usize(1));
      switch (escaped.to_primitive()) {
      case 'a':
        output.push(u8(7));
        break;
      case 'b':
        output.push(u8(8));
        break;
      case 'f':
        output.push(u8(12));
        break;
      case 'n':
        output.push(u8('\n'));
        break;
      case 'r':
        output.push(u8('\r'));
        break;
      case 't':
        output.push(u8('\t'));
        break;
      case 'v':
        output.push(u8(11));
        break;
      case '\\':
        output.push(u8('\\'));
        break;
      case '"':
        output.push(u8('"'));
        break;
      case '\'':
        output.push(u8('\''));
        break;
      case '\n':
        output.push(u8('\n'));
        break;
      case '\r':
        if (byte() == Some(u8('\n')))
          cursor_.advance(usize(1));
        output.push(u8('\n'));
        break;
      case 'z':
        while (!cursor_.is_eof() && rstd::ascii::is_space(*byte()))
          cursor_.advance(usize(1));
        break;
      case 'x': {
        if (byte().is_none() || byte(usize(1)).is_none() ||
            !rstd::ascii::is_hex_digit(*byte()) ||
            !rstd::ascii::is_hex_digit(*byte(usize(1)))) {
          fail({begin, cursor_.position()}, "invalid hexadecimal escape"_str);
          return false;
        }
        output.push((hex_value(*byte()) << u64(4)) |
                    hex_value(*byte(usize(1))));
        cursor_.advance(usize(2));
        break;
      }
      case 'u': {
        if (byte() != Some(u8('{'))) {
          fail({begin, cursor_.position()}, "invalid Unicode escape"_str);
          return false;
        }
        cursor_.advance(usize(1));
        char32_t value{};
        usize digits{};
        while (byte().is_some() && rstd::ascii::is_hex_digit(*byte())) {
          if (digits == usize(8)) {
            fail({begin, cursor_.position()}, "Unicode escape is too long"_str);
            return false;
          }
          value = value * 16 + hex_value(*byte()).to_primitive();
          ++digits;
          cursor_.advance(usize(1));
        }
        if (digits == usize() || byte() != Some(u8('}')) ||
            value > char32_t(0x10ffff) ||
            (value >= char32_t(0xd800) && value <= char32_t(0xdfff))) {
          fail({begin, cursor_.position()}, "invalid Unicode escape"_str);
          return false;
        }
        cursor_.advance(usize(1));
        auto encoded = String::make();
        encoded.push(value);
        for (u8 item : encoded.as_str().as_bytes())
          output.push(u8(item));
        break;
      }
      default:
        if (rstd::ascii::is_digit(escaped)) {
          unsigned value = unsigned((escaped - u8('0')).to_primitive());
          usize digits = usize(1);
          while (digits < usize(3) && byte().is_some() &&
                 rstd::ascii::is_digit(*byte())) {
            value = value * 10U + unsigned((*byte() - u8('0')).to_primitive());
            ++digits;
            cursor_.advance(usize(1));
          }
          if (value > 255U) {
            fail({begin, cursor_.position()}, "decimal escape exceeds 255"_str);
            return false;
          }
          output.push(u8(value));
          break;
        }
        fail({begin, cursor_.position()}, "invalid string escape"_str);
        return false;
      }
    }
    fail({begin, cursor_.position()}, "unfinished short string"_str);
    return false;
  }

  auto scan_number() -> bool {
    const usize begin = cursor_.position();
    bool hex = false;
    if (byte() == Some(u8('0')) &&
        (byte(usize(1)) == Some(u8('x')) || byte(usize(1)) == Some(u8('X')))) {
      hex = true;
      cursor_.advance(usize(2));
    }
    usize digits{};
    while (byte().is_some() && (hex ? rstd::ascii::is_hex_digit(*byte())
                                    : rstd::ascii::is_digit(*byte()))) {
      ++digits;
      cursor_.advance(usize(1));
    }
    if (byte() == Some(u8('.')) && byte(usize(1)) != Some(u8('.'))) {
      cursor_.advance(usize(1));
      while (byte().is_some() && (hex ? rstd::ascii::is_hex_digit(*byte())
                                      : rstd::ascii::is_digit(*byte()))) {
        ++digits;
        cursor_.advance(usize(1));
      }
    }
    if (digits == usize()) {
      fail({begin, cursor_.position()}, "number requires digits"_str);
      return false;
    }
    const bool exponent =
        hex ? (byte() == Some(u8('p')) || byte() == Some(u8('P')))
            : (byte() == Some(u8('e')) || byte() == Some(u8('E')));
    if (exponent) {
      cursor_.advance(usize(1));
      if (byte() == Some(u8('+')) || byte() == Some(u8('-')))
        cursor_.advance(usize(1));
      usize exponent_digits{};
      while (byte().is_some() && rstd::ascii::is_digit(*byte())) {
        ++exponent_digits;
        cursor_.advance(usize(1));
      }
      if (exponent_digits == usize()) {
        fail({begin, cursor_.position()},
             "number exponent requires digits"_str);
        return false;
      }
    }
    if (byte().is_some() && is_name_continue(*byte())) {
      fail({begin, cursor_.position() + usize(1)}, "malformed number"_str);
      return false;
    }
    emit(TokenKind::Number, begin);
    return true;
  }

  auto scan_symbol() -> bool {
    const usize begin = cursor_.position();
    static constexpr auto triples = rstd::array<ref<str>, 1>{"..."_str};
    static constexpr auto pairs = rstd::array<ref<str>, 9>{
        ".."_str, "//"_str, "=="_str, "~="_str, "<="_str,
        ">="_str, "<<"_str, ">>"_str, "::"_str};
    for (auto value : triples) {
      if (begins(value)) {
        cursor_.advance(value.size());
        emit(TokenKind::Symbol, begin);
        return true;
      }
    }
    for (auto value : pairs) {
      if (begins(value)) {
        cursor_.advance(value.size());
        emit(TokenKind::Symbol, begin);
        return true;
      }
    }
    auto symbols = "+-*/%^#&~|<>=(){}[];:,."_str;
    if (!symbols.contains(subtext(text_, begin, begin + usize(1)))) {
      fail({begin, begin + usize(1)}, "unexpected byte in Lua source"_str);
      return false;
    }
    cursor_.advance(usize(1));
    emit(TokenKind::Symbol, begin);
    return true;
  }

public:
  Lexer(const rstd::parse::SourceId &source, ref<str> text,
        const ExtractionOptions &options)
      : source_(source), text_(text), cursor_(rstd::parse::text_input(text)),
        options_(options), tokens_(Vec<Token>::make()) {}

  auto run() -> Result<Vec<Token>> {
    while (!cursor_.is_eof()) {
      if (!skip_trivia())
        return Err(rstd::move(*error_));
      if (cursor_.is_eof())
        break;
      if (tokens_.len() >= options_.max_tokens) {
        auto span = rstd::parse::Span{cursor_.position(), cursor_.position()};
        return Err(diagnostic(DiagnosticCode::CapacityLimit, source_,
                              text_.as_bytes(), span,
                              "Lua token limit exceeded"_str));
      }
      const usize begin = cursor_.position();
      const u8 current = *byte();
      if (is_name_start(current)) {
        cursor_.advance(usize(1));
        while (byte().is_some() && is_name_continue(*byte()))
          cursor_.advance(usize(1));
        emit(TokenKind::Name, begin);
      } else if (rstd::ascii::is_digit(current) ||
                 (current == u8('.') && byte(usize(1)).is_some() &&
                  rstd::ascii::is_digit(*byte(usize(1))))) {
        if (!scan_number())
          return Err(rstd::move(*error_));
      } else if (current == u8('"') || current == u8('\'')) {
        if (!scan_short_string())
          return Err(rstd::move(*error_));
      } else {
        auto level = long_level_at(begin);
        if (level.is_some()) {
          auto decoded = scan_long(*level, true);
          if (decoded.is_none())
            return Err(rstd::move(*error_));
          emit(TokenKind::String, begin, rstd::move(decoded));
        } else if (!scan_symbol()) {
          return Err(rstd::move(*error_));
        }
      }
    }
    tokens_.push(Token{TokenKind::Eof,
                       {cursor_.position(), cursor_.position()},
                       None(),
                       rstd::move(pending_note_)});
    return Ok(rstd::move(tokens_));
  }
};

struct ExprInfo {
  rstd::parse::Span span{};
  Option<String> literal;
  Vec<String> qualified;
  Option<String> leading_note;
  bool has_qualified{};
  bool assignable{};
  bool is_call{};
};

struct FoundCall {
  String id;
  String fallback;
  Occurrence occurrence;
};

struct DepthScope {
  usize &depth;
  ~DepthScope() { --depth; }
};

class Parser {
  const rstd::parse::SourceId &source_;
  ref<str> text_;
  const ExtractionOptions &options_;
  rstd::parse::Cursor<Token> cursor_;
  usize depth_{};
  Option<Diagnostic> error_;
  Vec<FoundCall> calls_;

  auto token(usize ahead = usize()) const noexcept -> const Token & {
    auto value = cursor_.peek(ahead);
    if (value.is_some())
      return **value;
    return cursor_.input()[cursor_.len() - usize(1)];
  }

  auto token_text(const Token &value) const noexcept -> ref<str> {
    return subtext(text_, value.span.begin, value.span.end);
  }

  auto at(ref<str> value) const noexcept -> bool {
    return token_text(token()) == value;
  }

  auto is_keyword(ref<str> value) const noexcept -> bool {
    static constexpr auto keywords = rstd::array<ref<str>, 22>{
        "and"_str,   "break"_str,  "do"_str,     "else"_str,     "elseif"_str,
        "end"_str,   "false"_str,  "for"_str,    "function"_str, "goto"_str,
        "if"_str,    "in"_str,     "local"_str,  "nil"_str,      "not"_str,
        "or"_str,    "repeat"_str, "return"_str, "then"_str,     "true"_str,
        "until"_str, "while"_str};
    for (auto keyword : keywords) {
      if (value == keyword)
        return true;
    }
    return false;
  }

  auto is_name_token(const Token &value) const noexcept -> bool {
    return value.kind == TokenKind::Name && !is_keyword(token_text(value));
  }

  auto take() noexcept -> const Token & {
    const Token &value = token();
    cursor_.advance(usize(1));
    return value;
  }

  auto consume(ref<str> value) noexcept -> bool {
    if (!at(value))
      return false;
    cursor_.advance(usize(1));
    return true;
  }

  void fail(DiagnosticCode code, rstd::parse::Span span, ref<str> message) {
    if (error_.is_none())
      error_ = Some(diagnostic(code, source_, text_.as_bytes(), span, message));
  }

  auto expected(ref<str> value) -> bool {
    fail(DiagnosticCode::SyntaxError, token().span,
         rstd::format("expected {}", value).as_str());
    return false;
  }

  auto expect(ref<str> value) -> bool {
    return consume(value) || expected(value);
  }

  auto expect_name(const Token *&output) -> bool {
    if (!is_name_token(token())) {
      fail(DiagnosticCode::SyntaxError, token().span,
           "expected a Lua name"_str);
      return false;
    }
    output = rstd::addressof(take());
    return true;
  }

  auto enter() -> bool {
    if (depth_ >= options_.max_nesting) {
      fail(DiagnosticCode::CapacityLimit, token().span,
           "Lua nesting limit exceeded"_str);
      return false;
    }
    ++depth_;
    return true;
  }

  auto is_reserved(ref<str> name) const noexcept -> bool {
    return options_.call.reserved_global.is_some() &&
           options_.call.reserved_global->as_str() == name;
  }

  auto reject_reserved(const Token &name) -> bool {
    if (!is_reserved(token_text(name)))
      return true;
    fail(DiagnosticCode::InvalidTranslationCall, name.span,
         "reserved host global cannot be shadowed"_str);
    return false;
  }

  auto same_callee(const ExprInfo &expression) const noexcept -> bool {
    if (!expression.has_qualified ||
        expression.qualified.len() != options_.call.callee.len())
      return false;
    for (usize index{}; index < expression.qualified.len(); ++index) {
      if (expression.qualified[index].as_str() !=
          options_.call.callee[index].as_str())
        return false;
    }
    return true;
  }

  auto parse_expression_list(Vec<ExprInfo> &values) -> bool {
    ExprInfo first;
    if (!parse_expression(first))
      return false;
    values.push(rstd::move(first));
    while (consume(","_str)) {
      ExprInfo next;
      if (!parse_expression(next))
        return false;
      values.push(rstd::move(next));
    }
    return true;
  }

  auto parse_table(ExprInfo &output) -> bool {
    const usize begin = token().span.begin;
    if (!expect("{"_str))
      return false;
    while (!at("}"_str)) {
      if (token().kind == TokenKind::Eof)
        return expected("}"_str);
      if (consume("["_str)) {
        ExprInfo key;
        if (!parse_expression(key) || !expect("]"_str) || !expect("="_str))
          return false;
        ExprInfo value;
        if (!parse_expression(value))
          return false;
      } else if (is_name_token(token()) &&
                 token_text(token(usize(1))) == "="_str) {
        take();
        take();
        ExprInfo value;
        if (!parse_expression(value))
          return false;
      } else {
        ExprInfo value;
        if (!parse_expression(value))
          return false;
      }
      if (!consume(","_str) && !consume(";"_str))
        break;
    }
    if (!expect("}"_str))
      return false;
    output.span = {begin,
                   cursor_.input()[cursor_.position() - usize(1)].span.end};
    return true;
  }

  auto parse_function_body(ExprInfo &output) -> bool {
    const usize begin = token().span.begin;
    if (!expect("("_str))
      return false;
    if (!consume(")"_str)) {
      if (consume("..."_str)) {
        if (!expect(")"_str))
          return false;
      } else {
        while (true) {
          const Token *name{};
          if (!expect_name(name) || !reject_reserved(*name))
            return false;
          if (!consume(","_str))
            break;
          if (consume("..."_str))
            break;
        }
        if (!expect(")"_str))
          return false;
      }
    }
    if (!parse_block())
      return false;
    if (!expect("end"_str))
      return false;
    output.span = {begin,
                   cursor_.input()[cursor_.position() - usize(1)].span.end};
    return true;
  }

  auto record_call(const ExprInfo &callee, const Vec<ExprInfo> &arguments,
                   usize end) -> bool {
    if (!same_callee(callee))
      return true;
    if (arguments.len() != options_.call.exact_argument_count ||
        options_.call.id_argument >= arguments.len() ||
        options_.call.fallback_argument >= arguments.len() ||
        arguments[options_.call.id_argument].literal.is_none() ||
        arguments[options_.call.fallback_argument].literal.is_none()) {
      fail(
          DiagnosticCode::InvalidTranslationCall, {callee.span.begin, end},
          "canonical translation call requires exactly two string literals"_str);
      return false;
    }
    const auto &id = arguments[options_.call.id_argument];
    const auto &fallback = arguments[options_.call.fallback_argument];
    if (id.literal->is_empty()) {
      fail(DiagnosticCode::EmptyMessageId, id.span,
           "translation message id cannot be empty"_str);
      return false;
    }
    calls_.push(FoundCall{
        id.literal->clone(), fallback.literal->clone(),
        Occurrence{source_.clone(),
                   {callee.span.begin, end},
                   id.span,
                   fallback.span,
                   source_position(text_.as_bytes(), callee.span.begin),
                   callee.leading_note.is_some()
                       ? Some(callee.leading_note->clone())
                       : None()}});
    return true;
  }

  auto parse_arguments(const ExprInfo &callee, usize &end) -> bool {
    auto arguments = Vec<ExprInfo>::make();
    if (consume("("_str)) {
      if (!consume(")"_str)) {
        if (!parse_expression_list(arguments) || !expect(")"_str))
          return false;
      }
      end = cursor_.input()[cursor_.position() - usize(1)].span.end;
    } else if (at("{"_str)) {
      ExprInfo table;
      if (!parse_table(table))
        return false;
      end = table.span.end;
      arguments.push(rstd::move(table));
    } else if (token().kind == TokenKind::String) {
      const Token &literal = take();
      ExprInfo value;
      value.span = literal.span;
      value.literal = Some(literal.decoded->clone());
      end = literal.span.end;
      arguments.push(rstd::move(value));
    } else {
      return expected("function arguments"_str);
    }
    return record_call(callee, arguments, end);
  }

  auto parse_prefix_expression(ExprInfo &output) -> bool {
    if (is_name_token(token())) {
      const Token &name = take();
      output.span = name.span;
      output.qualified.push(String::make(token_text(name)));
      output.leading_note = name.leading_note.is_some()
                                ? Some(name.leading_note->clone())
                                : None();
      output.has_qualified = true;
      output.assignable = true;
    } else if (consume("("_str)) {
      const usize begin =
          cursor_.input()[cursor_.position() - usize(1)].span.begin;
      if (!parse_expression(output) || !expect(")"_str))
        return false;
      output.span = {begin,
                     cursor_.input()[cursor_.position() - usize(1)].span.end};
      output.literal = None();
      output.qualified.clear();
      output.leading_note = None();
      output.has_qualified = false;
      output.assignable = false;
    } else {
      return expected("prefix expression"_str);
    }

    while (true) {
      if (consume("."_str)) {
        const Token *name{};
        if (!expect_name(name))
          return false;
        output.span.end = name->span.end;
        if (output.has_qualified)
          output.qualified.push(String::make(token_text(*name)));
        output.assignable = true;
      } else if (consume("["_str)) {
        ExprInfo index;
        if (!parse_expression(index) || !expect("]"_str))
          return false;
        output.span.end =
            cursor_.input()[cursor_.position() - usize(1)].span.end;
        output.qualified.clear();
        output.leading_note = None();
        output.has_qualified = false;
        output.assignable = true;
      } else if (consume(":"_str)) {
        const Token *name{};
        if (!expect_name(name))
          return false;
        usize end{};
        ExprInfo method_callee;
        method_callee.span = {output.span.begin, name->span.end};
        if (!parse_arguments(method_callee, end))
          return false;
        output.span.end = end;
        output.qualified.clear();
        output.leading_note = None();
        output.has_qualified = false;
        output.assignable = false;
        output.is_call = true;
      } else if (at("("_str) || at("{"_str) ||
                 token().kind == TokenKind::String) {
        usize end{};
        if (!parse_arguments(output, end))
          return false;
        output.span.end = end;
        output.qualified.clear();
        output.leading_note = None();
        output.has_qualified = false;
        output.assignable = false;
        output.is_call = true;
      } else {
        break;
      }
    }
    return true;
  }

  auto parse_simple_expression(ExprInfo &output) -> bool {
    const Token &current = token();
    if (current.kind == TokenKind::String) {
      take();
      output.span = current.span;
      output.literal = Some(current.decoded->clone());
      return true;
    }
    if (current.kind == TokenKind::Number || at("nil"_str) || at("false"_str) ||
        at("true"_str) || at("..."_str)) {
      take();
      output.span = current.span;
      return true;
    }
    if (consume("function"_str))
      return parse_function_body(output);
    if (at("{"_str))
      return parse_table(output);
    return parse_prefix_expression(output);
  }

  auto binary_precedence(ref<str> operation, bool &right) const noexcept
      -> unsigned {
    right = false;
    if (operation == "or"_str)
      return 1;
    if (operation == "and"_str)
      return 2;
    if (operation == "<"_str || operation == ">"_str || operation == "<="_str ||
        operation == ">="_str || operation == "~="_str || operation == "=="_str)
      return 3;
    if (operation == "|"_str)
      return 4;
    if (operation == "~"_str)
      return 5;
    if (operation == "&"_str)
      return 6;
    if (operation == "<<"_str || operation == ">>"_str)
      return 7;
    if (operation == ".."_str) {
      right = true;
      return 8;
    }
    if (operation == "+"_str || operation == "-"_str)
      return 9;
    if (operation == "*"_str || operation == "/"_str || operation == "//"_str ||
        operation == "%"_str)
      return 10;
    if (operation == "^"_str) {
      right = true;
      return 12;
    }
    return 0;
  }

  auto parse_subexpression(ExprInfo &output, unsigned limit) -> bool {
    if (!enter())
      return false;
    DepthScope scope{depth_};
    if (at("not"_str) || at("#"_str) || at("-"_str) || at("~"_str)) {
      const usize begin = take().span.begin;
      if (!parse_subexpression(output, 11))
        return false;
      output.span.begin = begin;
      output.literal = None();
      output.qualified.clear();
      output.leading_note = None();
      output.has_qualified = false;
      output.assignable = false;
    } else if (!parse_simple_expression(output)) {
      return false;
    }
    while (true) {
      bool right{};
      const unsigned precedence = binary_precedence(token_text(token()), right);
      if (precedence == 0 || precedence <= limit)
        break;
      take();
      ExprInfo rhs;
      if (!parse_subexpression(rhs, right ? precedence - 1 : precedence))
        return false;
      output.span.end = rhs.span.end;
      output.literal = None();
      output.qualified.clear();
      output.leading_note = None();
      output.has_qualified = false;
      output.assignable = false;
      output.is_call = false;
    }
    return true;
  }

  auto parse_expression(ExprInfo &output) -> bool {
    return parse_subexpression(output, 0);
  }

  auto parse_name_list(bool reject_host) -> bool {
    while (true) {
      const Token *name{};
      if (!expect_name(name) || (reject_host && !reject_reserved(*name)))
        return false;
      if (!consume(","_str))
        return true;
    }
  }

  auto parse_local() -> bool {
    if (consume("function"_str)) {
      const Token *name{};
      if (!expect_name(name) || !reject_reserved(*name))
        return false;
      ExprInfo body;
      return parse_function_body(body);
    }
    while (true) {
      const Token *name{};
      if (!expect_name(name) || !reject_reserved(*name))
        return false;
      if (consume("<"_str)) {
        const Token *attribute{};
        if (!expect_name(attribute) ||
            (token_text(*attribute) != "const"_str &&
             token_text(*attribute) != "close"_str)) {
          fail(DiagnosticCode::SyntaxError,
               attribute ? attribute->span : token().span,
               "expected const or close local attribute"_str);
          return false;
        }
        if (!expect(">"_str))
          return false;
      }
      if (!consume(","_str))
        break;
    }
    if (consume("="_str)) {
      auto values = Vec<ExprInfo>::make();
      return parse_expression_list(values);
    }
    return true;
  }

  auto parse_for() -> bool {
    const Token *first{};
    if (!expect_name(first) || !reject_reserved(*first))
      return false;
    if (consume("="_str)) {
      ExprInfo begin;
      ExprInfo limit;
      if (!parse_expression(begin) || !expect(","_str) ||
          !parse_expression(limit))
        return false;
      if (consume(","_str)) {
        ExprInfo step;
        if (!parse_expression(step))
          return false;
      }
    } else {
      while (consume(","_str)) {
        const Token *name{};
        if (!expect_name(name) || !reject_reserved(*name))
          return false;
      }
      if (!expect("in"_str))
        return false;
      auto values = Vec<ExprInfo>::make();
      if (!parse_expression_list(values))
        return false;
    }
    if (!expect("do"_str) || !parse_block() || !expect("end"_str))
      return false;
    return true;
  }

  auto parse_if() -> bool {
    ExprInfo condition;
    if (!parse_expression(condition) || !expect("then"_str) || !parse_block())
      return false;
    while (consume("elseif"_str)) {
      ExprInfo branch;
      if (!parse_expression(branch) || !expect("then"_str) || !parse_block())
        return false;
    }
    if (consume("else"_str) && !parse_block())
      return false;
    return expect("end"_str);
  }

  auto parse_function_statement() -> bool {
    const Token *name{};
    if (!expect_name(name))
      return false;
    if (is_reserved(token_text(*name))) {
      fail(DiagnosticCode::InvalidTranslationCall, name->span,
           "reserved host global cannot be assigned"_str);
      return false;
    }
    while (consume("."_str)) {
      if (!expect_name(name))
        return false;
    }
    if (consume(":"_str) && !expect_name(name))
      return false;
    ExprInfo body;
    return parse_function_body(body);
  }

  auto parse_assignment_or_call() -> bool {
    ExprInfo first;
    if (!parse_prefix_expression(first))
      return false;
    if (!at("="_str) && !at(","_str)) {
      if (!first.is_call) {
        fail(DiagnosticCode::SyntaxError, first.span,
             "expression statement must be a function call"_str);
        return false;
      }
      return true;
    }
    if (!first.assignable) {
      fail(DiagnosticCode::SyntaxError, first.span,
           "assignment target is not a variable"_str);
      return false;
    }
    if (first.has_qualified && first.qualified.len() == usize(1) &&
        is_reserved(first.qualified[usize()].as_str())) {
      fail(DiagnosticCode::InvalidTranslationCall, first.span,
           "reserved host global cannot be assigned"_str);
      return false;
    }
    while (consume(","_str)) {
      ExprInfo next;
      if (!parse_prefix_expression(next) || !next.assignable) {
        if (error_.is_none())
          fail(DiagnosticCode::SyntaxError, next.span,
               "assignment target is not a variable"_str);
        return false;
      }
      if (next.has_qualified && next.qualified.len() == usize(1) &&
          is_reserved(next.qualified[usize()].as_str())) {
        fail(DiagnosticCode::InvalidTranslationCall, next.span,
             "reserved host global cannot be assigned"_str);
        return false;
      }
    }
    if (!expect("="_str))
      return false;
    auto values = Vec<ExprInfo>::make();
    return parse_expression_list(values);
  }

  auto parse_statement() -> bool {
    if (consume(";"_str) || consume("break"_str))
      return true;
    if (consume("goto"_str)) {
      const Token *name{};
      return expect_name(name);
    }
    if (consume("::"_str)) {
      const Token *name{};
      return expect_name(name) && expect("::"_str);
    }
    if (consume("do"_str))
      return parse_block() && expect("end"_str);
    if (consume("while"_str)) {
      ExprInfo condition;
      return parse_expression(condition) && expect("do"_str) && parse_block() &&
             expect("end"_str);
    }
    if (consume("repeat"_str)) {
      if (!parse_block() || !expect("until"_str))
        return false;
      ExprInfo condition;
      return parse_expression(condition);
    }
    if (consume("if"_str))
      return parse_if();
    if (consume("for"_str))
      return parse_for();
    if (consume("function"_str))
      return parse_function_statement();
    if (consume("local"_str))
      return parse_local();
    return parse_assignment_or_call();
  }

  auto at_block_end() const noexcept -> bool {
    return token().kind == TokenKind::Eof || at("end"_str) || at("else"_str) ||
           at("elseif"_str) || at("until"_str);
  }

  auto parse_block() -> bool {
    if (!enter())
      return false;
    DepthScope scope{depth_};
    while (!at_block_end()) {
      if (consume("return"_str)) {
        if (!at_block_end() && !at(";"_str)) {
          auto values = Vec<ExprInfo>::make();
          if (!parse_expression_list(values))
            return false;
        }
        consume(";"_str);
        if (!at_block_end()) {
          fail(DiagnosticCode::SyntaxError, token().span,
               "return must be the final statement in a block"_str);
          return false;
        }
        return true;
      }
      if (!parse_statement())
        return false;
    }
    return true;
  }

public:
  Parser(const rstd::parse::SourceId &source, ref<str> text,
         slice<Token> tokens, const ExtractionOptions &options)
      : source_(source), text_(text), options_(options),
        cursor_(rstd::parse::Input<Token>(tokens)),
        calls_(Vec<FoundCall>::make()) {}

  auto run() -> Result<Vec<FoundCall>> {
    if (!parse_block())
      return Err(rstd::move(*error_));
    if (token().kind != TokenKind::Eof) {
      fail(DiagnosticCode::SyntaxError, token().span,
           "unexpected block terminator"_str);
      return Err(rstd::move(*error_));
    }
    return Ok(rstd::move(calls_));
  }
};

auto contains_note(const Vec<String> &notes, ref<str> value) noexcept -> bool {
  for (const auto &note : notes) {
    if (note.as_str() == value)
      return true;
  }
  return false;
}

auto extract(slice<SourceFile> sources, const ExtractionOptions &options)
    -> Result<Extraction> {
  auto messages = BTreeMap<String, Message>::make();
  if (options.call.callee.is_empty()) {
    auto empty_source = rstd::parse::SourceId("<configuration>"_str);
    return Err(diagnostic(DiagnosticCode::InvalidTranslationCall, empty_source,
                          {}, {}, "translation callee cannot be empty"_str));
  }
  for (const auto &source : sources) {
    if (source.bytes.len() > options.max_file_bytes) {
      return Err(diagnostic(DiagnosticCode::CapacityLimit, source.source,
                            source.bytes, {usize(), source.bytes.len()},
                            "Lua source file size limit exceeded"_str));
    }
    auto validated = rstd::str_::from_utf8(source.bytes);
    if (validated.is_err()) {
      return Err(diagnostic(DiagnosticCode::InvalidUtf8, source.source,
                            source.bytes, {usize(), source.bytes.len()},
                            "Lua source is not valid UTF-8"_str));
    }
    const ref<str> text = rstd::move(validated).unwrap_unchecked();
    auto lexed = Lexer(source.source, text, options).run();
    if (lexed.is_err())
      return Err(rstd::move(lexed).unwrap_err_unchecked());
    auto tokens = rstd::move(lexed).unwrap_unchecked();
    auto parsed = Parser(source.source, text, tokens.as_slice(), options).run();
    if (parsed.is_err())
      return Err(rstd::move(parsed).unwrap_err_unchecked());
    auto calls = rstd::move(parsed).unwrap_unchecked();
    for (auto &call : calls) {
      auto existing = messages.get_mut(call.id.as_str());
      if (existing.is_some()) {
        if ((*existing)->fallback.as_str() != call.fallback.as_str()) {
          auto conflict =
              diagnostic(DiagnosticCode::DuplicateFallback, source.source,
                         source.bytes, call.occurrence.fallback_span,
                         "message id is used with a different fallback"_str);
          const auto &original = (*existing)->occurrences[usize()];
          conflict.related.push(RelatedLocation{
              original.source.clone(), original.fallback_span,
              original.position,
              String::make("first fallback for this message id"_str)});
          return Err(rstd::move(conflict));
        }
        if (call.occurrence.translator_note.is_some() &&
            !contains_note((*existing)->translator_notes,
                           call.occurrence.translator_note->as_str())) {
          (*existing)->translator_notes.push(
              call.occurrence.translator_note->clone());
        }
        (*existing)->occurrences.push(rstd::move(call.occurrence));
        continue;
      }
      auto notes = Vec<String>::make();
      if (call.occurrence.translator_note.is_some())
        notes.push(call.occurrence.translator_note->clone());
      auto id_key = call.id.clone();
      auto occurrences = Vec<Occurrence>::make();
      occurrences.push(rstd::move(call.occurrence));
      messages.insert(rstd::move(id_key),
                      Message{rstd::move(call.id), rstd::move(call.fallback),
                              rstd::move(occurrences), rstd::move(notes)});
    }
  }

  auto output = Vec<Message>::with_capacity(messages.len());
  while (auto entry = messages.pop_first())
    output.push(rstd::move(entry->template get<1>()));
  return Ok(Extraction{rstd::move(output), Vec<Diagnostic>::make()});
}

auto catalog_error(DiagnosticCode code, const rstd::parse::SourceId &source,
                   ref<str> document, ref<str> message,
                   rstd::parse::SourcePosition position = {}) -> Diagnostic {
  usize offset{};
  auto current = rstd::parse::SourcePosition{};
  while (offset < document.size() &&
         (current.line < position.line || current.column < position.column)) {
    if (document[offset] == u8('\n')) {
      ++current.line;
      current.column = usize(1);
    } else {
      ++current.column;
    }
    ++offset;
  }
  return diagnostic(code, source, document.as_bytes(), {offset, offset},
                    message);
}

auto valid_locale(ref<str> locale) noexcept -> bool {
  if (locale.is_empty() || locale[usize()] == u8('-') ||
      locale[locale.size() - usize(1)] == u8('-'))
    return false;
  bool previous_hyphen{};
  for (u8 byte : locale.as_bytes()) {
    if (byte == u8('-')) {
      if (previous_hyphen)
        return false;
      previous_hyphen = true;
      continue;
    }
    previous_hyphen = false;
    if (!is_name_continue(byte) || byte == u8('_'))
      return false;
  }
  return true;
}

auto known_field(ref<str> name, slice<ref<str>> allowed) noexcept -> bool {
  for (auto field : allowed) {
    if (field == name)
      return true;
  }
  return false;
}

auto reject_unknown(const rstd::json::Map &object, slice<ref<str>> allowed,
                    const rstd::parse::SourceId &source, ref<str> document,
                    ref<str> context) -> Result<empty> {
  for (auto item : object.iter()) {
    auto [key, value] = item;
    (void)value;
    if (!known_field(key->as_str(), allowed)) {
      return Err(catalog_error(
          DiagnosticCode::InvalidCatalog, source, document,
          rstd::format("unknown field '{}' in {}", key->as_str(), context)
              .as_str()));
    }
  }
  return Ok(empty{});
}

auto required_value(const rstd::json::Map &object, ref<str> key,
                    const rstd::parse::SourceId &source, ref<str> document,
                    ref<str> context) -> Result<ref<rstd::json::Value>> {
  auto value = object.get(key);
  if (value.is_none()) {
    return Err(catalog_error(
        DiagnosticCode::InvalidCatalog, source, document,
        rstd::format("missing field '{}.{}'", context, key).as_str()));
  }
  return Ok(*value);
}

auto required_string(const rstd::json::Map &object, ref<str> key,
                     const rstd::parse::SourceId &source, ref<str> document,
                     ref<str> context) -> Result<String> {
  auto value = required_value(object, key, source, document, context);
  if (value.is_err())
    return Err(rstd::move(value).unwrap_err_unchecked());
  auto string = (*value)->as_str();
  if (string.is_none()) {
    return Err(catalog_error(
        DiagnosticCode::InvalidCatalog, source, document,
        rstd::format("field '{}.{}' must be a string", context, key).as_str()));
  }
  return Ok(String::make(*string));
}

auto parse_entry(const rstd::parse::SourceId &source, ref<str> document,
                 ref<str> id, const rstd::json::Value &value)
    -> Result<CatalogEntry> {
  auto object = value.as_object();
  if (object.is_none()) {
    return Err(catalog_error(
        DiagnosticCode::InvalidCatalog, source, document,
        rstd::format("message '{}' must be an object", id).as_str()));
  }
  static constexpr auto allowed = rstd::array<ref<str>, 5>{
      "source"_str, "translation"_str, "references"_str, "note"_str,
      "needs_review"_str};
  auto checked = reject_unknown(**object, allowed.as_slice(), source, document,
                                "message entry"_str);
  if (checked.is_err())
    return Err(rstd::move(checked).unwrap_err_unchecked());
  auto fallback = required_string(**object, "source"_str, source, document, id);
  if (fallback.is_err())
    return Err(rstd::move(fallback).unwrap_err_unchecked());
  auto translation =
      required_string(**object, "translation"_str, source, document, id);
  if (translation.is_err())
    return Err(rstd::move(translation).unwrap_err_unchecked());
  auto references_value =
      required_value(**object, "references"_str, source, document, id);
  if (references_value.is_err())
    return Err(rstd::move(references_value).unwrap_err_unchecked());
  auto references_array = (*references_value)->as_array();
  if (references_array.is_none()) {
    return Err(catalog_error(
        DiagnosticCode::InvalidCatalog, source, document,
        rstd::format("field '{}.references' must be an array", id).as_str()));
  }
  auto references = Vec<String>::make();
  for (const auto &item : **references_array) {
    auto reference = item.as_str();
    if (reference.is_none()) {
      return Err(catalog_error(
          DiagnosticCode::InvalidCatalog, source, document,
          rstd::format("field '{}.references' must contain strings", id)
              .as_str()));
    }
    references.push(String::make(*reference));
  }
  Option<String> note;
  auto note_value = (**object).get("note"_str);
  if (note_value.is_some()) {
    auto note_text = (*note_value)->as_str();
    if (note_text.is_none()) {
      return Err(catalog_error(
          DiagnosticCode::InvalidCatalog, source, document,
          rstd::format("field '{}.note' must be a string", id).as_str()));
    }
    note = Some(String::make(*note_text));
  }
  bool needs_review{};
  auto review_value = (**object).get("needs_review"_str);
  if (review_value.is_some()) {
    auto review = (*review_value)->as_bool();
    if (review.is_none()) {
      return Err(catalog_error(
          DiagnosticCode::InvalidCatalog, source, document,
          rstd::format("field '{}.needs_review' must be a boolean", id)
              .as_str()));
    }
    needs_review = *review;
  }
  return Ok(CatalogEntry{rstd::move(fallback).unwrap_unchecked(),
                         rstd::move(translation).unwrap_unchecked(),
                         rstd::move(references), rstd::move(note),
                         needs_review});
}

auto parse_entry_map(const rstd::parse::SourceId &source, ref<str> document,
                     const rstd::json::Value &value, ref<str> field)
    -> Result<BTreeMap<String, CatalogEntry>> {
  auto object = value.as_object();
  if (object.is_none()) {
    return Err(catalog_error(
        DiagnosticCode::InvalidCatalog, source, document,
        rstd::format("field '{}' must be an object", field).as_str()));
  }
  auto entries = BTreeMap<String, CatalogEntry>::make();
  for (auto item : (**object).iter()) {
    auto [id, entry_value] = item;
    if (id->is_empty()) {
      return Err(catalog_error(DiagnosticCode::InvalidCatalog, source, document,
                               "catalog message id cannot be empty"_str));
    }
    auto entry = parse_entry(source, document, id->as_str(), *entry_value);
    if (entry.is_err())
      return Err(rstd::move(entry).unwrap_err_unchecked());
    entries.insert(id->clone(), rstd::move(entry).unwrap_unchecked());
  }
  return Ok(rstd::move(entries));
}

auto parse_catalog(rstd::parse::SourceId source, ref<str> expected_locale,
                   ref<str> document) -> Result<Catalog> {
  if (!valid_locale(expected_locale)) {
    return Err(catalog_error(DiagnosticCode::LocaleMismatch, source, document,
                             "invalid locale tag"_str));
  }
  auto parsed = rstd::json::from_str(
      document, rstd::json::ParseOptions{.reject_duplicate_keys = true});
  if (parsed.is_err()) {
    auto error = rstd::move(parsed).unwrap_err_unchecked();
    return Err(
        catalog_error(DiagnosticCode::InvalidCatalog, source, document,
                      rstd::format("invalid catalog JSON: {}", error).as_str(),
                      {error.line(), error.column()}));
  }
  auto root = rstd::move(parsed).unwrap_unchecked();
  auto object = root.as_object();
  if (object.is_none()) {
    return Err(catalog_error(DiagnosticCode::InvalidCatalog, source, document,
                             "catalog root must be an object"_str));
  }
  static constexpr auto allowed = rstd::array<ref<str>, 4>{
      "version"_str, "locale"_str, "messages"_str, "obsolete"_str};
  auto checked = reject_unknown(**object, allowed.as_slice(), source, document,
                                "catalog"_str);
  if (checked.is_err())
    return Err(rstd::move(checked).unwrap_err_unchecked());
  auto version_value =
      required_value(**object, "version"_str, source, document, "catalog"_str);
  if (version_value.is_err())
    return Err(rstd::move(version_value).unwrap_err_unchecked());
  if ((*version_value)->as_u64() != Some(u64(1))) {
    return Err(catalog_error(DiagnosticCode::InvalidCatalog, source, document,
                             "catalog version must be 1"_str));
  }
  auto locale =
      required_string(**object, "locale"_str, source, document, "catalog"_str);
  if (locale.is_err())
    return Err(rstd::move(locale).unwrap_err_unchecked());
  if (locale->as_str() != expected_locale) {
    return Err(
        catalog_error(DiagnosticCode::LocaleMismatch, source, document,
                      rstd::format("catalog locale '{}' does not match '{}'",
                                   locale->as_str(), expected_locale)
                          .as_str()));
  }
  auto messages_value =
      required_value(**object, "messages"_str, source, document, "catalog"_str);
  if (messages_value.is_err())
    return Err(rstd::move(messages_value).unwrap_err_unchecked());
  auto messages =
      parse_entry_map(source, document, **messages_value, "messages"_str);
  if (messages.is_err())
    return Err(rstd::move(messages).unwrap_err_unchecked());
  auto obsolete_value =
      required_value(**object, "obsolete"_str, source, document, "catalog"_str);
  if (obsolete_value.is_err())
    return Err(rstd::move(obsolete_value).unwrap_err_unchecked());
  auto obsolete =
      parse_entry_map(source, document, **obsolete_value, "obsolete"_str);
  if (obsolete.is_err())
    return Err(rstd::move(obsolete).unwrap_err_unchecked());
  return Ok(Catalog{rstd::move(locale).unwrap_unchecked(),
                    rstd::move(messages).unwrap_unchecked(),
                    rstd::move(obsolete).unwrap_unchecked()});
}

auto entry_value(const CatalogEntry &entry) -> rstd::json::Value {
  auto object = rstd::json::Map::make();
  object.insert(String::make("source"_str),
                rstd::json::Value::String(entry.source.clone()));
  object.insert(String::make("translation"_str),
                rstd::json::Value::String(entry.translation.clone()));
  auto references = rstd::json::Array::with_capacity(entry.references.len());
  for (const auto &reference : entry.references)
    references.push(rstd::json::Value::String(reference.clone()));
  object.insert(String::make("references"_str),
                rstd::json::Value::Array(rstd::move(references)));
  if (entry.note.is_some()) {
    object.insert(String::make("note"_str),
                  rstd::json::Value::String(entry.note->clone()));
  }
  if (entry.needs_review) {
    object.insert(String::make("needs_review"_str),
                  rstd::json::Value::Bool(true));
  }
  return rstd::json::Value::Object(rstd::move(object));
}

auto entry_map_value(const BTreeMap<String, CatalogEntry> &entries)
    -> rstd::json::Value {
  auto object = rstd::json::Map::make();
  for (auto item : entries.iter()) {
    auto [id, entry] = item;
    object.insert(id->clone(), entry_value(*entry));
  }
  return rstd::json::Value::Object(rstd::move(object));
}

auto render_catalog(const Catalog &catalog) -> String {
  auto object = rstd::json::Map::make();
  object.insert(
      String::make("version"_str),
      rstd::json::Value::Number(rstd::json::Number::from_u64(u64(1))));
  object.insert(String::make("locale"_str),
                rstd::json::Value::String(catalog.locale.clone()));
  object.insert(String::make("messages"_str),
                entry_map_value(catalog.messages));
  object.insert(String::make("obsolete"_str),
                entry_map_value(catalog.obsolete));
  auto output = rstd::json::to_string(
      rstd::json::Value::Object(rstd::move(object)),
      rstd::json::FormatOptions{.pretty = true, .indent = usize(2)});
  output.push_ascii('\n');
  return output;
}

auto occurrence_references(const Message &message) -> Vec<String> {
  auto references = BTreeMap<String, empty>::make();
  for (const auto &occurrence : message.occurrences) {
    references.insert(rstd::format("{}:{}", occurrence.source.as_str(),
                                   occurrence.position.line),
                      empty{});
  }
  auto output = Vec<String>::with_capacity(references.len());
  while (auto item = references.pop_first())
    output.push(rstd::move(item->template get<0>()));
  return output;
}

auto joined_notes(const Message &message) -> Option<String> {
  if (message.translator_notes.is_empty())
    return None();
  auto note = String::make();
  for (usize index{}; index < message.translator_notes.len(); ++index) {
    if (index != usize())
      note.push_ascii('\n');
    note.push_str(message.translator_notes[index].as_str());
  }
  return Some(rstd::move(note));
}

auto update_catalog(rstd::parse::SourceId source, ref<str> locale,
                    Option<ref<str>> existing, const Extraction &extraction)
    -> Result<String> {
  Catalog catalog{String::make(locale), BTreeMap<String, CatalogEntry>::make(),
                  BTreeMap<String, CatalogEntry>::make()};
  if (existing.is_some()) {
    auto parsed = parse_catalog(source.clone(), locale, *existing);
    if (parsed.is_err())
      return Err(rstd::move(parsed).unwrap_err_unchecked());
    catalog = rstd::move(parsed).unwrap_unchecked();
  } else if (!valid_locale(locale)) {
    return Err(catalog_error(DiagnosticCode::LocaleMismatch, source, {},
                             "invalid locale tag"_str));
  }

  auto current = BTreeMap<String, CatalogEntry>::make();
  for (const auto &message : extraction.messages) {
    auto prior = catalog.messages.remove(message.id.as_str());
    if (prior.is_none())
      prior = catalog.obsolete.remove(message.id.as_str());
    CatalogEntry entry{message.fallback.clone(), String::make(),
                       occurrence_references(message), joined_notes(message),
                       false};
    if (prior.is_some()) {
      entry.translation = prior->translation.clone();
      entry.needs_review = prior->needs_review;
      if (prior->source.as_str() != message.fallback.as_str())
        entry.needs_review = true;
    }
    current.insert(message.id.clone(), rstd::move(entry));
  }
  while (auto stale = catalog.messages.pop_first())
    catalog.obsolete.insert(rstd::move(stale->template get<0>()),
                            rstd::move(stale->template get<1>()));
  catalog.messages = rstd::move(current);
  return Ok(render_catalog(catalog));
}

auto check_catalog(rstd::parse::SourceId source, ref<str> locale,
                   ref<str> document, const Extraction &extraction)
    -> Result<empty> {
  auto updated =
      update_catalog(source.clone(), locale, Some(document), extraction);
  if (updated.is_err())
    return Err(rstd::move(updated).unwrap_err_unchecked());
  if (updated->as_str() != document) {
    return Err(
        catalog_error(DiagnosticCode::CatalogDrift, source, document,
                      "catalog is not synchronized with Lua sources"_str));
  }
  return Ok(empty{});
}

auto code_name(DiagnosticCode code) noexcept -> ref<str> {
  switch (code) {
  case DiagnosticCode::InvalidUtf8:
    return "invalid-utf8"_str;
  case DiagnosticCode::LexicalError:
    return "lexical-error"_str;
  case DiagnosticCode::SyntaxError:
    return "syntax-error"_str;
  case DiagnosticCode::CapacityLimit:
    return "capacity-limit"_str;
  case DiagnosticCode::InvalidTranslationCall:
    return "invalid-translation-call"_str;
  case DiagnosticCode::EmptyMessageId:
    return "empty-message-id"_str;
  case DiagnosticCode::DuplicateFallback:
    return "duplicate-fallback"_str;
  case DiagnosticCode::InvalidCatalog:
    return "invalid-catalog"_str;
  case DiagnosticCode::LocaleMismatch:
    return "locale-mismatch"_str;
  case DiagnosticCode::CatalogDrift:
    return "catalog-drift"_str;
  case DiagnosticCode::UnsafePluginPath:
    return "unsafe-plugin-path"_str;
  }
  rstd::unreachable();
}

} // namespace luato::i18n
