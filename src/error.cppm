export module luato:error;

export import rstd;

using namespace rstd::prelude;

export namespace luato {

enum class ErrorKind {
  StateCreation,
  Bootstrap,
  File,
  Syntax,
  Runtime,
  Memory,
  Binding,
  Type,
  Module,
  PanicInvariant,
};

struct ErrorDetails {
  String domain;
  String code;
};

struct Error {
  ErrorKind kind;
  String source;
  String message;
  String traceback;
  Option<ErrorDetails> details;

  auto clone() const -> Error {
    Error copy{kind, source.clone(), message.clone(), traceback.clone()};
    if (details.is_some())
      copy.details = Some(ErrorDetails{details->domain.clone(), details->code.clone()});
    return copy;
  }

  static auto make(ErrorKind kind, String source, String message) -> Error {
    return {kind, rstd::move(source), rstd::move(message), String::make()};
  }

  static auto binding(String message) -> Error {
    return make(ErrorKind::Binding, String::make(), rstd::move(message));
  }

  static auto structured(ErrorKind kind, String source, String message,
                         String domain, String code) -> Error {
    auto error = make(kind, rstd::move(source), rstd::move(message));
    error.details = Some(ErrorDetails{rstd::move(domain), rstd::move(code)});
    return error;
  }
};

template <typename T> using Result = rstd::Result<T, Error>;

using BindingResult = Result<usize>;

} // namespace luato
