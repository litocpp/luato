export module luato:error;

export import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::sync::Arc;

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

  Error() = default;
  Error(ErrorKind kind, String source, String message, String traceback,
        Option<ErrorDetails> details = None())
      : kind(kind), source(rstd::move(source)), message(rstd::move(message)),
        traceback(rstd::move(traceback)), details(rstd::move(details)) {}

  auto clone() const -> Error {
    Error copy{kind, source.clone(), message.clone(), traceback.clone()};
    if (details.is_some())
      copy.details =
          Some(ErrorDetails{details->domain.clone(), details->code.clone()});
    if (cause_.is_some())
      copy.cause_ = Some(cause_->clone());
    return copy;
  }

  auto with_cause(Box<dyn<rstd::error::Error>> cause) && -> Error {
    cause_ = Some(Arc<Cause>::make(rstd::move(cause)));
    return rstd::move(*this);
  }

  template <typename E>
    requires rstd::Impled<rstd::mtp::rm_cvf<E>, rstd::error::Error> &&
             (!rstd::mtp::same_as<rstd::mtp::rm_cvf<E>, rstd::error::ErrorRef>)
  auto with_cause(E &&cause) && -> Error {
    return rstd::move(*this).with_cause(
        Box<dyn<rstd::error::Error>>::make(rstd::forward<E>(cause)));
  }

  auto native_cause() const & noexcept [[clang::lifetimebound]]
  -> Option<rstd::error::ErrorRef> {
    if (cause_.is_none())
      return None();
    return Some((*cause_)->value.as_ref());
  }
  auto native_cause() const && -> Option<rstd::error::ErrorRef> = delete;

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

private:
  struct Cause {
    Box<dyn<rstd::error::Error>> value;
    explicit Cause(Box<dyn<rstd::error::Error>> value)
        : value(rstd::move(value)) {}
  };
  Option<Arc<Cause>> cause_;
};

template <typename T> using Result = rstd::Result<T, Error>;

using BindingResult = Result<usize>;

} // namespace luato

namespace rstd {

export template <>
struct Impl<fmt::Display, luato::Error> : ImplBase<luato::Error> {
  auto fmt(fmt::Formatter &formatter) const -> bool {
    return formatter.write_str(this->self().message.as_str());
  }
};

export template <>
struct Impl<fmt::Debug, luato::Error> : ImplBase<luato::Error> {
  auto fmt(fmt::Formatter &formatter) const -> bool {
    const auto &error = this->self();
    if (!formatter.write_fmt(
            fmt::Arguments::make("luato::Error {{ kind: {}, source: {:?}, "
                                 "message: {:?}, traceback: {:?}",
                                 static_cast<int>(error.kind), error.source,
                                 error.message, error.traceback)))
      return false;
    if (error.details.is_some() &&
        !formatter.write_fmt(fmt::Arguments::make(", domain: {:?}, code: {:?}",
                                                  error.details->domain,
                                                  error.details->code)))
      return false;
    if (auto cause = error.native_cause(); cause.is_some())
      if (!formatter.write_fmt(fmt::Arguments::make(", cause: {:?}", *cause)))
        return false;
    return formatter.write_str(" }"_str);
  }
};

export template <>
struct Impl<error::Error, luato::Error>
    : DefaultInImpl<error::Error, luato::Error> {
  auto source() const noexcept -> Option<error::ErrorRef> {
    return this->self().native_cause();
  }
};

} // namespace rstd
