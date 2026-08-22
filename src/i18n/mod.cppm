export module luato.i18n;

export import rstd.parse;
export import rstd.json;

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
  DuplicateFallback,
  InvalidCatalog,
  LocaleMismatch,
  CatalogDrift,
  UnsafePluginPath,
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
  usize id_argument{};
  usize fallback_argument{1};
  usize exact_argument_count{2};
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
  rstd::parse::Span id_span;
  rstd::parse::Span fallback_span;
  rstd::parse::SourcePosition position;
  Option<String> translator_note;
};

struct Message {
  String id;
  String fallback;
  Vec<Occurrence> occurrences;
  Vec<String> translator_notes;
};

struct Extraction {
  Vec<Message> messages;
  Vec<Diagnostic> warnings;
};

auto extract(slice<SourceFile> sources, const ExtractionOptions &options)
    -> Result<Extraction>;

struct CatalogEntry {
  String source;
  String translation;
  Vec<String> references;
  Option<String> note;
  bool needs_review{};
};

struct Catalog {
  String locale;
  BTreeMap<String, CatalogEntry> messages;
  BTreeMap<String, CatalogEntry> obsolete;
};

auto parse_catalog(rstd::parse::SourceId source, ref<str> expected_locale,
                   ref<str> document) -> Result<Catalog>;
auto render_catalog(const Catalog &catalog) -> String;
auto update_catalog(rstd::parse::SourceId source, ref<str> locale,
                    Option<ref<str>> existing, const Extraction &extraction)
    -> Result<String>;
auto check_catalog(rstd::parse::SourceId source, ref<str> locale,
                   ref<str> document, const Extraction &extraction)
    -> Result<empty>;

} // namespace luato::i18n
