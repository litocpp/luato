module luato;
import :serde;

using namespace rstd::prelude;

namespace luato {
auto make_serde_host_error(rstd::serde::DataPath path, ref<str> message,
                           Error source) -> rstd::serde::Error {
  return rstd::serde::Error::invalid_value_with_source(
      rstd::move(path), message, rstd::move(source));
}
} // namespace luato
