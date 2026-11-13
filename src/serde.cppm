module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>
export module luato:serde;
export import :state;
export import rstd.serde;
export import rstd.json;
using rstd::serde::DataPath;
using SerdeError = rstd::serde::Error;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace luato {
struct DeserializeOptions {
  const TableHandle *null_value{};
  usize max_depth{128};
};

class ValueDeserializer;
class ValueSequenceAccess;
class ValueMapAccess;
class ValueEnumAccess;

class ValueDeserializer {
  const LuaValue &value_;
  DeserializeOptions options_;
  DataPath path_;
  usize depth_{};

public:
  using error_type = SerdeError;
  using sequence_type = ValueSequenceAccess;
  using map_type = ValueMapAccess;
  using enum_type = ValueEnumAccess;

  ValueDeserializer(const LuaValue &value, DeserializeOptions options = {},
                    DataPath path = {}, usize depth = {})
      : value_(value), options_(options), path_(rstd::move(path)),
        depth_(depth) {}
  auto invalid_value(ref<str> text) const -> SerdeError {
    return SerdeError::invalid_value(path_.clone(), text);
  }
  auto missing_field(ref<str> name) const -> SerdeError {
    return SerdeError::missing_field(path_.clone(), name);
  }
  auto unknown_field(ref<str> name) const -> SerdeError {
    return SerdeError::unknown_field(path_.clone(), name);
  }
  auto unknown_variant(ref<str> name) const -> SerdeError {
    return SerdeError::unknown_variant(path_.clone(), name);
  }
  auto duplicate_field(ref<str> name) const -> SerdeError {
    return SerdeError::duplicate_field(path_.clone(), name);
  }
  auto invariant(ref<str> text) const -> SerdeError {
    return SerdeError::invariant(path_.clone(), text);
  }
  auto unsupported(ref<str> text) const -> SerdeError {
    return SerdeError::unsupported(path_.clone(), text);
  }
  template <class T>
  auto host(Result<T> value) const -> rstd::Result<T, SerdeError> {
    if (value.is_err())
      return Err(SerdeError::invalid_value_with_source(
          path_.clone(), "cannot read Lua value"_str,
          rstd::move(value).unwrap_err()));
    return Ok(rstd::move(value).unwrap());
  }
  auto is_none() const -> rstd::Result<bool, SerdeError> {
    RSTD_MATCH(value_) {
      RSTD_CASE(Nil) { return Ok(true); }
      RSTD_CASE(Opaque, value) { return Ok(value == nullptr); }
      RSTD_CASE(Table, value) {
        if (options_.null_value != nullptr)
          return host(value.same_object_in_call(*options_.null_value));
        return Ok(false);
      }
    default:
      return Ok(false);
    }
  }
  auto deserialize_unit() -> rstd::Result<empty, SerdeError> {
    if (!rstd_try(is_none()))
      return Err(invalid_value("expected nil"_str));
    return Ok(empty{});
  }
  auto deserialize_bool() -> rstd::Result<bool, SerdeError> {
    if (!value_.is_Boolean())
      return Err(invalid_value("expected a boolean"_str));
    return Ok(value_.as_Boolean().value);
  }
  auto deserialize_i64() -> rstd::Result<i64, SerdeError> {
    if (!value_.is_Integer())
      return Err(invalid_value("expected an integer"_str));
    return Ok(value_.as_Integer().value);
  }
  auto deserialize_u64() -> rstd::Result<u64, SerdeError> {
    auto value = rstd_try(deserialize_i64());
    if (value < i64())
      return Err(invalid_value("expected an unsigned integer"_str));
    return Ok(rstd::as_cast<u64>(value));
  }
  auto deserialize_f64() -> rstd::Result<f64, SerdeError> {
    RSTD_MATCH(value_) {
      RSTD_CASE(Number, value) { return Ok(value); }
      RSTD_CASE(Integer, value) { return Ok(rstd::as_cast<f64>(value)); }
    default:
      return Err(invalid_value("expected a number"_str));
    }
  }
  auto deserialize_string() -> rstd::Result<String, SerdeError> {
    return Ok(String::make(rstd_try(host(value_.as_utf8()))));
  }
  auto deserialize_bytes() -> rstd::Result<Vec<u8>, SerdeError> {
    if (!value_.is_Bytes())
      return Err(invalid_value("expected bytes"_str));
    return Ok(Vec<u8>::from(value_.as_Bytes().value.as_slice()));
  }
  auto deserialize_json_value() -> rstd::Result<rstd::json::Value, SerdeError>;
  template <class T>
  auto deserialize_option() -> rstd::Result<Option<T>, SerdeError> {
    if (rstd_try(is_none()))
      return Ok(None<T>());
    return Ok(Some(rstd_try(rstd::serde::deserialize<T>(*this))));
  }
  template <class T>
  auto deserialize_newtype(ref<str>) -> rstd::Result<T, SerdeError> {
    return rstd::serde::deserialize<T>(*this);
  }
  auto ignore_value() -> rstd::Result<empty, SerdeError> { return Ok(empty{}); }
  auto begin_sequence() -> rstd::Result<ValueSequenceAccess, SerdeError>;
  auto begin_map() -> rstd::Result<ValueMapAccess, SerdeError>;
  auto begin_enum() -> rstd::Result<ValueEnumAccess, SerdeError>;
  auto begin_adjacent_enum(ref<str> tag, ref<str> content,
                           rstd::serde::UnknownFieldPolicy unknown)
      -> rstd::Result<ValueEnumAccess, SerdeError>;
};

class ValueSequenceAccess {
  TableHandle table_;
  DeserializeOptions options_;
  DataPath path_;
  usize depth_, length_, index_{};

public:
  using error_type = SerdeError;
  ValueSequenceAccess(TableHandle table, DeserializeOptions options,
                      DataPath path, usize depth, usize length)
      : table_(rstd::move(table)), options_(options), path_(rstd::move(path)),
        depth_(depth), length_(length) {}
  template <class T> auto next() -> rstd::Result<Option<T>, SerdeError> {
    if (index_ == length_)
      return Ok(None<T>());
    auto key = LuaValue::Integer(i64((index_ + usize(1)).to_primitive()));
    auto read = table_.get_in_call(key);
    auto null = LuaValue::Nil();
    auto source =
        ValueDeserializer(null, options_, path_.with_index(index_), depth_);
    auto value = rstd_try(source.host(rstd::move(read)));
    auto child =
        ValueDeserializer(value, options_, path_.with_index(index_), depth_);
    auto result = rstd_try(rstd::serde::deserialize<T>(child));
    ++index_;
    return Ok(Some(rstd::move(result)));
  }
  auto end() -> rstd::Result<empty, SerdeError> {
    if (index_ != length_)
      return Err(SerdeError::invariant(path_.clone(),
                                       "sequence has unread elements"_str));
    return Ok(empty{});
  }
};

class ValueMapAccess {
  TableHandle table_;
  Vec<LuaValue> keys_;
  DeserializeOptions options_;
  DataPath path_;
  usize depth_, index_{};
  bool pending_{};

public:
  using error_type = SerdeError;
  ValueMapAccess(TableHandle table, Vec<LuaValue> keys,
                 DeserializeOptions options, DataPath path, usize depth)
      : table_(rstd::move(table)), keys_(rstd::move(keys)), options_(options),
        path_(rstd::move(path)), depth_(depth) {}
  template <class T> auto next_key() -> rstd::Result<Option<T>, SerdeError> {
    if (pending_)
      return Err(
          SerdeError::invariant(path_.clone(), "map value is missing"_str));
    if (index_ == keys_.len())
      return Ok(None<T>());
    auto child =
        ValueDeserializer(keys_[index_], options_, path_.clone(), depth_);
    auto key = rstd_try(rstd::serde::deserialize<T>(child));
    pending_ = true;
    return Ok(Some(rstd::move(key)));
  }
  template <class T> auto next_value() -> rstd::Result<T, SerdeError> {
    if (!pending_)
      return Err(
          SerdeError::invariant(path_.clone(), "map key is missing"_str));
    auto &key = keys_[index_];
    auto text = key.as_utf8();
    auto path = text.is_ok() ? path_.with_field(*text) : path_.clone();
    auto null = LuaValue::Nil();
    auto source = ValueDeserializer(null, options_, path.clone(), depth_);
    auto value = rstd_try(source.host(table_.get_in_call(key)));
    auto child = ValueDeserializer(value, options_, rstd::move(path), depth_);
    auto result = rstd_try(rstd::serde::deserialize<T>(child));
    pending_ = false;
    ++index_;
    return Ok(rstd::move(result));
  }
  auto ignore_value() -> rstd::Result<empty, SerdeError> {
    if (!pending_)
      return Err(
          SerdeError::invariant(path_.clone(), "map key is missing"_str));
    pending_ = false;
    ++index_;
    return Ok(empty{});
  }
  auto end() -> rstd::Result<empty, SerdeError> {
    if (pending_ || index_ != keys_.len())
      return Err(
          SerdeError::invariant(path_.clone(), "map has unread entries"_str));
    return Ok(empty{});
  }
};

class ValueEnumAccess {
  String variant_;
  Option<LuaValue> value_;
  DeserializeOptions options_;
  DataPath path_;
  usize depth_;
  bool adjacent_{};

public:
  using error_type = SerdeError;
  ValueEnumAccess(String variant, Option<LuaValue> value,
                  DeserializeOptions options, DataPath path, usize depth,
                  bool adjacent = false)
      : variant_(rstd::move(variant)), value_(rstd::move(value)),
        options_(options), path_(rstd::move(path)), depth_(depth),
        adjacent_(adjacent) {}
  auto variant() const -> ref<str> { return variant_.as_str(); }
  auto unit() -> rstd::Result<empty, SerdeError> {
    if (value_.is_none())
      return Ok(empty{});
    if (adjacent_)
      return Err(SerdeError::invalid_value(path_.clone(),
                                           "unexpected enum payload"_str));
    auto child = ValueDeserializer(*value_, options_, path_.clone(), depth_);
    return child.deserialize_unit();
  }
  template <class T> auto value() -> rstd::Result<T, SerdeError> {
    if (value_.is_none())
      return Err(
          SerdeError::invalid_value(path_.clone(), "missing enum payload"_str));
    auto child = ValueDeserializer(
        *value_, options_,
        adjacent_ ? path_.clone() : path_.with_variant(variant_.as_str()),
        depth_);
    return rstd::serde::deserialize<T>(child);
  }
};

inline auto ValueDeserializer::begin_sequence()
    -> rstd::Result<ValueSequenceAccess, SerdeError> {
  if (depth_ >= options_.max_depth)
    return Err(invalid_value("Lua value nesting is too deep"_str));
  if (!value_.is_Table() || rstd_try(is_none()))
    return Err(invalid_value("expected a sequence table"_str));
  auto table = rstd_try(host(value_.as_Table().value.clone()));
  auto length = rstd_try(host(table.length()));
  return Ok(ValueSequenceAccess(rstd::move(table), options_, path_.clone(),
                                depth_ + usize(1), length));
}
inline auto ValueDeserializer::begin_map()
    -> rstd::Result<ValueMapAccess, SerdeError> {
  if (depth_ >= options_.max_depth)
    return Err(invalid_value("Lua value nesting is too deep"_str));
  if (!value_.is_Table() || rstd_try(is_none()))
    return Err(invalid_value("expected a record table"_str));
  auto table = rstd_try(host(value_.as_Table().value.clone()));
  auto keys = rstd_try(host(table.keys()));
  return Ok(ValueMapAccess(rstd::move(table), rstd::move(keys), options_,
                           path_.clone(), depth_ + usize(1)));
}
inline auto ValueDeserializer::begin_enum()
    -> rstd::Result<ValueEnumAccess, SerdeError> {
  if (value_.is_Bytes())
    return Ok(ValueEnumAccess(rstd_try(deserialize_string()), None(), options_,
                              path_.clone(), depth_));
  if (depth_ >= options_.max_depth)
    return Err(invalid_value("Lua value nesting is too deep"_str));
  if (!value_.is_Table() || rstd_try(is_none()))
    return Err(invalid_value("expected an enum"_str));
  auto table = rstd_try(host(value_.as_Table().value.clone()));
  auto keys = rstd_try(host(table.keys()));
  if (keys.len() != usize(1))
    return Err(invalid_value("expected one enum variant"_str));
  auto name = rstd_try(host(keys[usize()].as_utf8()));
  auto value = rstd_try(host(table.get_in_call(keys[usize()])));
  return Ok(ValueEnumAccess(String::make(name), Some(rstd::move(value)),
                            options_, path_.clone(), depth_ + usize(1)));
}

inline auto
ValueDeserializer::begin_adjacent_enum(ref<str> tag, ref<str> content,
                                       rstd::serde::UnknownFieldPolicy unknown)
    -> rstd::Result<ValueEnumAccess, SerdeError> {
  auto map = rstd_try(begin_map());
  // Retain the Lua payload until the tag is known, independent of table key
  // order.
  auto table = rstd_try(host(value_.as_Table().value.clone()));
  auto keys = rstd_try(host(table.keys()));
  Option<String> variant;
  Option<LuaValue> payload;
  for (const auto &key : keys) {
    auto name = rstd_try(host(key.as_utf8()));
    auto value = rstd_try(host(table.get_in_call(key)));
    if (name == tag) {
      auto child = ValueDeserializer(value, options_, path_.with_field(tag),
                                     depth_ + usize(1));
      variant = Some(rstd_try(child.deserialize_string()));
    } else if (name == content) {
      payload = Some(rstd::move(value));
    } else if (unknown == rstd::serde::UnknownFieldPolicy::Reject) {
      return Err(unknown_field(name));
    }
  }
  if (variant.is_none())
    return Err(missing_field(tag));
  return Ok(ValueEnumAccess(rstd::move(*variant), rstd::move(payload), options_,
                            path_.with_field(content), depth_ + usize(1),
                            true));
}

inline auto ValueDeserializer::deserialize_json_value()
    -> rstd::Result<rstd::json::Value, SerdeError> {
  if (rstd_try(is_none()))
    return Ok(rstd::json::Value::Null());
  RSTD_MATCH(value_) {
    RSTD_CASE(Boolean, value) { return Ok(rstd::json::Value::Bool(value)); }
    RSTD_CASE(Integer, value) {
      return Ok(rstd::json::Value::Number(rstd::json::Number::from_i64(value)));
    }
    RSTD_CASE(Number, value) {
      auto number = rstd::json::Number::from_f64(value);
      if (number.is_none())
        return Err(invalid_value("JSON number must be finite"_str));
      return Ok(rstd::json::Value::Number(*number));
    }
    RSTD_CASE(Bytes, value) {
      return Ok(rstd::json::Value::String(rstd_try(deserialize_string())));
    }
    RSTD_CASE(Table, value) {
      auto table = rstd_try(host(value.clone()));
      auto length = rstd_try(host(table.length()));
      auto keys = rstd_try(host(table.keys()));
      bool sequence = length > usize();
      for (const auto &key : keys) {
        if (!key.is_Integer() || key.as_Integer().value < i64(1) ||
            key.as_Integer().value > i64(length.to_primitive())) {
          sequence = false;
          break;
        }
      }
      if (sequence) {
        auto values =
            rstd_try(rstd::serde::deserialize<Vec<rstd::json::Value>>(*this));
        return Ok(rstd::json::Value::Array(rstd::move(values)));
      }
      auto input = rstd_try(begin_map());
      rstd::json::Map values;
      for (;;) {
        auto key = rstd_try(input.next_key<String>());
        if (key.is_none())
          break;
        auto child = rstd_try(input.next_value<rstd::json::Value>());
        values.insert(rstd::move(*key), rstd::move(child));
      }
      rstd_try(input.end());
      return Ok(rstd::json::Value::Object(rstd::move(values)));
    }
  default:
    return Err(invalid_value("unsupported Lua value in JSON"_str));
  }
}

class SnapshotDeserializer;
class SnapshotSequenceAccess;
class SnapshotMapAccess;
class SnapshotEnumAccess;

class SnapshotDeserializer {
  const Value *value_{};
  const Table *table_{};
  DataPath path_;
  usize depth_{}, max_depth_;

public:
  using error_type = SerdeError;
  using sequence_type = SnapshotSequenceAccess;
  using map_type = SnapshotMapAccess;
  using enum_type = SnapshotEnumAccess;

  SnapshotDeserializer(const Value &value, DataPath path = {}, usize depth = {},
                       usize max_depth = usize(128))
      : value_(&value), path_(rstd::move(path)), depth_(depth),
        max_depth_(max_depth) {
    if (value.is_Table())
      table_ = &*value.as_Table().value;
  }
  SnapshotDeserializer(const Table &table, DataPath path = {}, usize depth = {},
                       usize max_depth = usize(128))
      : table_(&table), path_(rstd::move(path)), depth_(depth),
        max_depth_(max_depth) {}
  auto invalid_value(ref<str> text) const -> SerdeError {
    return SerdeError::invalid_value(path_.clone(), text);
  }
  auto missing_field(ref<str> name) const -> SerdeError {
    return SerdeError::missing_field(path_.clone(), name);
  }
  auto unknown_field(ref<str> name) const -> SerdeError {
    return SerdeError::unknown_field(path_.clone(), name);
  }
  auto unknown_variant(ref<str> name) const -> SerdeError {
    return SerdeError::unknown_variant(path_.clone(), name);
  }
  auto duplicate_field(ref<str> name) const -> SerdeError {
    return SerdeError::duplicate_field(path_.clone(), name);
  }
  auto invariant(ref<str> text) const -> SerdeError {
    return SerdeError::invariant(path_.clone(), text);
  }
  auto unsupported(ref<str> text) const -> SerdeError {
    return SerdeError::unsupported(path_.clone(), text);
  }
  auto is_none() const -> rstd::Result<bool, SerdeError> {
    return Ok(value_ != nullptr &&
              (value_->is_Nil() ||
               (value_->is_Opaque() && value_->as_Opaque().value == nullptr)));
  }
  auto deserialize_unit() -> rstd::Result<empty, SerdeError> {
    if (!rstd_try(is_none()))
      return Err(invalid_value("expected nil"_str));
    return Ok(empty{});
  }
  auto deserialize_bool() -> rstd::Result<bool, SerdeError> {
    if (value_ == nullptr || !value_->is_Boolean())
      return Err(invalid_value("expected a boolean"_str));
    return Ok(value_->as_Boolean().value);
  }
  auto deserialize_i64() -> rstd::Result<i64, SerdeError> {
    if (value_ == nullptr || !value_->is_Integer())
      return Err(invalid_value("expected an integer"_str));
    return Ok(value_->as_Integer().value);
  }
  auto deserialize_u64() -> rstd::Result<u64, SerdeError> {
    auto value = rstd_try(deserialize_i64());
    if (value < i64())
      return Err(invalid_value("expected an unsigned integer"_str));
    return Ok(rstd::as_cast<u64>(value));
  }
  auto deserialize_f64() -> rstd::Result<f64, SerdeError> {
    if (value_ != nullptr) {
      if (value_->is_Number())
        return Ok(value_->as_Number().value);
      if (value_->is_Integer())
        return Ok(rstd::as_cast<f64>(value_->as_Integer().value));
    }
    return Err(invalid_value("expected a number"_str));
  }
  auto deserialize_string() -> rstd::Result<String, SerdeError> {
    if (value_ == nullptr || !value_->is_String())
      return Err(invalid_value("expected a string"_str));
    return Ok(value_->as_String().value.clone());
  }
  auto deserialize_bytes() -> rstd::Result<Vec<u8>, SerdeError> {
    auto value = rstd_try(deserialize_string());
    return Ok(Vec<u8>::from(value.as_str().as_bytes()));
  }
  template <class T>
  auto deserialize_option() -> rstd::Result<Option<T>, SerdeError> {
    if (rstd_try(is_none()))
      return Ok(None<T>());
    return Ok(Some(rstd_try(rstd::serde::deserialize<T>(*this))));
  }
  template <class T>
  auto deserialize_newtype(ref<str>) -> rstd::Result<T, SerdeError> {
    return rstd::serde::deserialize<T>(*this);
  }
  auto ignore_value() -> rstd::Result<empty, SerdeError> { return Ok(empty{}); }
  auto begin_sequence() -> rstd::Result<SnapshotSequenceAccess, SerdeError>;
  auto begin_map() -> rstd::Result<SnapshotMapAccess, SerdeError>;
  auto begin_dictionary() -> rstd::Result<SnapshotMapAccess, SerdeError>;
  auto begin_enum() -> rstd::Result<SnapshotEnumAccess, SerdeError>;
  auto begin_adjacent_enum(ref<str> tag, ref<str> content,
                           rstd::serde::UnknownFieldPolicy unknown)
      -> rstd::Result<SnapshotEnumAccess, SerdeError>;
  auto deserialize_json_value() -> rstd::Result<rstd::json::Value, SerdeError>;
};

class SnapshotSequenceAccess {
  slice<Value> values_;
  DataPath path_;
  usize depth_, max_depth_, index_{};

public:
  using error_type = SerdeError;
  SnapshotSequenceAccess(slice<Value> values, DataPath path, usize depth,
                         usize max_depth)
      : values_(values), path_(rstd::move(path)), depth_(depth),
        max_depth_(max_depth) {}
  template <class T> auto next() -> rstd::Result<Option<T>, SerdeError> {
    if (index_ == values_.len())
      return Ok(None<T>());
    auto source = SnapshotDeserializer(
        values_[index_], path_.with_index(index_), depth_, max_depth_);
    auto value = rstd_try(rstd::serde::deserialize<T>(source));
    ++index_;
    return Ok(Some(rstd::move(value)));
  }
  auto end() -> rstd::Result<empty, SerdeError> {
    if (index_ != values_.len())
      return Err(SerdeError::invariant(path_.clone(),
                                       "sequence has unread elements"_str));
    return Ok(empty{});
  }
};

class SnapshotMapAccess {
  slice<TableEntry> entries_;
  DataPath path_;
  usize depth_, max_depth_, index_{};
  bool pending_{};

public:
  using error_type = SerdeError;
  SnapshotMapAccess(slice<TableEntry> entries, DataPath path, usize depth,
                    usize max_depth)
      : entries_(entries), path_(rstd::move(path)), depth_(depth),
        max_depth_(max_depth) {}
  template <class T> auto next_key() -> rstd::Result<Option<T>, SerdeError> {
    if (pending_)
      return Err(
          SerdeError::invariant(path_.clone(), "map value is missing"_str));
    if (index_ == entries_.len())
      return Ok(None<T>());
    auto key = Value::String(entries_[index_].key.clone());
    auto source = SnapshotDeserializer(key, path_.clone(), depth_, max_depth_);
    auto value = rstd_try(rstd::serde::deserialize<T>(source));
    pending_ = true;
    return Ok(Some(rstd::move(value)));
  }
  template <class T> auto next_value() -> rstd::Result<T, SerdeError> {
    if (!pending_)
      return Err(
          SerdeError::invariant(path_.clone(), "map key is missing"_str));
    const auto &entry = entries_[index_];
    auto source = SnapshotDeserializer(
        entry.value, path_.with_field(entry.key.as_str()), depth_, max_depth_);
    auto value = rstd_try(rstd::serde::deserialize<T>(source));
    pending_ = false;
    ++index_;
    return Ok(rstd::move(value));
  }
  auto ignore_value() -> rstd::Result<empty, SerdeError> {
    if (!pending_)
      return Err(
          SerdeError::invariant(path_.clone(), "map key is missing"_str));
    pending_ = false;
    ++index_;
    return Ok(empty{});
  }
  auto end() -> rstd::Result<empty, SerdeError> {
    if (pending_ || index_ != entries_.len())
      return Err(
          SerdeError::invariant(path_.clone(), "map has unread entries"_str));
    return Ok(empty{});
  }
};

class SnapshotEnumAccess {
  String variant_;
  const Value *value_;
  DataPath path_;
  usize depth_, max_depth_;
  bool adjacent_{};

public:
  SnapshotEnumAccess(String variant, const Value *value, DataPath path,
                     usize depth, usize max_depth, bool adjacent = false)
      : variant_(rstd::move(variant)), value_(value), path_(rstd::move(path)),
        depth_(depth), max_depth_(max_depth), adjacent_(adjacent) {}
  auto variant() const -> ref<str> { return variant_.as_str(); }
  auto unit() -> rstd::Result<empty, SerdeError> {
    if (value_ == nullptr)
      return Ok(empty{});
    if (adjacent_)
      return Err(SerdeError::invalid_value(path_.clone(),
                                           "unexpected enum payload"_str));
    auto source =
        SnapshotDeserializer(*value_, path_.clone(), depth_, max_depth_);
    return source.deserialize_unit();
  }
  template <class T> auto value() -> rstd::Result<T, SerdeError> {
    if (value_ == nullptr)
      return Err(
          SerdeError::invalid_value(path_.clone(), "missing enum payload"_str));
    auto source = SnapshotDeserializer(
        *value_,
        adjacent_ ? path_.clone() : path_.with_variant(variant_.as_str()),
        depth_, max_depth_);
    return rstd::serde::deserialize<T>(source);
  }
};

inline auto SnapshotDeserializer::begin_sequence()
    -> rstd::Result<SnapshotSequenceAccess, SerdeError> {
  if (depth_ >= max_depth_)
    return Err(invalid_value("Lua value nesting is too deep"_str));
  if (value_ != nullptr && value_->is_Array())
    return Ok(SnapshotSequenceAccess(value_->as_Array().value->values(),
                                     path_.clone(), depth_ + usize(1),
                                     max_depth_));
  if (table_ != nullptr && table_->entries().is_empty())
    return Ok(SnapshotSequenceAccess({}, path_.clone(), depth_ + usize(1),
                                     max_depth_));
  return Err(invalid_value("expected a sequence table"_str));
}
inline auto SnapshotDeserializer::begin_map()
    -> rstd::Result<SnapshotMapAccess, SerdeError> {
  if (depth_ >= max_depth_)
    return Err(invalid_value("Lua value nesting is too deep"_str));
  if (table_ == nullptr)
    return Err(invalid_value("expected a record table"_str));
  return Ok(SnapshotMapAccess(table_->entries(), path_.clone(),
                              depth_ + usize(1), max_depth_));
}
inline auto SnapshotDeserializer::begin_dictionary()
    -> rstd::Result<SnapshotMapAccess, SerdeError> {
  if (depth_ >= max_depth_)
    return Err(invalid_value("Lua value nesting is too deep"_str));
  if (value_ != nullptr && value_->is_Array() &&
      value_->as_Array().value->is_empty())
    return Ok(
        SnapshotMapAccess({}, path_.clone(), depth_ + usize(1), max_depth_));
  return begin_map();
}

inline auto SnapshotDeserializer::begin_enum()
    -> rstd::Result<SnapshotEnumAccess, SerdeError> {
  if (value_ != nullptr && value_->is_String())
    return Ok(SnapshotEnumAccess(rstd_try(deserialize_string()), nullptr,
                                 path_.clone(), depth_, max_depth_));
  if (depth_ >= max_depth_)
    return Err(invalid_value("Lua value nesting is too deep"_str));
  if (table_ == nullptr || table_->entries().len() != usize(1))
    return Err(invalid_value("expected one enum variant"_str));
  const auto &entry = table_->entries()[usize()];
  return Ok(SnapshotEnumAccess(entry.key.clone(), &entry.value, path_.clone(),
                               depth_ + usize(1), max_depth_));
}
inline auto SnapshotDeserializer::begin_adjacent_enum(
    ref<str> tag, ref<str> content, rstd::serde::UnknownFieldPolicy unknown)
    -> rstd::Result<SnapshotEnumAccess, SerdeError> {
  auto map = rstd_try(begin_map());
  Option<String> variant;
  const Value *payload = nullptr;
  for (const auto &entry : table_->entries()) {
    if (entry.key == tag) {
      auto child = SnapshotDeserializer(entry.value, path_.with_field(tag),
                                        depth_ + usize(1), max_depth_);
      variant = Some(rstd_try(child.deserialize_string()));
    } else if (entry.key == content) {
      payload = &entry.value;
    } else if (unknown == rstd::serde::UnknownFieldPolicy::Reject) {
      return Err(unknown_field(entry.key.as_str()));
    }
  }
  if (variant.is_none())
    return Err(missing_field(tag));
  return Ok(SnapshotEnumAccess(rstd::move(*variant), payload,
                               path_.with_field(content), depth_ + usize(1),
                               max_depth_, true));
}

inline auto SnapshotDeserializer::deserialize_json_value()
    -> rstd::Result<rstd::json::Value, SerdeError> {
  if (rstd_try(is_none()))
    return Ok(rstd::json::Value::Null());
  if (table_ != nullptr) {
    auto map = rstd_try(begin_map());
    rstd::json::Map values;
    for (;;) {
      auto key = rstd_try(map.next_key<String>());
      if (key.is_none())
        break;
      values.insert(rstd::move(*key),
                    rstd_try(map.next_value<rstd::json::Value>()));
    }
    rstd_try(map.end());
    return Ok(rstd::json::Value::Object(rstd::move(values)));
  }
  if (value_ != nullptr) {
    RSTD_MATCH(*value_) {
      RSTD_CASE(Boolean, value) { return Ok(rstd::json::Value::Bool(value)); }
      RSTD_CASE(Integer, value) {
        return Ok(
            rstd::json::Value::Number(rstd::json::Number::from_i64(value)));
      }
      RSTD_CASE(Number, value) {
        auto number = rstd::json::Number::from_f64(value);
        if (number.is_none())
          return Err(invalid_value("JSON number must be finite"_str));
        return Ok(rstd::json::Value::Number(*number));
      }
      RSTD_CASE(String, value) {
        return Ok(rstd::json::Value::String(value.clone()));
      }
      RSTD_CASE(Array) {
        return Ok(rstd::json::Value::Array(
            rstd_try(rstd::serde::deserialize<Vec<rstd::json::Value>>(*this))));
      }
    default:
      break;
    }
  }
  return Err(invalid_value("unsupported Lua value in JSON"_str));
}

template <class T, class Deserializer>
auto deserialize_value(Deserializer source) -> Result<T> {
  auto result = rstd::serde::deserialize<T>(source);
  if (result.is_err()) {
    auto error = rstd::move(result).unwrap_err();
    auto message = rstd::format("{}", error);
    return Err(Error::make(ErrorKind::Type, String::make("luato:serde"_str),
                           rstd::move(message))
                   .with_cause(rstd::move(error)));
  }
  return Ok(rstd::move(result).unwrap());
}

template <class T>
  requires rstd::serde::Deserializable<T>
auto from_value(const LuaValue &value, DeserializeOptions options = {})
    -> Result<T> {
  return deserialize_value<T>(ValueDeserializer(value, options));
}
template <class T>
  requires rstd::serde::Deserializable<T>
auto from_value(const Value &value, usize max_depth = usize(128)) -> Result<T> {
  return deserialize_value<T>(SnapshotDeserializer(value, {}, {}, max_depth));
}
template <class T>
  requires rstd::serde::Deserializable<T>
auto from_value(const Table &value, usize max_depth = usize(128)) -> Result<T> {
  return deserialize_value<T>(SnapshotDeserializer(value, {}, {}, max_depth));
}
} // namespace luato
