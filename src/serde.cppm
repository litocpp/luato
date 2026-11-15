module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>
export module luato:serde;
export import :state;
export import rstd.serde;
export import rstd.json;
using rstd::serde::DataPath;
using SerdeError = rstd::serde::Error;
using alloc::rc::make_rc;
using alloc::rc::Rc;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace luato {
struct LuaValueCaptureStorage {
  LuaValue value;
  Option<TableHandle> null_value;
  Option<TableHandle> object_metatable;
  Option<TableHandle> array_metatable;
};
auto make_serde_host_error(DataPath path, ref<str> message, Error source)
    -> SerdeError;
} // namespace luato

export namespace luato {
struct DeserializeOptions {
  const TableHandle *null_value{};
  usize max_depth{128};
  const TableHandle *object_metatable{};
  const TableHandle *array_metatable{};
};

struct SerializeOptions {
  // Markers are borrowed during conversion and belong to the destination VM.
  const TableHandle *null_value{};
  const TableHandle *object_metatable{};
  const TableHandle *array_metatable{};
  bool serialize_none_to_null{true};
  bool serialize_unit_to_null{true};
  bool serialize_json_null_to_null{true};
  usize max_depth{128};

  static auto from_deserialize_options(DeserializeOptions source)
      -> SerializeOptions {
    auto options = SerializeOptions{};
    options.null_value = source.null_value;
    options.object_metatable = source.object_metatable;
    options.array_metatable = source.array_metatable;
    options.max_depth = source.max_depth;
    return options;
  }
  static auto lua(DeserializeOptions source = {}) -> SerializeOptions {
    auto options = from_deserialize_options(source);
    options.serialize_none_to_null = false;
    options.serialize_unit_to_null = false;
    return options;
  }
  auto deserialize_options() const -> DeserializeOptions {
    return {null_value, max_depth, object_metatable, array_metatable};
  }
};

class ValueSequenceSerializer;
class ValueMapSerializer;

class ValueSerializer {
  State *state_{};
  CallFrame *frame_{};
  SerializeOptions options_;
  DataPath path_;
  usize depth_{};

public:
  using value_type = LuaValue;
  using error_type = SerdeError;
  using result_type = rstd::Result<LuaValue, SerdeError>;
  using sequence_type = ValueSequenceSerializer;
  using map_type = ValueMapSerializer;

  ValueSerializer(State &state, SerializeOptions options = {})
      : state_(&state), options_(options) {}
  ValueSerializer(CallFrame &frame, SerializeOptions options = {})
      : frame_(&frame), options_(options) {}
  auto invalid_value(ref<str> text) const -> SerdeError {
    return SerdeError::invalid_value(path_.clone(), text);
  }
  auto unsupported(ref<str> text) const -> SerdeError {
    return SerdeError::unsupported(path_.clone(), text);
  }
  auto invariant(ref<str> text) const -> SerdeError {
    return SerdeError::invariant(path_.clone(), text);
  }
  template <class T>
  auto host(Result<T> value) const -> rstd::Result<T, SerdeError> {
    if (value.is_err())
      return Err(make_serde_host_error(path_.clone(),
                                       "cannot write Lua value"_str,
                                       rstd::move(value).unwrap_err()));
    return Ok(rstd::move(value).unwrap());
  }
  auto child(DataPath path) const -> ValueSerializer {
    auto result = frame_ ? ValueSerializer(*frame_, options_)
                         : ValueSerializer(*state_, options_);
    result.path_ = rstd::move(path);
    result.depth_ = depth_ + usize(1);
    return result;
  }
  auto field(ref<str> name) const -> ValueSerializer {
    return child(path_.with_field(name));
  }
  auto index(usize index) const -> ValueSerializer {
    return child(path_.with_index(index));
  }
  auto nested() const -> rstd::Result<empty, SerdeError> {
    if (depth_ >= options_.max_depth)
      return Err(invalid_value("Lua value nesting is too deep"_str));
    return Ok(empty{});
  }
  auto table(const TableHandle *metatable)
      -> rstd::Result<TableHandle, SerdeError> {
    auto result = rstd_try(
        host(frame_ ? frame_->create_table() : state_->create_table()));
    if (metatable)
      rstd_try(host(frame_ ? result.set_metatable_in_call(metatable)
                           : result.set_metatable(metatable)));
    return Ok(rstd::move(result));
  }
  auto set(TableHandle &table, const LuaValue &key, const LuaValue &value)
      -> rstd::Result<empty, SerdeError> {
    return host(frame_ ? table.set_in_call(key, value)
                       : table.raw_set(key, value));
  }
  auto serialize_null() -> result_type {
    if (!options_.null_value)
      return Ok(LuaValue::Opaque(nullptr));
    auto value = LuaValue::Table(rstd_try(host(options_.null_value->clone())));
    rstd_try(host(frame_ ? frame_->validate_value(value)
                         : state_->validate_value(value)));
    return Ok(rstd::move(value));
  }
  auto serialize_none() -> result_type {
    return options_.serialize_none_to_null ? serialize_null()
                                           : result_type(Ok(LuaValue::Nil()));
  }
  auto serialize_unit() -> result_type {
    return options_.serialize_unit_to_null ? serialize_null()
                                           : result_type(Ok(LuaValue::Nil()));
  }
  auto serialize_json_null() -> result_type {
    return options_.serialize_json_null_to_null
               ? serialize_null()
               : result_type(Ok(LuaValue::Nil()));
  }
  auto serialize_bool(bool value) -> result_type {
    return Ok(LuaValue::Boolean(value));
  }
  auto serialize_i64(i64 value) -> result_type {
    return Ok(LuaValue::Integer(value));
  }
  auto serialize_u64(u64 value) -> result_type {
    auto integer = rstd::try_from<i64>(value);
    if (integer.is_err())
      return Err(invalid_value("integer out of Lua range"_str));
    return serialize_i64(*integer);
  }
  auto serialize_f64(f64 value) -> result_type {
    return Ok(LuaValue::Number(value));
  }
  auto serialize_string(ref<str> value) -> result_type {
    return Ok(LuaValue::Bytes(Vec<u8>::from(value.as_bytes())));
  }
  auto serialize_bytes(slice<u8> value) -> result_type {
    return Ok(LuaValue::Bytes(Vec<u8>::from(value)));
  }
  template <class T> auto serialize_some(const T &value) -> result_type {
    rstd_try(nested());
    auto sink = child(path_.clone());
    return rstd::serde::serialize(sink, value);
  }
  template <class T>
  auto serialize_newtype(ref<str>, const T &value) -> result_type {
    return serialize_some(value);
  }
  auto serialize_unit_variant(ref<str> variant) -> result_type {
    return serialize_string(variant);
  }
  template <class T>
  auto serialize_newtype_variant(ref<str> variant, const T &value)
      -> result_type {
    rstd_try(nested());
    auto result = rstd_try(table(options_.object_metatable));
    auto sink = child(path_.with_variant(variant));
    auto encoded = rstd_try(rstd::serde::serialize(sink, value));
    auto key = rstd_try(serialize_string(variant));
    rstd_try(sink.set(result, key, encoded));
    return Ok(LuaValue::Table(rstd::move(result)));
  }
  template <class T>
  auto serialize_extension(ref<str>, const T &) -> result_type {
    return Err(unsupported("Lua does not support this extension"_str));
  }
  auto begin_sequence(usize)
      -> rstd::Result<ValueSequenceSerializer, SerdeError>;
  auto begin_map(usize) -> rstd::Result<ValueMapSerializer, SerdeError>;
};

class ValueSequenceSerializer {
  ValueSerializer sink_;
  TableHandle table_;
  usize index_{};
  bool ended_{};

public:
  using error_type = SerdeError;
  ValueSequenceSerializer(ValueSerializer sink, TableHandle table)
      : sink_(rstd::move(sink)), table_(rstd::move(table)) {}
  template <class T>
  auto element(const T &value) -> rstd::Result<empty, SerdeError> {
    if (ended_)
      return Err(sink_.invariant("sequence already ended"_str));
    auto child = sink_.index(index_);
    auto encoded = rstd_try(rstd::serde::serialize(child, value));
    auto index = rstd::try_from<i64>(index_ + usize(1));
    if (index.is_err())
      return Err(child.invalid_value("sequence index out of Lua range"_str));
    rstd_try(child.set(table_, LuaValue::Integer(*index), encoded));
    ++index_;
    return Ok(empty{});
  }
  auto end() -> ValueSerializer::result_type {
    if (ended_)
      return Err(sink_.invariant("sequence already ended"_str));
    ended_ = true;
    return Ok(LuaValue::Table(rstd::move(table_)));
  }
};

class ValueMapSerializer {
  ValueSerializer sink_;
  TableHandle table_;
  Option<LuaValue> key_;
  bool ended_{};

public:
  using error_type = SerdeError;
  ValueMapSerializer(ValueSerializer sink, TableHandle table)
      : sink_(rstd::move(sink)), table_(rstd::move(table)) {}
  template <class T>
  auto key(const T &value) -> rstd::Result<empty, SerdeError> {
    if (ended_)
      return Err(sink_.invariant("map already ended"_str));
    if (key_.is_some())
      return Err(sink_.invariant("map value is missing"_str));
    auto child = sink_.index(usize());
    auto key = rstd_try(rstd::serde::serialize(child, value));
    if (key.is_Nil() ||
        (key.is_Number() && key.as_Number().value.to_primitive() !=
                                key.as_Number().value.to_primitive()))
      return Err(sink_.invalid_value("invalid Lua table key"_str));
    key_ = Some(rstd::move(key));
    return Ok(empty{});
  }
  template <class T>
  auto value(const T &value) -> rstd::Result<empty, SerdeError> {
    if (ended_)
      return Err(sink_.invariant("map already ended"_str));
    if (key_.is_none())
      return Err(sink_.invariant("map key is missing"_str));
    auto text = key_->as_utf8();
    auto child = text.is_ok() ? sink_.field(*text) : sink_.index(usize());
    auto encoded = rstd_try(rstd::serde::serialize(child, value));
    rstd_try(child.set(table_, *key_, encoded));
    key_ = None();
    return Ok(empty{});
  }
  auto end() -> ValueSerializer::result_type {
    if (ended_)
      return Err(sink_.invariant("map already ended"_str));
    if (key_.is_some())
      return Err(sink_.invariant("map value is missing"_str));
    ended_ = true;
    return Ok(LuaValue::Table(rstd::move(table_)));
  }
};

inline auto ValueSerializer::begin_sequence(usize)
    -> rstd::Result<ValueSequenceSerializer, SerdeError> {
  rstd_try(nested());
  auto result = rstd_try(table(options_.array_metatable));
  auto sink = child(path_.clone());
  sink.depth_ = depth_;
  return Ok(ValueSequenceSerializer(rstd::move(sink), rstd::move(result)));
}
inline auto ValueSerializer::begin_map(usize)
    -> rstd::Result<ValueMapSerializer, SerdeError> {
  rstd_try(nested());
  auto result = rstd_try(table(options_.object_metatable));
  auto sink = child(path_.clone());
  sink.depth_ = depth_;
  return Ok(ValueMapSerializer(rstd::move(sink), rstd::move(result)));
}

static_assert(rstd::serde::Serializer<ValueSerializer>);

class ValueDeserializer;
class ValueSequenceAccess;
class ValueMapAccess;
class ValueEnumAccess;
class ValueInternalEnumAccess;

class CapturedLuaValue {
  Rc<LuaValueCaptureStorage> owner_;
  DataPath path_;
  usize depth_, max_depth_;
  Option<Rc<Vec<LuaValue>>> map_keys_;
  bool reject_unknown_fields_;
  friend class ValueDeserializer;

  CapturedLuaValue(Rc<LuaValueCaptureStorage> owner, DataPath path, usize depth,
                   usize max_depth, Option<Rc<Vec<LuaValue>>> map_keys,
                   bool reject_unknown_fields)
      : owner_(rstd::move(owner)), path_(rstd::move(path)), depth_(depth),
        max_depth_(max_depth), map_keys_(rstd::move(map_keys)),
        reject_unknown_fields_(reject_unknown_fields) {}

public:
  auto replay() const -> ValueDeserializer;
};

class ValueDeserializer {
  const LuaValue &value_;
  DeserializeOptions options_;
  DataPath path_;
  usize depth_{};
  Option<Rc<Vec<LuaValue>>> map_keys_;
  bool map_started_{}, reject_unknown_fields_{};
  Option<Rc<LuaValueCaptureStorage>> owner_;
  friend class CapturedLuaValue;

public:
  using error_type = SerdeError;
  using sequence_type = ValueSequenceAccess;
  using map_type = ValueMapAccess;
  using enum_type = ValueEnumAccess;

  ValueDeserializer(const LuaValue &value, DeserializeOptions options = {},
                    DataPath path = {}, usize depth = {},
                    Option<Vec<LuaValue>> map_keys = None(),
                    bool reject_unknown_fields = false,
                    Option<Rc<LuaValueCaptureStorage>> owner = None())
      : value_(value), options_(options), path_(rstd::move(path)),
        depth_(depth), reject_unknown_fields_(reject_unknown_fields),
        owner_(rstd::move(owner)) {
    if (map_keys.is_some())
      map_keys_ = Some(make_rc<Vec<LuaValue>>(rstd::move(*map_keys)));
  }
  auto capture_value() -> rstd::Result<CapturedLuaValue, SerdeError>;
  auto record_unknown_fields(rstd::serde::UnknownFieldPolicy declared) const
      -> rstd::serde::UnknownFieldPolicy {
    return reject_unknown_fields_ ? rstd::serde::UnknownFieldPolicy::Reject
                                  : declared;
  }
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
      return Err(make_serde_host_error(path_.clone(),
                                       "cannot read Lua value"_str,
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
      RSTD_CASE_DEFAULT { return Ok(false); }
    }
  }
  auto has_metatable(const TableHandle *marker) const
      -> rstd::Result<bool, SerdeError> {
    if (!marker || !value_.is_Table())
      return Ok(false);
    auto table = rstd_try(host(value_.as_Table().value.clone()));
    auto meta = rstd_try(host(table.metatable_in_call()));
    if (meta.is_none())
      return Ok(false);
    return host(meta->same_object_in_call(*marker));
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
      RSTD_CASE_DEFAULT { return Err(invalid_value("expected a number"_str)); }
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
  auto begin_internal_enum(ref<str> tag,
                           rstd::serde::UnknownFieldPolicy unknown)
      -> rstd::Result<ValueInternalEnumAccess, SerdeError>;
  auto begin_adjacent_enum(ref<str> tag, ref<str> content,
                           rstd::serde::UnknownFieldPolicy unknown)
      -> rstd::Result<ValueEnumAccess, SerdeError>;
};

inline auto ValueDeserializer::capture_value()
    -> rstd::Result<CapturedLuaValue, SerdeError> {
  if (map_keys_.is_some() && map_started_)
    return Err(invariant("map already consumed"_str));
  auto clone_marker = [&](const TableHandle *marker)
      -> rstd::Result<Option<TableHandle>, SerdeError> {
    if (!marker)
      return Ok(None<TableHandle>());
    return Ok(Some(rstd_try(host(marker->clone()))));
  };
  auto value = rstd_try(host(value_.clone()));
  auto null = rstd_try(clone_marker(options_.null_value));
  auto object = rstd_try(clone_marker(options_.object_metatable));
  auto array = rstd_try(clone_marker(options_.array_metatable));
  auto owner = make_rc<LuaValueCaptureStorage>(
      LuaValueCaptureStorage{rstd::move(value), rstd::move(null),
                             rstd::move(object), rstd::move(array)});
  return Ok(CapturedLuaValue(rstd::move(owner), path_.clone(), depth_,
                             options_.max_depth, map_keys_.clone(),
                             reject_unknown_fields_));
}

inline auto CapturedLuaValue::replay() const -> ValueDeserializer {
  const auto &storage = *owner_;
  DeserializeOptions options;
  options.max_depth = max_depth_;
  if (storage.null_value.is_some())
    options.null_value = &*storage.null_value;
  if (storage.object_metatable.is_some())
    options.object_metatable = &*storage.object_metatable;
  if (storage.array_metatable.is_some())
    options.array_metatable = &*storage.array_metatable;
  auto source =
      ValueDeserializer(storage.value, options, path_.clone(), depth_, None(),
                        reject_unknown_fields_, Some(owner_.clone()));
  source.map_keys_ = map_keys_.clone();
  return source;
}

class ValueSequenceAccess {
  TableHandle table_;
  DeserializeOptions options_;
  DataPath path_;
  usize depth_, length_, index_{};
  Option<Rc<LuaValueCaptureStorage>> owner_;

public:
  using error_type = SerdeError;
  ValueSequenceAccess(TableHandle table, DeserializeOptions options,
                      DataPath path, usize depth, usize length,
                      Option<Rc<LuaValueCaptureStorage>> owner = None())
      : table_(rstd::move(table)), options_(options), path_(rstd::move(path)),
        depth_(depth), length_(length), owner_(rstd::move(owner)) {}
  template <class T> auto next() -> rstd::Result<Option<T>, SerdeError> {
    if (index_ == length_)
      return Ok(None<T>());
    auto key = LuaValue::Integer(i64((index_ + usize(1)).to_primitive()));
    auto read = table_.get_in_call(key);
    auto null = LuaValue::Nil();
    auto source = ValueDeserializer(null, options_, path_.with_index(index_),
                                    depth_, None(), false, owner_.clone());
    auto value = rstd_try(source.host(rstd::move(read)));
    auto child = ValueDeserializer(value, options_, path_.with_index(index_),
                                   depth_, None(), false, owner_.clone());
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
  Rc<Vec<LuaValue>> keys_;
  DeserializeOptions options_;
  DataPath path_;
  usize depth_, index_{};
  bool pending_{};
  Option<Rc<LuaValueCaptureStorage>> owner_;
  friend class ValueDeserializer;

public:
  using error_type = SerdeError;
  ValueMapAccess(TableHandle table, Vec<LuaValue> keys,
                 DeserializeOptions options, DataPath path, usize depth)
      : ValueMapAccess(rstd::move(table),
                       make_rc<Vec<LuaValue>>(rstd::move(keys)), options,
                       rstd::move(path), depth) {}
  ValueMapAccess(TableHandle table, Rc<Vec<LuaValue>> keys,
                 DeserializeOptions options, DataPath path, usize depth,
                 Option<Rc<LuaValueCaptureStorage>> owner = None())
      : table_(rstd::move(table)), keys_(rstd::move(keys)), options_(options),
        path_(rstd::move(path)), depth_(depth), owner_(rstd::move(owner)) {}
  template <class T> auto next_key() -> rstd::Result<Option<T>, SerdeError> {
    if (pending_)
      return Err(
          SerdeError::invariant(path_.clone(), "map value is missing"_str));
    if (index_ == keys_->len())
      return Ok(None<T>());
    auto child = ValueDeserializer((*keys_)[index_], options_, path_.clone(),
                                   depth_, None(), false, owner_.clone());
    auto key = rstd_try(rstd::serde::deserialize<T>(child));
    pending_ = true;
    return Ok(Some(rstd::move(key)));
  }
  template <class T> auto next_value() -> rstd::Result<T, SerdeError> {
    if (!pending_)
      return Err(
          SerdeError::invariant(path_.clone(), "map key is missing"_str));
    const auto &key = (*keys_)[index_];
    auto text = key.as_utf8();
    auto path = text.is_ok() ? path_.with_field(*text) : path_.clone();
    auto null = LuaValue::Nil();
    auto source = ValueDeserializer(null, options_, path.clone(), depth_,
                                    None(), false, owner_.clone());
    auto value = rstd_try(source.host(table_.get_in_call(key)));
    auto child = ValueDeserializer(value, options_, rstd::move(path), depth_,
                                   None(), false, owner_.clone());
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
    if (pending_ || index_ != keys_->len())
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
  Option<Rc<LuaValueCaptureStorage>> owner_;

public:
  using error_type = SerdeError;
  ValueEnumAccess(String variant, Option<LuaValue> value,
                  DeserializeOptions options, DataPath path, usize depth,
                  bool adjacent = false,
                  Option<Rc<LuaValueCaptureStorage>> owner = None())
      : variant_(rstd::move(variant)), value_(rstd::move(value)),
        options_(options), path_(rstd::move(path)), depth_(depth),
        adjacent_(adjacent), owner_(rstd::move(owner)) {}
  auto variant() const -> ref<str> { return variant_.as_str(); }
  auto unit() -> rstd::Result<empty, SerdeError> {
    if (value_.is_none())
      return Ok(empty{});
    auto child = ValueDeserializer(*value_, options_, path_.clone(), depth_,
                                   None(), false, owner_.clone());
    return child.deserialize_unit();
  }
  template <class T> auto value() -> rstd::Result<T, SerdeError> {
    if (value_.is_none())
      return Err(
          SerdeError::invalid_value(path_.clone(), "missing enum payload"_str));
    auto child = ValueDeserializer(
        *value_, options_,
        adjacent_ ? path_.clone() : path_.with_variant(variant_.as_str()),
        depth_, None(), false, owner_.clone());
    return rstd::serde::deserialize<T>(child);
  }
};

class ValueInternalEnumAccess {
  String variant_;
  LuaValue value_;
  Vec<LuaValue> keys_;
  DeserializeOptions options_;
  DataPath path_;
  usize depth_;
  rstd::serde::UnknownFieldPolicy unknown_;
  bool consumed_{};
  Option<Rc<LuaValueCaptureStorage>> owner_;

public:
  using error_type = SerdeError;
  ValueInternalEnumAccess(String variant, TableHandle table, Vec<LuaValue> keys,
                          DeserializeOptions options, DataPath path,
                          usize depth, rstd::serde::UnknownFieldPolicy unknown,
                          Option<Rc<LuaValueCaptureStorage>> owner = None())
      : variant_(rstd::move(variant)),
        value_(LuaValue::Table(rstd::move(table))), keys_(rstd::move(keys)),
        options_(options), path_(rstd::move(path)), depth_(depth),
        unknown_(unknown), owner_(rstd::move(owner)) {}
  auto variant() const &[[clang::lifetimebound]] -> ref<str> {
    return variant_.as_str();
  }
  auto unit() -> rstd::Result<empty, SerdeError> {
    if (consumed_)
      return Err(
          SerdeError::invariant(path_.clone(), "enum already consumed"_str));
    consumed_ = true;
    if (unknown_ == rstd::serde::UnknownFieldPolicy::Reject &&
        !keys_.is_empty()) {
      auto source = ValueDeserializer(value_, options_, path_.clone(), depth_,
                                      None(), false, owner_.clone());
      auto name = rstd_try(source.host(keys_[usize()].as_utf8()));
      return Err(source.unknown_field(name));
    }
    return Ok(empty{});
  }
  template <class T> auto value() -> rstd::Result<T, SerdeError> {
    if (consumed_)
      return Err(
          SerdeError::invariant(path_.clone(), "enum already consumed"_str));
    consumed_ = true;
    auto source = ValueDeserializer(
        value_, options_, path_.clone(), depth_, Some(rstd::move(keys_)),
        unknown_ == rstd::serde::UnknownFieldPolicy::Reject, owner_.clone());
    return rstd::serde::deserialize<T>(source);
  }
};

inline auto ValueDeserializer::begin_sequence()
    -> rstd::Result<ValueSequenceAccess, SerdeError> {
  if (depth_ >= options_.max_depth)
    return Err(invalid_value("Lua value nesting is too deep"_str));
  if (map_keys_.is_some() || !value_.is_Table() || rstd_try(is_none()))
    return Err(invalid_value("expected a sequence table"_str));
  if (rstd_try(has_metatable(options_.object_metatable)))
    return Err(invalid_value("expected a sequence table"_str));
  auto table = rstd_try(host(value_.as_Table().value.clone()));
  auto length = rstd_try(host(table.length()));
  auto keys = rstd_try(host(table.keys()));
  if (keys.len() != length)
    return Err(invalid_value("expected a contiguous sequence table"_str));
  for (const auto &key : keys) {
    if (!key.is_Integer() || key.as_Integer().value < i64(1) ||
        key.as_Integer().value >
            i64(static_cast<long long>(length.to_primitive())))
      return Err(invalid_value("expected a contiguous sequence table"_str));
  }
  return Ok(ValueSequenceAccess(rstd::move(table), options_, path_.clone(),
                                depth_ + usize(1), length, owner_.clone()));
}
inline auto ValueDeserializer::begin_map()
    -> rstd::Result<ValueMapAccess, SerdeError> {
  if (depth_ >= options_.max_depth)
    return Err(invalid_value("Lua value nesting is too deep"_str));
  if (!value_.is_Table() || rstd_try(is_none()))
    return Err(invalid_value("expected a record table"_str));
  if (rstd_try(has_metatable(options_.array_metatable)))
    return Err(invalid_value("expected a record table"_str));
  if (map_keys_.is_some() && map_started_)
    return Err(invariant("map already consumed"_str));
  auto table = rstd_try(host(value_.as_Table().value.clone()));
  auto keys = map_keys_.is_some()
                  ? map_keys_->clone()
                  : make_rc<Vec<LuaValue>>(rstd_try(host(table.keys())));
  map_started_ = true;
  return Ok(ValueMapAccess(rstd::move(table), rstd::move(keys), options_,
                           path_.clone(), depth_ + usize(1), owner_.clone()));
}

inline auto
ValueDeserializer::begin_internal_enum(ref<str> tag,
                                       rstd::serde::UnknownFieldPolicy unknown)
    -> rstd::Result<ValueInternalEnumAccess, SerdeError> {
  auto map = rstd_try(begin_map());
  Option<String> variant;
  Vec<LuaValue> keys;
  for (const auto &key : *map.keys_) {
    auto name = rstd_try(host(key.as_utf8()));
    if (name == tag) {
      if (variant.is_some())
        return Err(duplicate_field(tag));
      auto value = rstd_try(host(map.table_.get_in_call(key)));
      auto child =
          ValueDeserializer(value, options_, path_.with_field(tag),
                            depth_ + usize(1), None(), false, owner_.clone());
      variant = Some(rstd_try(child.deserialize_string()));
    } else {
      keys.push(rstd_try(host(key.clone())));
    }
  }
  if (variant.is_none())
    return Err(missing_field(tag));
  return Ok(ValueInternalEnumAccess(
      rstd::move(*variant), rstd::move(map.table_), rstd::move(keys), options_,
      path_.clone(), depth_, unknown, owner_.clone()));
}
inline auto ValueDeserializer::begin_enum()
    -> rstd::Result<ValueEnumAccess, SerdeError> {
  if (value_.is_Bytes())
    return Ok(ValueEnumAccess(rstd_try(deserialize_string()), None(), options_,
                              path_.clone(), depth_, false, owner_.clone()));
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
                            options_, path_.clone(), depth_ + usize(1), false,
                            owner_.clone()));
}

inline auto
ValueDeserializer::begin_adjacent_enum(ref<str> tag, ref<str> content,
                                       rstd::serde::UnknownFieldPolicy unknown)
    -> rstd::Result<ValueEnumAccess, SerdeError> {
  auto map = rstd_try(begin_map());
  Option<String> variant;
  Option<LuaValue> payload;
  for (const auto &key : *map.keys_) {
    auto name = rstd_try(host(key.as_utf8()));
    auto value = rstd_try(host(map.table_.get_in_call(key)));
    if (name == tag) {
      auto child =
          ValueDeserializer(value, options_, path_.with_field(tag),
                            depth_ + usize(1), None(), false, owner_.clone());
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
                            path_.with_field(content), depth_ + usize(1), true,
                            owner_.clone()));
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
      auto marked_array = rstd_try(has_metatable(options_.array_metatable));
      auto marked_object = rstd_try(has_metatable(options_.object_metatable));
      bool sequence = marked_array || (!marked_object && length > usize());
      for (const auto &key : keys) {
        if (!key.is_Integer() || key.as_Integer().value < i64(1) ||
            key.as_Integer().value > i64(length.to_primitive())) {
          if (marked_array)
            return Err(invalid_value("invalid array table key"_str));
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
    RSTD_CASE_DEFAULT {
      return Err(invalid_value("unsupported Lua value in JSON"_str));
    }
  }
}

class SnapshotDeserializer;
class SnapshotSequenceAccess;
class SnapshotMapAccess;
class SnapshotEnumAccess;
class SnapshotInternalEnumAccess;

class CapturedSnapshotValue {
  Rc<Value> owner_;
  DataPath path_;
  usize depth_, max_depth_;
  bool reject_unknown_fields_;
  Option<Vec<usize>> map_indices_;
  friend class SnapshotDeserializer;

  CapturedSnapshotValue(Rc<Value> owner, DataPath path, usize depth,
                        usize max_depth, bool reject_unknown_fields,
                        Option<Vec<usize>> map_indices)
      : owner_(rstd::move(owner)), path_(rstd::move(path)), depth_(depth),
        max_depth_(max_depth), reject_unknown_fields_(reject_unknown_fields),
        map_indices_(rstd::move(map_indices)) {}

public:
  auto replay() const -> SnapshotDeserializer;
};

class SnapshotDeserializer {
  const Value *value_{};
  const Table *table_{};
  DataPath path_;
  usize depth_{}, max_depth_;
  bool reject_unknown_fields_{};
  Option<Vec<usize>> map_indices_;
  bool map_started_{};
  Option<Rc<Value>> owner_;

public:
  using error_type = SerdeError;
  using sequence_type = SnapshotSequenceAccess;
  using map_type = SnapshotMapAccess;
  using enum_type = SnapshotEnumAccess;

  SnapshotDeserializer(const Value &value, DataPath path = {}, usize depth = {},
                       usize max_depth = usize(128),
                       bool reject_unknown_fields = false,
                       Option<Vec<usize>> map_indices = None(),
                       Option<Rc<Value>> owner = None())
      : value_(&value), path_(rstd::move(path)), depth_(depth),
        max_depth_(max_depth), reject_unknown_fields_(reject_unknown_fields),
        map_indices_(rstd::move(map_indices)), owner_(rstd::move(owner)) {
    if (value.is_Table())
      table_ = &*value.as_Table().value;
  }
  SnapshotDeserializer(const Table &table, DataPath path = {}, usize depth = {},
                       usize max_depth = usize(128),
                       bool reject_unknown_fields = false,
                       Option<Vec<usize>> map_indices = None(),
                       Option<Rc<Value>> owner = None())
      : table_(&table), path_(rstd::move(path)), depth_(depth),
        max_depth_(max_depth), reject_unknown_fields_(reject_unknown_fields),
        map_indices_(rstd::move(map_indices)), owner_(rstd::move(owner)) {}
  auto capture_value() -> rstd::Result<CapturedSnapshotValue, SerdeError> {
    if (map_indices_.is_some() && map_started_)
      return Err(invariant("map already consumed"_str));
    auto value = value_ ? value_->clone() : Value::Table(table_->clone());
    return Ok(CapturedSnapshotValue(
        make_rc<Value>(rstd::move(value)), path_.clone(), depth_, max_depth_,
        reject_unknown_fields_, map_indices_.clone()));
  }
  auto record_unknown_fields(rstd::serde::UnknownFieldPolicy declared) const
      -> rstd::serde::UnknownFieldPolicy {
    return reject_unknown_fields_ ? rstd::serde::UnknownFieldPolicy::Reject
                                  : declared;
  }
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
  auto begin_internal_enum(ref<str> tag,
                           rstd::serde::UnknownFieldPolicy unknown)
      -> rstd::Result<SnapshotInternalEnumAccess, SerdeError>;
  auto begin_adjacent_enum(ref<str> tag, ref<str> content,
                           rstd::serde::UnknownFieldPolicy unknown)
      -> rstd::Result<SnapshotEnumAccess, SerdeError>;
  auto deserialize_json_value() -> rstd::Result<rstd::json::Value, SerdeError>;
};

inline auto CapturedSnapshotValue::replay() const -> SnapshotDeserializer {
  return SnapshotDeserializer(*owner_, path_.clone(), depth_, max_depth_,
                              reject_unknown_fields_, map_indices_.clone(),
                              Some(owner_.clone()));
}

class SnapshotSequenceAccess {
  slice<Value> values_;
  DataPath path_;
  usize depth_, max_depth_, index_{};
  Option<Rc<Value>> owner_;

public:
  using error_type = SerdeError;
  SnapshotSequenceAccess(slice<Value> values, DataPath path, usize depth,
                         usize max_depth, Option<Rc<Value>> owner = None())
      : values_(values), path_(rstd::move(path)), depth_(depth),
        max_depth_(max_depth), owner_(rstd::move(owner)) {}
  template <class T> auto next() -> rstd::Result<Option<T>, SerdeError> {
    if (index_ == values_.len())
      return Ok(None<T>());
    auto source =
        SnapshotDeserializer(values_[index_], path_.with_index(index_), depth_,
                             max_depth_, false, None(), owner_.clone());
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
  Option<Vec<usize>> indices_;
  DataPath path_;
  usize depth_, max_depth_, index_{};
  bool pending_{};
  Option<Rc<Value>> owner_;
  friend class SnapshotDeserializer;
  auto len() const -> usize {
    return indices_.is_some() ? indices_->len() : entries_.len();
  }
  auto entry_index(usize index) const -> usize {
    return indices_.is_some() ? (*indices_)[index] : index;
  }
  auto entry(usize index) const -> const TableEntry & {
    return entries_[entry_index(index)];
  }

public:
  using error_type = SerdeError;
  SnapshotMapAccess(slice<TableEntry> entries, DataPath path, usize depth,
                    usize max_depth, Option<Vec<usize>> indices = None(),
                    Option<Rc<Value>> owner = None())
      : entries_(entries), indices_(rstd::move(indices)),
        path_(rstd::move(path)), depth_(depth), max_depth_(max_depth),
        owner_(rstd::move(owner)) {}
  template <class T> auto next_key() -> rstd::Result<Option<T>, SerdeError> {
    if (pending_)
      return Err(
          SerdeError::invariant(path_.clone(), "map value is missing"_str));
    if (index_ == len())
      return Ok(None<T>());
    auto key = Value::String(entry(index_).key.clone());
    auto source = SnapshotDeserializer(key, path_.clone(), depth_, max_depth_,
                                       false, None(), owner_.clone());
    auto value = rstd_try(rstd::serde::deserialize<T>(source));
    pending_ = true;
    return Ok(Some(rstd::move(value)));
  }
  template <class T> auto next_value() -> rstd::Result<T, SerdeError> {
    if (!pending_)
      return Err(
          SerdeError::invariant(path_.clone(), "map key is missing"_str));
    const auto &entry = this->entry(index_);
    auto source =
        SnapshotDeserializer(entry.value, path_.with_field(entry.key.as_str()),
                             depth_, max_depth_, false, None(), owner_.clone());
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
    if (pending_ || index_ != len())
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
  Option<Rc<Value>> owner_;

public:
  using error_type = SerdeError;
  SnapshotEnumAccess(String variant, const Value *value, DataPath path,
                     usize depth, usize max_depth, bool adjacent = false,
                     Option<Rc<Value>> owner = None())
      : variant_(rstd::move(variant)), value_(value), path_(rstd::move(path)),
        depth_(depth), max_depth_(max_depth), adjacent_(adjacent),
        owner_(rstd::move(owner)) {}
  auto variant() const -> ref<str> { return variant_.as_str(); }
  auto unit() -> rstd::Result<empty, SerdeError> {
    if (value_ == nullptr)
      return Ok(empty{});
    auto source =
        SnapshotDeserializer(*value_, path_.clone(), depth_, max_depth_, false,
                             None(), owner_.clone());
    return source.deserialize_unit();
  }
  template <class T> auto value() -> rstd::Result<T, SerdeError> {
    if (value_ == nullptr)
      return Err(
          SerdeError::invalid_value(path_.clone(), "missing enum payload"_str));
    auto source = SnapshotDeserializer(
        *value_,
        adjacent_ ? path_.clone() : path_.with_variant(variant_.as_str()),
        depth_, max_depth_, false, None(), owner_.clone());
    return rstd::serde::deserialize<T>(source);
  }
};

class SnapshotInternalEnumAccess {
  String variant_;
  const Table *table_;
  Vec<usize> indices_;
  DataPath path_;
  usize depth_, max_depth_;
  rstd::serde::UnknownFieldPolicy unknown_;
  bool consumed_{};
  Option<Rc<Value>> owner_;

public:
  using error_type = SerdeError;
  SnapshotInternalEnumAccess(String variant, const Table &table,
                             Vec<usize> indices, DataPath path, usize depth,
                             usize max_depth,
                             rstd::serde::UnknownFieldPolicy unknown,
                             Option<Rc<Value>> owner = None())
      : variant_(rstd::move(variant)), table_(&table),
        indices_(rstd::move(indices)), path_(rstd::move(path)), depth_(depth),
        max_depth_(max_depth), unknown_(unknown), owner_(rstd::move(owner)) {}
  auto variant() const &[[clang::lifetimebound]] -> ref<str> {
    return variant_.as_str();
  }
  auto unit() -> rstd::Result<empty, SerdeError> {
    if (consumed_)
      return Err(
          SerdeError::invariant(path_.clone(), "enum already consumed"_str));
    consumed_ = true;
    if (unknown_ == rstd::serde::UnknownFieldPolicy::Reject &&
        !indices_.is_empty())
      return Err(SerdeError::unknown_field(
          path_.clone(), table_->entries()[indices_[usize()]].key.as_str()));
    return Ok(empty{});
  }
  template <class T> auto value() -> rstd::Result<T, SerdeError> {
    if (consumed_)
      return Err(
          SerdeError::invariant(path_.clone(), "enum already consumed"_str));
    consumed_ = true;
    auto source = SnapshotDeserializer(
        *table_, path_.clone(), depth_, max_depth_,
        unknown_ == rstd::serde::UnknownFieldPolicy::Reject,
        Some(rstd::move(indices_)), owner_.clone());
    return rstd::serde::deserialize<T>(source);
  }
};

inline auto SnapshotDeserializer::begin_sequence()
    -> rstd::Result<SnapshotSequenceAccess, SerdeError> {
  if (depth_ >= max_depth_)
    return Err(invalid_value("Lua value nesting is too deep"_str));
  if (map_indices_.is_some())
    return Err(invalid_value("expected a sequence table"_str));
  if (value_ != nullptr && value_->is_Array())
    return Ok(SnapshotSequenceAccess(value_->as_Array().value->values(),
                                     path_.clone(), depth_ + usize(1),
                                     max_depth_, owner_.clone()));
  if (table_ != nullptr && table_->entries().is_empty())
    return Ok(SnapshotSequenceAccess({}, path_.clone(), depth_ + usize(1),
                                     max_depth_, owner_.clone()));
  return Err(invalid_value("expected a sequence table"_str));
}
inline auto SnapshotDeserializer::begin_map()
    -> rstd::Result<SnapshotMapAccess, SerdeError> {
  if (depth_ >= max_depth_)
    return Err(invalid_value("Lua value nesting is too deep"_str));
  if (table_ == nullptr)
    return Err(invalid_value("expected a record table"_str));
  if (map_indices_.is_some() && map_started_)
    return Err(invariant("map already consumed"_str));
  Option<Vec<usize>> indices;
  if (map_indices_.is_some())
    indices = Some(rstd::move(*map_indices_));
  map_started_ = true;
  return Ok(SnapshotMapAccess(table_->entries(), path_.clone(),
                              depth_ + usize(1), max_depth_,
                              rstd::move(indices), owner_.clone()));
}
inline auto SnapshotDeserializer::begin_dictionary()
    -> rstd::Result<SnapshotMapAccess, SerdeError> {
  if (depth_ >= max_depth_)
    return Err(invalid_value("Lua value nesting is too deep"_str));
  if (value_ != nullptr && value_->is_Array() &&
      value_->as_Array().value->is_empty())
    return Ok(SnapshotMapAccess({}, path_.clone(), depth_ + usize(1),
                                max_depth_, None(), owner_.clone()));
  return begin_map();
}

inline auto SnapshotDeserializer::begin_enum()
    -> rstd::Result<SnapshotEnumAccess, SerdeError> {
  if (value_ != nullptr && value_->is_String())
    return Ok(SnapshotEnumAccess(rstd_try(deserialize_string()), nullptr,
                                 path_.clone(), depth_, max_depth_, false,
                                 owner_.clone()));
  if (depth_ >= max_depth_)
    return Err(invalid_value("Lua value nesting is too deep"_str));
  if (table_ == nullptr || table_->entries().len() != usize(1))
    return Err(invalid_value("expected one enum variant"_str));
  const auto &entry = table_->entries()[usize()];
  return Ok(SnapshotEnumAccess(entry.key.clone(), &entry.value, path_.clone(),
                               depth_ + usize(1), max_depth_, false,
                               owner_.clone()));
}
inline auto SnapshotDeserializer::begin_internal_enum(
    ref<str> tag, rstd::serde::UnknownFieldPolicy unknown)
    -> rstd::Result<SnapshotInternalEnumAccess, SerdeError> {
  auto map = rstd_try(begin_map());
  Option<String> variant;
  Vec<usize> indices;
  for (usize index{}; index < map.len(); ++index) {
    const auto &entry = map.entry(index);
    if (entry.key == tag) {
      if (variant.is_some())
        return Err(duplicate_field(tag));
      auto child = SnapshotDeserializer(entry.value, path_.with_field(tag),
                                        depth_ + usize(1), max_depth_, false,
                                        None(), owner_.clone());
      variant = Some(rstd_try(child.deserialize_string()));
    } else {
      indices.push(map.entry_index(index));
    }
  }
  if (variant.is_none())
    return Err(missing_field(tag));
  return Ok(SnapshotInternalEnumAccess(
      rstd::move(*variant), *table_, rstd::move(indices), path_.clone(), depth_,
      max_depth_, unknown, owner_.clone()));
}
inline auto SnapshotDeserializer::begin_adjacent_enum(
    ref<str> tag, ref<str> content, rstd::serde::UnknownFieldPolicy unknown)
    -> rstd::Result<SnapshotEnumAccess, SerdeError> {
  auto map = rstd_try(begin_map());
  Option<String> variant;
  const Value *payload = nullptr;
  for (usize index{}; index < map.len(); ++index) {
    const auto &entry = map.entry(index);
    if (entry.key == tag) {
      auto child = SnapshotDeserializer(entry.value, path_.with_field(tag),
                                        depth_ + usize(1), max_depth_, false,
                                        None(), owner_.clone());
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
                               max_depth_, true, owner_.clone()));
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
      RSTD_CASE_DEFAULT { break; }
    }
  }
  return Err(invalid_value("unsupported Lua value in JSON"_str));
}

static_assert(rstd::serde::InternalVariantDeserializer<ValueDeserializer>);
static_assert(rstd::serde::InternalVariantDeserializer<SnapshotDeserializer>);
static_assert(rstd::serde::CapturingDeserializer<ValueDeserializer>);
static_assert(rstd::serde::CapturingDeserializer<SnapshotDeserializer>);

template <class T>
auto serde_result(rstd::Result<T, SerdeError> result) -> Result<T> {
  if (result.is_err()) {
    auto error = rstd::move(result).unwrap_err();
    auto message = rstd::format("{}", error);
    return Err(Error::make(ErrorKind::Type, String::make("luato:serde"_str),
                           rstd::move(message))
                   .with_cause(rstd::move(error)));
  }
  return Ok(rstd::move(result).unwrap());
}

template <class T, class Deserializer>
auto deserialize_value(Deserializer source) -> Result<T> {
  return serde_result(rstd::serde::deserialize<T>(source));
}

template <class T>
  requires rstd::serde::Serializable<T>
auto to_value(State &state, const T &value, SerializeOptions options = {})
    -> Result<LuaValue> {
  auto sink = ValueSerializer(state, options);
  rstd_try(state.validate_value(LuaValue::Nil()));
  return serde_result(rstd::serde::serialize(sink, value));
}
template <class T>
  requires rstd::serde::Serializable<T>
auto to_value(CallFrame &frame, const T &value, SerializeOptions options = {})
    -> Result<LuaValue> {
  auto sink = ValueSerializer(frame, options);
  rstd_try(frame.validate_value(LuaValue::Nil()));
  return serde_result(rstd::serde::serialize(sink, value));
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
