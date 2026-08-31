export module luato.i18n;

export import rstd.parse;

using namespace rstd::prelude;
using ::alloc::collections::BTreeMap;
using ::alloc::string::String;
using ::alloc::vec::Vec;

export namespace luato::i18n {

enum class DiagnosticCode : rstd::uint8_t {
  InvalidUtf8,
  LexicalError,
  SyntaxError,
  CapacityLimit,
  InvalidTranslationCall,
  EmptyMessageId,
};

auto code_name(DiagnosticCode code) noexcept -> ref<str>;

struct RelatedLocation {
  rstd::parse::SourceId source;
  rstd::parse::Span span;
  rstd::parse::SourcePosition position;
  String message;
};

struct Diagnostic {
  DiagnosticCode code;
  rstd::parse::SourceId source;
  rstd::parse::Span span;
  rstd::parse::SourcePosition position;
  String message;
  Vec<RelatedLocation> related;
};

template <typename T> using Result = rstd::Result<T, Diagnostic>;

struct SourceFile {
  rstd::parse::SourceId source;
  slice<u8> bytes;
};

struct CallSpec {
  Vec<String> callee;
  usize message_argument{};
  Option<usize> maximum_argument_count{Some(usize(1))};
  String translator_comment_prefix;
  Option<String> reserved_global;
};

struct ExtractionOptions {
  CallSpec call;
  usize max_file_bytes{usize(4 * 1024 * 1024)};
  usize max_tokens{usize(1024 * 1024)};
  usize max_nesting{usize(256)};
};

struct Occurrence {
  rstd::parse::SourceId source;
  rstd::parse::Span call_span;
  rstd::parse::Span message_span;
  rstd::parse::SourcePosition position;
  Option<String> translator_note;
};

struct Message {
  String msgid;
  Vec<Occurrence> occurrences;
  Vec<String> translator_notes;
};

struct Extraction {
  Vec<Message> messages;
  Vec<Diagnostic> warnings;
};

auto extract(slice<SourceFile> sources, const ExtractionOptions &options)
    -> Result<Extraction>;
auto merge_extractions(Vec<Extraction> extractions) -> Extraction;

} // namespace luato::i18n
