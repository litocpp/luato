module luato;
import :binding;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace luato {

auto Value::Table(luato::Table value) -> Value {
  return Value::Table(Box<luato::Table>::make(rstd::move(value)));
}

auto Value::Array(luato::Array value) -> Value {
  return Value::Array(Box<luato::Array>::make(rstd::move(value)));
}

auto Array::clone() const -> Array {
  auto copied = values_.iter()
                    .map([](auto value) { return value->clone(); })
                    .collect<Vec<Value>>();
  return Array(rstd::move(copied));
}

auto Value::type_name() const noexcept -> ref<str> {
  switch (tag()) {
  case Tag::Nil:
    return "nil"_str;
  case Tag::Number:
    return "number"_str;
  case Tag::Integer:
    return "integer"_str;
  case Tag::Boolean:
    return "boolean"_str;
  case Tag::String:
    return "string"_str;
  case Tag::Opaque:
    return "opaque handle"_str;
  case Tag::Table:
    return "table"_str;
  case Tag::Array:
    return "array"_str;
  }
  return "unknown"_str;
}

auto Value::clone() const -> Value {
  switch (tag()) {
  case Tag::Nil:
    return Nil();
  case Tag::Number:
    return Number(as_Number().value);
  case Tag::Integer:
    return Integer(as_Integer().value);
  case Tag::Boolean:
    return Boolean(as_Boolean().value);
  case Tag::String:
    return String(as_String().value.clone());
  case Tag::Opaque:
    return Opaque(as_Opaque().value);
  case Tag::Table:
    return Table(Box<luato::Table>::make(as_Table().value->clone()));
  case Tag::Array:
    return Array(Box<luato::Array>::make(as_Array().value->clone()));
  }
  rstd::panic{"invalid Luato value tag"};
}

auto Table::clone() const -> Table {
  auto copied = entries_.iter()
                    .map([](auto entry) { return entry->clone(); })
                    .collect<Vec<TableEntry>>();
  return Table(path_.clone(), rstd::move(copied));
}

auto Table::insert(String key, Value value) -> Result<empty> {
  if (key.is_empty())
    return Err(Error::binding("table field cannot be empty"_Str));
  if (entries_.iter().any(
          [&](auto entry) { return entry->key == key.as_str(); })) {
    return Err(Error::binding(
        rstd::format("duplicate table field '{}'", key.as_str())));
  }
  entries_.push(TableEntry{rstd::move(key), rstd::move(value)});
  return Ok(empty{});
}

auto Table::field_path(ref<str> key) const -> String {
  if (path_.is_empty())
    return String::make(key);
  return rstd::format("{}.{}", path_.as_str(), key);
}

auto Table::field_type_error(ref<str> key, ref<str> expected,
                             const Value &actual) const -> Error {
  return Error::make(ErrorKind::Type, String::make(),
                     rstd::format("{} must be {}, received {}",
                                  field_path(key).as_str(), expected,
                                  actual.type_name()));
}

auto Table::reject_unknown_fields(slice<String> known) const -> Result<empty> {
  return entries_.iter().try_for_each([&](auto borrowed) -> Result<empty> {
    const auto &entry = *borrowed;
    auto matched =
        rstd::iter::into_iter(slice<String>(known)).any([&](auto field) {
          return entry.key == field->as_str();
        });
    if (!matched) {
      return Err(Error::make(
          ErrorKind::Binding, String::make(),
          rstd::format("unknown field {}", field_path(entry.key).as_str())));
    }
    return Ok(empty{});
  });
}

auto Table::scalar_entries() const -> Result<Vec<ScalarEntry>> {
  return entries_.iter()
      .map([&](auto borrowed) -> Result<ScalarEntry> {
        const auto &entry = *borrowed;
        auto path = field_path(entry.key);
        switch (entry.value.tag()) {
        case Value::Tag::Nil:
        case Value::Tag::Number:
          return Err(Error::make(
              ErrorKind::Type, String::make(),
              rstd::format("{} must be an integer, boolean, or string",
                           path.as_str())));
        case Value::Tag::Integer:
          return Ok(ScalarEntry{
              entry.key.clone(), rstd::move(path),
              ScalarValue::Integer(entry.value.as_Integer().value)});
        case Value::Tag::Boolean:
          return Ok(ScalarEntry{
              entry.key.clone(), rstd::move(path),
              ScalarValue::Boolean(entry.value.as_Boolean().value)});
        case Value::Tag::String:
          return Ok(ScalarEntry{
              entry.key.clone(), rstd::move(path),
              ScalarValue::String(entry.value.as_String().value.clone())});
        case Value::Tag::Opaque:
          return Err(Error::make(
              ErrorKind::Type, String::make(),
              rstd::format("{} must be a scalar, received opaque handle",
                           path.as_str())));
        case Value::Tag::Table:
          return Err(
              Error::make(ErrorKind::Type, String::make(),
                          rstd::format("{} must be a scalar, received table",
                                       path.as_str())));
        case Value::Tag::Array:
          return Err(
              Error::make(ErrorKind::Type, String::make(),
                          rstd::format("{} must be a scalar, received array",
                                       path.as_str())));
        }
        rstd::panic{"invalid Luato value tag"};
      })
      .collect<Result<Vec<ScalarEntry>>>();
}

NativeFunctionSpec::NativeFunctionSpec(String name, usize arity,
                                       Box<NativeCallback> callback)
    : name_(rstd::move(name)), arity_(arity), callback_(rstd::move(callback)) {}

AsyncNativeFunctionSpec::AsyncNativeFunctionSpec(
    String name, usize arity, Box<AsyncNativeStarter> starter)
    : name_(rstd::move(name)), arity_(arity), starter_(rstd::move(starter)) {}

AsyncNativeHandleFunctionSpec::AsyncNativeHandleFunctionSpec(
    String name, usize arity, Box<AsyncHandleStarter> starter)
    : name_(rstd::move(name)), arity_(arity), starter_(rstd::move(starter)) {}

ModuleSpec::ModuleSpec(String name)
    : name_(rstd::move(name)), functions_(Vec<NativeFunctionSpec>::make()),
      async_functions_(Vec<AsyncNativeFunctionSpec>::make()),
      async_handle_functions_(Vec<AsyncNativeHandleFunctionSpec>::make()),
      async_values_functions_(Vec<AsyncNativeValuesFunctionSpec>::make()),
      fields_(Vec<TableEntry>::make()) {}

void ModuleSpec::add(NativeFunctionSpec function) {
  functions_.push(rstd::move(function));
}

void ModuleSpec::set(String name, i64 value) {
  fields_.push(TableEntry{rstd::move(name), Value::Integer(value)});
}

void ModuleSpec::set(String name, bool value) {
  fields_.push(TableEntry{rstd::move(name), Value::Boolean(value)});
}

void ModuleSpec::set(String name, String value) {
  fields_.push(TableEntry{rstd::move(name), Value::String(rstd::move(value))});
}

void ModuleSpec::set(String name, Table value) {
  fields_.push(TableEntry{rstd::move(name), Value::Table(rstd::move(value))});
}

void ModuleSpec::set(String name, Array value) {
  fields_.push(TableEntry{rstd::move(name), Value::Array(rstd::move(value))});
}

NativeRequireModuleSpec::NativeRequireModuleSpec(String require_name,
                                                 String identity,
                                                 ModuleSpec module)
    : require_name_(rstd::move(require_name)), identity_(rstd::move(identity)),
      global_alias_(None()), module_(rstd::move(module)) {}

void NativeRequireModuleSpec::set_global_alias(String alias) {
  global_alias_ = Some(rstd::move(alias));
}

} // namespace luato
