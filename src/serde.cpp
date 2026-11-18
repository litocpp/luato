module;
#include <rstd/enum.hpp>
#include <rstd/macro.hpp>

module luato;
import :serde;

using alloc::rc::make_rc;
using rstd::serde::DataPath;
using SerdeError = rstd::serde::Error;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace luato {
auto make_serde_host_error(rstd::serde::DataPath path, ref<str> message,
                           Error source) -> rstd::serde::Error {
  return rstd::serde::Error::invalid_value_with_source(
      rstd::move(path), message, rstd::move(source));
}

auto ValueDeserializer::capture_value()
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

auto CapturedLuaValue::replay() const -> ValueDeserializer {
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

auto ValueDeserializer::begin_internal_enum(
    ref<str> tag, rstd::serde::UnknownFieldPolicy unknown)
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
auto ValueDeserializer::begin_enum()
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

auto ValueDeserializer::begin_adjacent_enum(
    ref<str> tag, ref<str> content, rstd::serde::UnknownFieldPolicy unknown)
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

auto ValueDeserializer::deserialize_json_value()
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

auto SnapshotDeserializer::capture_value()
    -> rstd::Result<CapturedSnapshotValue, SerdeError> {
  if (map_indices_.is_some() && map_started_)
    return Err(invariant("map already consumed"_str));
  auto value = value_ ? value_->clone() : Value::Table(table_->clone());
  return Ok(CapturedSnapshotValue(
      make_rc<Value>(rstd::move(value)), path_.clone(), depth_, max_depth_,
      reject_unknown_fields_, map_indices_.clone()));
}

auto CapturedSnapshotValue::replay() const -> SnapshotDeserializer {
  return SnapshotDeserializer(*owner_, path_.clone(), depth_, max_depth_,
                              reject_unknown_fields_, map_indices_.clone(),
                              Some(owner_.clone()));
}

auto SnapshotDeserializer::begin_enum()
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
auto SnapshotDeserializer::begin_internal_enum(
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
auto SnapshotDeserializer::begin_adjacent_enum(
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

auto SnapshotDeserializer::deserialize_json_value()
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
} // namespace luato
