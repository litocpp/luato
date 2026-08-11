module;
#include <rstd/enum.hpp>

export module luato:binding;

export import :error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace luato
{

class State;
class Table;

class Value {
  RSTD_ENUM(Value,
            (Integer, (i64 value;)),
            (Boolean, (bool value;)),
            (String, (::alloc::string::String value;)),
            (Table, (Box<luato::Table> value;)))

public:
  Value(const Value &) = delete;
  auto operator=(const Value &) -> Value & = delete;
  Value(Value &&) noexcept = default;
  auto operator=(Value &&) noexcept -> Value & = default;

  static auto Table(luato::Table value) -> Value;

  auto type_name() const noexcept -> ref<str>;
  auto clone() const -> Value;
};

struct TableEntry {
  String key;
  Value value;

  auto clone() const -> TableEntry {
    return TableEntry{.key = key.clone(), .value = value.clone()};
  }
};

class ScalarValue {
  RSTD_ENUM(ScalarValue,
            (Integer, (i64 value;)),
            (Boolean, (bool value;)),
            (String, (::alloc::string::String value;)))

public:
  ScalarValue(const ScalarValue &) = delete;
  auto operator=(const ScalarValue &) -> ScalarValue & = delete;
  ScalarValue(ScalarValue &&) noexcept = default;
  auto operator=(ScalarValue &&) noexcept -> ScalarValue & = default;
};

struct ScalarEntry {
  String key;
  String path;
  ScalarValue value;
};

class Table {
public:
  static auto make() -> Table {
    return Table(String::make(), Vec<TableEntry>::make());
  }

  static auto with_path(String path, Vec<TableEntry> entries) -> Table {
    return Table(rstd::move(path), rstd::move(entries));
  }

  auto path() const noexcept -> ref<str> { return path_.as_str(); }
  auto entries() const noexcept -> slice<TableEntry> {
    return entries_.as_slice();
  }
  auto clone() const -> Table;

  auto insert(String key, Value value) -> Result<empty>;
  auto set(String key, i64 value) -> Result<empty> {
    return insert(rstd::move(key), Value::Integer(value));
  }
  auto set(String key, bool value) -> Result<empty> {
    return insert(rstd::move(key), Value::Boolean(value));
  }
  auto set(String key, String value) -> Result<empty> {
    return insert(rstd::move(key), Value::String(rstd::move(value)));
  }
  auto set(String key, Table value) -> Result<empty> {
    return insert(rstd::move(key), Value::Table(rstd::move(value)));
  }

  template <typename T>
    requires(rstd::mtp::same_as<T, i64> || rstd::mtp::same_as<T, bool> ||
             rstd::mtp::same_as<T, String> || rstd::mtp::same_as<T, Table>)
  auto required(ref<str> key) const -> Result<T> {
    auto *value = lookup(key);
    if (value == nullptr) {
      return Err(Error::make(
          ErrorKind::Type, String::make(),
          rstd::format("{} is required", field_path(key).as_str())));
    }

    if constexpr (rstd::mtp::same_as<T, i64>) {
      if (value->is_Integer())
        return Ok(value->as_Integer().value);
      return Err(field_type_error(key, "an integer"_str, *value));
    } else if constexpr (rstd::mtp::same_as<T, bool>) {
      if (value->is_Boolean())
        return Ok(value->as_Boolean().value);
      return Err(field_type_error(key, "a boolean"_str, *value));
    } else if constexpr (rstd::mtp::same_as<T, String>) {
      if (value->is_String())
        return Ok(value->as_String().value.clone());
      return Err(field_type_error(key, "a string"_str, *value));
    } else {
      if (value->is_Table())
        return Ok(value->as_Table().value->clone());
      return Err(field_type_error(key, "a table"_str, *value));
    }
  }

  auto reject_unknown_fields(slice<String> known) const -> Result<empty>;
  auto scalar_entries() const -> Result<Vec<ScalarEntry>>;

private:
  explicit Table(String path, Vec<TableEntry> entries)
      : path_(rstd::move(path)), entries_(rstd::move(entries)) {}

  auto lookup(ref<str> key) const noexcept -> const Value *;
  auto field_path(ref<str> key) const -> String;
  auto field_type_error(ref<str> key, ref<str> expected,
                        const Value &actual) const -> Error;

  String path_;
  Vec<TableEntry> entries_;
};

inline auto Value::Table(luato::Table value) -> Value {
  return Value::Table(Box<luato::Table>::make(rstd::move(value)));
}

inline auto Value::type_name() const noexcept -> ref<str> {
  switch (tag()) {
  case Tag::Integer:
    return "integer"_str;
  case Tag::Boolean:
    return "boolean"_str;
  case Tag::String:
    return "string"_str;
  case Tag::Table:
    return "table"_str;
  }
  return "unknown"_str;
}

inline auto Value::clone() const -> Value {
  switch (tag()) {
  case Tag::Integer:
    return Integer(as_Integer().value);
  case Tag::Boolean:
    return Boolean(as_Boolean().value);
  case Tag::String:
    return String(as_String().value.clone());
  case Tag::Table:
    return Table(Box<luato::Table>::make(as_Table().value->clone()));
  }
  rstd::panic { "invalid Luato value tag" };
}

inline auto Table::clone() const -> Table {
  auto copied = Vec<TableEntry>::with_capacity(entries_.len());
  for (const auto &entry : entries_)
    copied.push(entry.clone());
  return Table(path_.clone(), rstd::move(copied));
}

inline auto Table::insert(String key, Value value) -> Result<empty> {
  if (key.is_empty())
    return Err(Error::binding(String::make("table field cannot be empty"_str)));
  for (const auto &entry : entries_) {
    if (entry.key == key.as_str()) {
      return Err(Error::binding(
          rstd::format("duplicate table field '{}'", key.as_str())));
    }
  }
  entries_.push(TableEntry{rstd::move(key), rstd::move(value)});
  return Ok(empty{});
}

inline auto Table::lookup(ref<str> key) const noexcept -> const Value * {
  for (const auto &entry : entries_) {
    if (entry.key == key)
      return rstd::addressof(entry.value);
  }
  return nullptr;
}

inline auto Table::field_path(ref<str> key) const -> String {
  if (path_.is_empty())
    return String::make(key);
  return rstd::format("{}.{}", path_.as_str(), key);
}

inline auto Table::field_type_error(ref<str> key, ref<str> expected,
                                    const Value &actual) const -> Error {
  return Error::make(ErrorKind::Type, String::make(),
                     rstd::format("{} must be {}, received {}",
                                  field_path(key).as_str(), expected,
                                  actual.type_name()));
}

inline auto Table::reject_unknown_fields(slice<String> known) const
    -> Result<empty> {
  for (const auto &entry : entries_) {
    auto matched = false;
    for (const auto &field : known) {
      if (entry.key != field.as_str())
        continue;
      matched = true;
      break;
    }
    if (!matched) {
      return Err(Error::make(
          ErrorKind::Binding, String::make(),
          rstd::format("unknown field {}", field_path(entry.key).as_str())));
    }
  }
  return Ok(empty{});
}

inline auto Table::scalar_entries() const -> Result<Vec<ScalarEntry>> {
  auto result = Vec<ScalarEntry>::with_capacity(entries_.len());
  for (const auto &entry : entries_) {
    auto path = field_path(entry.key);
    switch (entry.value.tag()) {
    case Value::Tag::Integer:
      result.push(
          ScalarEntry{entry.key.clone(), rstd::move(path),
                      ScalarValue::Integer(entry.value.as_Integer().value)});
      break;
    case Value::Tag::Boolean:
      result.push(
          ScalarEntry{entry.key.clone(), rstd::move(path),
                      ScalarValue::Boolean(entry.value.as_Boolean().value)});
      break;
    case Value::Tag::String:
      result.push(ScalarEntry{
          entry.key.clone(), rstd::move(path),
          ScalarValue::String(entry.value.as_String().value.clone())});
      break;
    case Value::Tag::Table:
      return Err(Error::make(
          ErrorKind::Type, String::make(),
          rstd::format("{} must be a scalar, received table", path.as_str())));
    }
  }
  return Ok(rstd::move(result));
}

class CallFrame {
public:
    CallFrame(const CallFrame&)            = delete;
    CallFrame& operator=(const CallFrame&) = delete;

    auto argument_count() const noexcept -> usize { return argument_count_; }

    template <typename T>
      requires(rstd::mtp::same_as<T, i64> || rstd::mtp::same_as<T, bool> ||
               rstd::mtp::same_as<T, String> || rstd::mtp::same_as<T, Table>)
    auto required(usize index) -> Result<T> {
      if constexpr (rstd::mtp::same_as<T, i64>) {
        return read_i64_(context_, index);
      } else if constexpr (rstd::mtp::same_as<T, bool>) {
        return read_bool_(context_, index);
      } else if constexpr (rstd::mtp::same_as<T, String>) {
        return read_string_(context_, index);
      } else {
        return read_table_(context_, index);
      }
    }

    void push(i64 value) { push_value_(context_, Value::Integer(value)); }
    void push(bool value) { push_value_(context_, Value::Boolean(value)); }
    void push(ref<str> value) {
      push_value_(context_, Value::String(String::make(value)));
    }
    void push(String value) {
      push_value_(context_, Value::String(rstd::move(value)));
    }
    void push(Table value) {
      push_value_(context_, Value::Table(rstd::move(value)));
    }
    void push(Value value) { push_value_(context_, rstd::move(value)); }

  private:
    using ReadI64    = auto (*)(void*, usize) -> Result<i64>;
    using ReadBool = auto (*)(void *, usize) -> Result<bool>;
    using ReadString = auto (*)(void*, usize) -> Result<String>;
    using ReadTable = auto (*)(void *, usize) -> Result<Table>;
    using PushValue = void (*)(void *, Value);

    CallFrame(void *context, usize argument_count, ReadI64 read_i64,
              ReadBool read_bool, ReadString read_string, ReadTable read_table,
              PushValue push_value) noexcept
        : context_(context), argument_count_(argument_count),
          read_i64_(read_i64), read_bool_(read_bool), read_string_(read_string),
          read_table_(read_table), push_value_(push_value) {}

    void*      context_;
    usize      argument_count_;
    ReadI64    read_i64_;
    ReadBool read_bool_;
    ReadString read_string_;
    ReadTable read_table_;
    PushValue push_value_;

    friend class State;
};

using NativeCallback = dyn<FnMut<BindingResult(CallFrame&)>>;

class NativeFunctionSpec {
public:
    NativeFunctionSpec(NativeFunctionSpec&&) noexcept            = default;
    NativeFunctionSpec& operator=(NativeFunctionSpec&&) noexcept = default;

    template<typename Callback>
    static auto make(String name, usize arity, Callback&& callback) -> NativeFunctionSpec {
        return NativeFunctionSpec(
            rstd::move(name), arity, Box<NativeCallback>::make(rstd::forward<Callback>(callback)));
    }

private:
    NativeFunctionSpec(String name, usize arity, Box<NativeCallback> callback)
        : name_(rstd::move(name)), arity_(arity), callback_(rstd::move(callback)) {}

    String              name_;
    usize               arity_;
    Box<NativeCallback> callback_;

    friend class State;
};

class ModuleSpec {
public:
  explicit ModuleSpec(String name)
      : name_(rstd::move(name)), functions_(Vec<NativeFunctionSpec>::make()),
        fields_(Vec<TableEntry>::make()) {}

  ModuleSpec(ModuleSpec &&) noexcept = default;
  ModuleSpec &operator=(ModuleSpec &&) noexcept = default;

  void add(NativeFunctionSpec function) {
    functions_.push(rstd::move(function));
  }
  void set(String name, i64 value) {
    fields_.push(TableEntry{rstd::move(name), Value::Integer(value)});
  }
  void set(String name, bool value) {
    fields_.push(TableEntry{rstd::move(name), Value::Boolean(value)});
  }
  void set(String name, String value) {
    fields_.push(
        TableEntry{rstd::move(name), Value::String(rstd::move(value))});
  }
  void set(String name, Table value) {
    fields_.push(TableEntry{rstd::move(name), Value::Table(rstd::move(value))});
  }

private:
    String                  name_;
    Vec<NativeFunctionSpec> functions_;
    Vec<TableEntry> fields_;

    friend class State;
};

} // namespace luato
