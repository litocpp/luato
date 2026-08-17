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
  PanicInvariant,
};

struct Error {
  ErrorKind kind;
  String source;
  String message;
  String traceback;

  static auto make(ErrorKind kind, String source, String message) -> Error {
    return {kind, rstd::move(source), rstd::move(message), String::make()};
  }

  static auto binding(String message) -> Error {
    return make(ErrorKind::Binding, String::make(), rstd::move(message));
  }
};

template <typename T> using Result = rstd::Result<T, Error>;

using BindingResult = Result<usize>;

} // namespace luato
