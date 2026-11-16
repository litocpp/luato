#include <rstd/enum.hpp>
import luato;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using Dictionary = alloc::collections::BTreeMap<String, i64>;

class SerdeChoice {
  RSTD_ENUM(SerdeChoice, (Empty), (Items, (Vec<Option<i64>> items;)))
};
constexpr auto rstd_serde_definition(SerdeChoice::Items_payload *) {
  using T = SerdeChoice::Items_payload;
  return rstd::serde::NewtypeDefinition<
      T, "Items", rstd::serde::Direction::Both, &T::items>{};
}
constexpr auto rstd_serde_definition(SerdeChoice *) {
  return rstd::serde::VariantDefinition<SerdeChoice, "SerdeChoice",
                                        rstd::serde::Direction::Both>{};
}

class SerdeOutcome {
  RSTD_ENUM(SerdeOutcome, (Empty), (Data, (SerdeChoice value;)))
};
constexpr auto rstd_serde_definition(SerdeOutcome::Data_payload *) {
  using T = SerdeOutcome::Data_payload;
  return rstd::serde::NewtypeDefinition<T, "Data", rstd::serde::Direction::Both,
                                        &T::value>{};
}

constexpr auto rstd_serde_definition(SerdeOutcome *) {
  return rstd::serde::AdjacentVariantDefinition<SerdeOutcome, "SerdeOutcome",
                                                rstd::serde::Direction::Both,
                                                "type", "content">{};
}

struct SerdeRequest {
  String name;
  Option<String> optional;
  Vec<Option<i64>> items;
  Dictionary attributes;
  Vec<SerdeOutcome> outcomes;
};
constexpr auto rstd_serde_definition(SerdeRequest *) {
  using namespace rstd::serde;
  return RecordDefinition<
      SerdeRequest, "SerdeRequest", Direction::Both, UnknownFieldPolicy::Reject,
      Member<&SerdeRequest::name, "name">,
      Member<&SerdeRequest::optional, "optional", FieldPolicy::Defaulted>,
      Member<&SerdeRequest::items, "items">,
      Member<&SerdeRequest::attributes, "attributes">,
      Member<&SerdeRequest::outcomes, "outcomes">>{};
}

struct SerdeUnsigned {
  Vec<u64> numbers;
};

class SerdeInternal {
  RSTD_ENUM(SerdeInternal, (Empty), (Data, (SerdeRequest value;)))
};
constexpr auto rstd_serde_definition(SerdeInternal::Data_payload *) {
  using T = SerdeInternal::Data_payload;
  return rstd::serde::NewtypeDefinition<T, "Data", rstd::serde::Direction::Both,
                                        &T::value>{};
}
constexpr auto rstd_serde_definition(SerdeInternal *) {
  return rstd::serde::InternalVariantDefinition<
      SerdeInternal, "SerdeInternal", rstd::serde::Direction::Both, "kind">{};
}

struct SerdeLeaf {
  String kind;
  Vec<i64> numbers;
};
constexpr auto rstd_serde_definition(SerdeLeaf *) {
  using namespace rstd::serde;
  return RecordDefinition<SerdeLeaf, "SerdeLeaf", Direction::Both,
                          UnknownFieldPolicy::Ignore,
                          Member<&SerdeLeaf::kind, "kind">,
                          Member<&SerdeLeaf::numbers, "numbers">>{};
}
struct SerdeLoosePayload {
  SerdeLeaf nested;
};
constexpr auto rstd_serde_definition(SerdeLoosePayload *) {
  using namespace rstd::serde;
  return RecordDefinition<SerdeLoosePayload, "SerdeLoosePayload",
                          Direction::Both, UnknownFieldPolicy::Ignore,
                          Member<&SerdeLoosePayload::nested, "nested">>{};
}
struct SerdeEmptyRecord {};
constexpr auto rstd_serde_definition(SerdeEmptyRecord *) {
  using namespace rstd::serde;
  return RecordDefinition<SerdeEmptyRecord, "SerdeEmptyRecord", Direction::Both,
                          UnknownFieldPolicy::Ignore>{};
}
class SerdeLooseInternal {
  RSTD_ENUM(SerdeLooseInternal, (Empty), (Data, (SerdeLoosePayload value;)))
};
constexpr auto rstd_serde_definition(SerdeLooseInternal::Data_payload *) {
  using T = SerdeLooseInternal::Data_payload;
  return rstd::serde::NewtypeDefinition<T, "Data", rstd::serde::Direction::Both,
                                        &T::value>{};
}
constexpr auto rstd_serde_definition(SerdeLooseInternal *) {
  return rstd::serde::InternalVariantDefinition<
      SerdeLooseInternal, "SerdeLooseInternal", rstd::serde::Direction::Both,
      "kind", rstd::serde::UnknownFieldPolicy::Ignore>{};
}
constexpr auto rstd_serde_definition(SerdeUnsigned *) {
  using namespace rstd::serde;
  return RecordDefinition<SerdeUnsigned, "SerdeUnsigned", Direction::Serialize,
                          UnknownFieldPolicy::Reject,
                          Member<&SerdeUnsigned::numbers, "numbers">>{};
}

struct SerdeBytes {
  Vec<u8> value;
};
namespace rstd {
template <> struct Impl<serde::Serialize, SerdeBytes> {
  template <class S>
  static auto serialize(S &sink, const SerdeBytes &value) ->
      typename S::result_type {
    return sink.serialize_bytes(value.value.as_slice());
  }
};
template <> struct Impl<serde::Deserialize, SerdeBytes> {
  template <class D>
  static auto deserialize(D &source)
      -> Result<SerdeBytes, typename D::error_type> {
    auto bytes = source.deserialize_bytes();
    if (bytes.is_err())
      return Err(rstd::move(bytes).unwrap_err());
    return Ok(SerdeBytes{rstd::move(*bytes)});
  }
};
} // namespace rstd

namespace {
struct Checks {
  int failures{};
  int assertions{};
  void expect(bool value, const char *message) {
    ++assertions;
    if (!value) {
      ++failures;
      __builtin_printf("FAIL: serde write: %s\n", message);
    }
  }
  template <class T> void error(const luato::Result<T> &value, ref<str> path) {
    expect(value.is_err(), "reject invalid serialization");
    if (value.is_err()) {
      auto &error = value.as_ref().unwrap_err();
      expect(error.kind == luato::ErrorKind::Type, "retain error kind");
      expect(error.message.as_str().contains(path), "retain field path");
    }
  }
};

auto source(ref<str> text) -> luato::LuaModuleSource {
  return {"serde"_Str, "test:serde"_Str, "serde.lua"_Str,
          Vec<u8>::from(text.as_bytes())};
}
auto key(ref<str> text) -> luato::LuaValue {
  return luato::LuaValue::Bytes(Vec<u8>::from(text.as_bytes()));
}
auto request() -> SerdeRequest {
  return {"\xe4\xb8\xad\xe6\x96\x87"_Str, None(), {}, {}, {}};
}

void scalars(Checks &checks, luato::State &state) {
  for (auto number : {i64::MIN, i64::MAX, i64(9007199254740993LL)}) {
    auto value = luato::to_value(state, number).unwrap();
    checks.expect(value.is_Integer() && value.as_Integer().value == number,
                  "preserve signed integer precision");
    checks.expect(luato::from_value<i64>(value).unwrap() == number,
                  "reuse typed integer reader");
  }
  auto maximum = luato::to_value(state, rstd::as_cast<u64>(i64::MAX)).unwrap();
  checks.expect(maximum.as_Integer().value == i64::MAX,
                "preserve unsigned integer in Lua range");
  checks.error(luato::to_value(state, u64::MAX), "Lua range"_str);
  checks.expect(luato::to_value(state, true).unwrap().as_Boolean().value,
                "serialize boolean");
  checks.expect(luato::to_value(state, f64(1.25)).unwrap().as_Number().value ==
                    f64(1.25),
                "serialize floating point");
  checks.expect(luato::to_value(state, f64(__builtin_inf()))
                        .unwrap()
                        .as_Number()
                        .value.to_primitive() == __builtin_inf(),
                "Lua numbers are not restricted to JSON numbers");
  auto bytes = Vec<u8>::make();
  bytes.push(u8(0xff));
  bytes.push(u8());
  auto value = luato::to_value(state, SerdeBytes{rstd::move(bytes)}).unwrap();
  auto decoded = luato::from_value<SerdeBytes>(value).unwrap();
  checks.expect(decoded.value.len() == usize(2) &&
                    decoded.value[usize()] == u8(0xff) &&
                    decoded.value[usize(1)] == u8(),
                "serialize bytes without UTF-8 or array conversion");
  auto null = luato::to_value(state, None<i64>()).unwrap();
  checks.expect(null.is_Opaque() && null.as_Opaque().value == nullptr,
                "default null matches the serde reader");
  checks.expect(luato::from_value<Option<i64>>(null).unwrap().is_none(),
                "read default null");
  checks.expect(luato::to_value(state, empty{}, luato::SerializeOptions::lua())
                    .unwrap()
                    .is_Nil(),
                "Lua mode unit is nil");
}

void collections(Checks &checks, luato::State &state,
                 luato::SerializeOptions options) {
  options = luato::SerializeOptions::from_deserialize_options(
      options.deserialize_options());
  auto input = request();
  auto value = luato::to_value(state, input, options).unwrap();
  auto read_options = options.deserialize_options();
  auto output = luato::from_value<SerdeRequest>(value, read_options).unwrap();
  checks.expect(output.name == input.name && output.optional.is_none() &&
                    output.items.is_empty() && output.attributes.is_empty() &&
                    output.outcomes.is_empty(),
                "round trip typed empty collections, null and UTF-8");
  auto json =
      luato::from_value<rstd::json::Value>(value, read_options).unwrap();
  checks.expect((*json.get("items"_str))->is_Array() &&
                    (*json.get("attributes"_str))->is_Object() &&
                    (*json.get("optional"_str))->is_null(),
                "markers preserve empty array, empty object and null");
  auto &table = value.as_Table().value;
  auto items = table.raw_get_value("items"_str).unwrap();
  auto object = table.raw_get_value("attributes"_str).unwrap();
  checks.expect(luato::from_value<Dictionary>(items, read_options).is_err() &&
                    luato::from_value<Vec<i64>>(object, read_options).is_err(),
                "marked collection kinds are not interchangeable");

  Vec<Option<i64>> nested;
  nested.push(Some(i64(17)));
  nested.push(None());
  input.outcomes.push(
      SerdeOutcome::Data(SerdeChoice::Items(rstd::move(nested))));
  input.outcomes.push(SerdeOutcome::Empty());
  input.items.push(None());
  input.items.push(Some(i64(i64::MIN)));
  value = luato::to_value(state, input, options).unwrap();
  output = luato::from_value<SerdeRequest>(value, read_options).unwrap();
  auto &choice = output.outcomes[usize()].as_Data().value;
  checks.expect(output.items.len() == usize(2) &&
                    output.items[usize()].is_none() &&
                    choice.as_Items().items[usize(1)].is_none() &&
                    output.outcomes[usize(1)].is_Empty(),
                "round trip nested external and adjacent enums with null");

  value = luato::to_value(state, request(),
                          luato::SerializeOptions::lua(read_options))
              .unwrap();
  auto fields = value.as_Table().value.keys().unwrap();
  checks.expect(fields.len() == usize(4), "Lua nil optional field is omitted");
  auto lua_items = value.as_Table().value.raw_get_value("items"_str).unwrap();
  checks.expect(lua_items.as_Table()
                    .value.metatable()
                    .unwrap()
                    .unwrap()
                    .same_object(*options.array_metatable)
                    .unwrap(),
                "Lua nil strategy retains collection markers");
  options.serialize_none_to_null = false;
  value = luato::to_value(state, request(), options).unwrap();
  checks.expect(
      value.as_Table().value.raw_get_value("optional"_str).unwrap().is_Nil(),
      "none strategy does not depend on a configured sentinel");
  checks.expect(luato::to_value(state, empty{}, options).unwrap().is_Table(),
                "unit strategy is independent of none strategy");
  Vec<SerdeInternal> internal;
  internal.push(SerdeInternal::Data(request()));
  internal.push(SerdeInternal::Empty());
  value = luato::to_value(state, internal, options).unwrap();
  auto tagged =
      luato::from_value<rstd::json::Value>(value, read_options).unwrap();
  auto &first = (*tagged.as_array().unwrap())[usize()];
  checks.expect((*first.get("kind"_str))->as_str().unwrap() == "Data"_str &&
                    (*first.get("items"_str))->is_Array(),
                "serialize nested internal enums through rstd map protocol");
}

void null_options(Checks &checks, luato::State &state,
                  luato::SerializeOptions options) {
  for (unsigned flags = 0; flags < 8; ++flags) {
    options.serialize_none_to_null = (flags & 1) != 0;
    options.serialize_unit_to_null = (flags & 2) != 0;
    options.serialize_json_null_to_null = (flags & 4) != 0;
    auto sink = luato::ValueSerializer(state, options);
    checks.expect(sink.serialize_none().unwrap().is_Table() ==
                      options.serialize_none_to_null,
                  "none strategy is independent");
    checks.expect(sink.serialize_unit().unwrap().is_Table() ==
                      options.serialize_unit_to_null,
                  "unit strategy is independent");
    checks.expect(luato::to_value(state, rstd::json::Value::Null(), options)
                          .unwrap()
                          .is_Table() == options.serialize_json_null_to_null,
                  "JSON null hook strategy is independent");
  }
  auto input = rstd::json::Map::make();
  input.insert("null"_Str, rstd::json::Value::Null());
  auto value =
      luato::to_value(state, input,
                      luato::SerializeOptions::from_deserialize_options(
                          options.deserialize_options()))
          .unwrap();
  checks.expect(value.as_Table()
                    .value.raw_get_value("null"_str)
                    .unwrap()
                    .as_Table()
                    .value.same_object(*options.null_value)
                    .unwrap(),
                "serialize JSON null in a map without a JSON intermediate");
}

void internal_enums(Checks &checks, luato::State &state,
                    luato::SerializeOptions options) {
  auto input = request();
  input.items.push(None());
  Vec<Option<i64>> numbers;
  numbers.push(None());
  input.outcomes.push(
      SerdeOutcome::Data(SerdeChoice::Items(rstd::move(numbers))));
  auto value =
      luato::to_value(state, SerdeInternal::Data(rstd::move(input))).unwrap();
  auto live = luato::from_value<SerdeInternal>(value).unwrap();
  auto snapshot = state.snapshot(value).unwrap();
  auto copied = luato::from_value<SerdeInternal>(snapshot).unwrap();
  for (const auto *decoded : {&live, &copied}) {
    checks.expect(decoded->is_Data() &&
                      decoded->as_Data().value.items[usize()].is_none(),
                  "internal enum round trip preserves null and record fields");
    auto &nested = decoded->as_Data().value.outcomes[usize()].as_Data().value;
    checks.expect(nested.as_Items().items[usize()].is_none(),
                  "internal enum round trip preserves nested enums");
  }
  auto unit = luato::to_value(state, SerdeInternal::Empty()).unwrap();
  checks.expect(luato::from_value<SerdeInternal>(unit).unwrap().is_Empty(),
                "live internal unit round trip");
  checks.expect(luato::from_value<SerdeInternal>(state.snapshot(unit).unwrap())
                    .unwrap()
                    .is_Empty(),
                "snapshot internal unit round trip");
  checks.expect(
      luato::from_value<SerdeInternal>(luato::LuaValue::Nil()).is_err() &&
          luato::from_value<SerdeInternal>(luato::Value::Nil()).is_err(),
      "nil is not an internal unit variant");
  auto null = luato::to_value(state, empty{}, options).unwrap();
  checks.expect(
      luato::from_value<SerdeInternal>(null, options.deserialize_options())
              .is_err() &&
          luato::from_value<SerdeInternal>(luato::Value::Opaque(nullptr))
              .is_err(),
      "null is not an internal unit variant");

  auto values =
      state
          .evaluate_entry(source(
              "return "
              "{kind='Data',nested={kind='kept',numbers={7},extra=true},extra="
              "true}, "
              "{kind='Data',nested={kind='kept',numbers={7},extra=true}}, "
              "{{kind='Data',nested={kind='kept',numbers={1,false}}}}, "
              "{kind='Empty',extra=null}, {kind=null}, {}, {kind='credential-marker'}"_str))
          .unwrap()
          .into_values();
  auto loose = luato::from_value<SerdeLooseInternal>(values[usize()]).unwrap();
  checks.expect(loose.as_Data().value.nested.kind == "kept"_str,
                "tag filtering is restricted to the payload root");
  using Strict = rstd::serde::InternalVariantDefinition<
      SerdeLooseInternal, "StrictInternal", rstd::serde::Direction::Both,
      "kind", rstd::serde::UnknownFieldPolicy::Reject>;
  auto strict_live = luato::ValueDeserializer(values[usize()]);
  checks.error(luato::serde_result(Strict::deserialize(strict_live)),
               "extra"_str);
  auto extra_snapshot = state.snapshot(values[usize()]).unwrap();
  auto strict_snapshot = luato::SnapshotDeserializer(extra_snapshot);
  checks.error(luato::serde_result(Strict::deserialize(strict_snapshot)),
               "extra"_str);
  auto nested_live = luato::ValueDeserializer(values[usize(1)]);
  checks.expect(Strict::deserialize(nested_live).is_ok(),
                "outer Reject does not override nested record policy");
  auto nested_snapshot = state.snapshot(values[usize(1)]).unwrap();
  auto nested_source = luato::SnapshotDeserializer(nested_snapshot);
  checks.expect(Strict::deserialize(nested_source).is_ok(),
                "snapshot nested record policy is preserved");
  checks.error(luato::from_value<Vec<SerdeLooseInternal>>(values[usize(2)]),
               "[0].nested.numbers[1]"_str);
  checks.error(luato::from_value<Vec<SerdeLooseInternal>>(
                   state.snapshot(values[usize(2)]).unwrap()),
               "[0].nested.numbers[1]"_str);

  auto read_options = options.deserialize_options();
  checks.error(luato::from_value<SerdeInternal>(values[usize(3)], read_options),
               "extra"_str);
  checks.expect(
      luato::from_value<SerdeLooseInternal>(values[usize(3)], read_options)
          .unwrap()
          .is_Empty(),
      "unit Ignore accepts an explicit null extra field");
  auto unit_extra = luato::Table::make();
  unit_extra.set("kind"_Str, "Empty"_Str).unwrap();
  unit_extra.insert("extra"_Str, luato::Value::Nil()).unwrap();
  checks.error(luato::from_value<SerdeInternal>(unit_extra), "extra"_str);
  checks.expect(
      luato::from_value<SerdeLooseInternal>(unit_extra).unwrap().is_Empty(),
      "snapshot unit Ignore accepts an explicit null extra field");
  checks.error(luato::from_value<SerdeInternal>(values[usize(4)], read_options),
               "kind"_str);
  auto null_tag = luato::Table::make();
  null_tag.insert("kind"_Str, luato::Value::Nil()).unwrap();
  checks.error(luato::from_value<SerdeInternal>(null_tag), "kind"_str);
  checks.error(luato::from_value<SerdeInternal>(values[usize(5)]), "kind"_str);
  checks.error(luato::from_value<SerdeInternal>(luato::Table::make()),
               "kind"_str);
  auto unknown = luato::from_value<SerdeInternal>(values[usize(6)]);
  checks.expect(unknown.is_err() &&
                    !unknown.as_ref().unwrap_err().message.as_str().contains(
                        "credential-marker"_str),
                "unknown internal tags are not echoed");
  Vec<luato::TableEntry> duplicate;
  duplicate.push({"kind"_Str, luato::Value::String("Empty"_Str)});
  duplicate.push({"kind"_Str, luato::Value::String("Empty"_Str)});
  checks.error(luato::from_value<SerdeInternal>(luato::Table::with_path(
                   String::make(), rstd::move(duplicate))),
               "kind"_str);

  auto access = [&] {
    auto source = luato::ValueDeserializer(value);
    return source
        .begin_internal_enum("kind"_str,
                             rstd::serde::UnknownFieldPolicy::Reject)
        .unwrap();
  }();
  value = luato::LuaValue::Nil();
  auto moved = rstd::move(access);
  checks.expect(
      moved.value<SerdeRequest>().is_ok(),
      "live access owns its table and filtered keys after source destruction");
  checks.expect(moved.value<SerdeRequest>().is_err(),
                "internal access is consumed once");
  auto snapshot_access = [&] {
    auto source = luato::SnapshotDeserializer(snapshot);
    return source
        .begin_internal_enum("kind"_str,
                             rstd::serde::UnknownFieldPolicy::Reject)
        .unwrap();
  }();
  auto moved_snapshot = rstd::move(snapshot_access);
  checks.expect(
      moved_snapshot.value<SerdeRequest>().is_ok(),
      "snapshot access owns indices without borrowing a temporary source");
  checks.expect(moved_snapshot.unit().is_err(),
                "snapshot access is consumed once");
  auto empty_source = luato::ValueDeserializer(values[usize(1)]);
  auto empty_access =
      empty_source
          .begin_internal_enum("kind"_str,
                               rstd::serde::UnknownFieldPolicy::Reject)
          .unwrap();
  checks.expect(empty_access.value<SerdeEmptyRecord>().is_err(),
                "Reject reaches records without declared fields");
  auto empty_snapshot_source = luato::SnapshotDeserializer(nested_snapshot);
  auto empty_snapshot_access =
      empty_snapshot_source
          .begin_internal_enum("kind"_str,
                               rstd::serde::UnknownFieldPolicy::Reject)
          .unwrap();
  checks.expect(empty_snapshot_access.value<SerdeEmptyRecord>().is_err(),
                "snapshot Reject reaches records without declared fields");
  auto views =
      state
          .evaluate_entry(source(
              "return {kind='Data',item=7}, {kind='Data',type='Data',content={Items={4}}}"_str))
          .unwrap()
          .into_values();
  auto map_source = luato::ValueDeserializer(views[usize()]);
  auto map_access = map_source
                        .begin_internal_enum(
                            "kind"_str, rstd::serde::UnknownFieldPolicy::Reject)
                        .unwrap();
  auto dictionary = map_access.value<Dictionary>().unwrap();
  checks.expect(dictionary.len() == usize(1) &&
                    *dictionary.get("item"_Str).unwrap() == i64(7),
                "live filtered payload supports generic maps");
  auto map_snapshot = state.snapshot(views[usize()]).unwrap();
  auto snapshot_map_source = luato::SnapshotDeserializer(map_snapshot);
  auto snapshot_map =
      snapshot_map_source
          .begin_internal_enum("kind"_str,
                               rstd::serde::UnknownFieldPolicy::Reject)
          .unwrap();
  checks.expect(snapshot_map.value<Dictionary>().unwrap().len() == usize(1),
                "snapshot filtered payload supports generic maps");
  auto adjacent_source = luato::ValueDeserializer(views[usize(1)]);
  auto adjacent_access =
      adjacent_source
          .begin_internal_enum("kind"_str,
                               rstd::serde::UnknownFieldPolicy::Reject)
          .unwrap();
  checks.expect(adjacent_access.value<SerdeOutcome>().is_ok(),
                "adjacent enum consumes the filtered live map");
  auto adjacent_snapshot = state.snapshot(views[usize(1)]).unwrap();
  auto adjacent_snapshot_source =
      luato::SnapshotDeserializer(adjacent_snapshot);
  auto adjacent_snapshot_access =
      adjacent_snapshot_source
          .begin_internal_enum("kind"_str,
                               rstd::serde::UnknownFieldPolicy::Reject)
          .unwrap();
  checks.expect(adjacent_snapshot_access.value<SerdeOutcome>().is_ok(),
                "adjacent enum consumes the filtered snapshot map");
  auto sequence_source = luato::ValueDeserializer(unit);
  auto sequence_access =
      sequence_source
          .begin_internal_enum("kind"_str,
                               rstd::serde::UnknownFieldPolicy::Reject)
          .unwrap();
  checks.expect(sequence_access.value<Vec<i64>>().is_err(),
                "internal map payload is not a sequence");
}

void callbacks(Checks &checks, luato::State &state,
               luato::SerializeOptions options) {
  auto module = luato::ModuleSpec("serde_host"_Str);
  module.raw_function(
      "internal"_Str, usize(1),
      [options](luato::CallFrame &frame) -> luato::BindingResult {
        auto input = luato::argument<luato::LuaValue>(frame, usize());
        if (input.is_err())
          return Err(rstd::move(input).unwrap_err());
        auto decoded = luato::from_value<SerdeLooseInternal>(
            *input, options.deserialize_options());
        if (decoded.is_err())
          return Err(rstd::move(decoded).unwrap_err());
        return luato::return_value(frame,
                                   luato::to_value(frame, *decoded, options));
      });
  auto large = Vec<i64>::make();
  for (usize i{}; i < usize(16384); ++i)
    large.push(i64(7));
  bool reached{};
  module.raw_function(
      "large"_Str, usize(),
      [large = rstd::move(large),
       &reached](luato::CallFrame &frame) -> luato::BindingResult {
        reached = true;
        return luato::return_value(frame, luato::to_value(frame, large));
      });
  module.raw_function(
      "get"_Str, usize(),
      [options](luato::CallFrame &frame) -> luato::BindingResult {
        return luato::return_value(frame,
                                   luato::to_value(frame, request(), options));
      });
  module.raw_function(
      "nil_value"_Str, usize(),
      [options](luato::CallFrame &frame) -> luato::BindingResult {
        return luato::return_value(
            frame, luato::to_value(frame, None<SerdeInternal>(),
                                   luato::SerializeOptions::lua(
                                       options.deserialize_options())));
      });
  checks.expect(state.register_module(rstd::move(module)).is_ok(),
                "register callback serializer");
  checks.expect(
      state
          .execute_entry(source(
              "local "
              "value=serde_host.internal{kind='Data',nested={kind='kept',"
              "numbers={3}}}; "
              "assert(value.kind=='Data' and value.nested.kind=='kept' and value.nested.numbers[1]==3)"_str))
          .is_ok(),
      "internal enum round trip inside an active CallFrame");
  auto result = state.evaluate_entry(
      source("local a=serde_host.get(); local b=serde_host.get(); "
             "assert(a.optional == null and #a.items == 0); "
             "assert(getmetatable(a.items) == array_meta); "
             "assert(getmetatable(a.attributes) == object_meta); "
             "assert(select('#', serde_host.nil_value()) == 1); "
             "assert(serde_host.nil_value() == nil); return a,b"_str));
  checks.expect(result.is_ok(), "serialize from active CallFrame repeatedly");
  if (result.is_ok())
    checks.expect(luato::from_value<SerdeRequest>(result->values()[usize()],
                                                  options.deserialize_options())
                      .is_ok(),
                  "read callback value through existing reader");
  auto async_callable =
      state
          .evaluate_entry(
              source("return function() return serde_host.get() end"_str))
          .unwrap()
          .into_values();
  auto runtime = rstd::async::RuntimeBuilder::current_thread()
                     .enable_all()
                     .build()
                     .unwrap();
  auto async_result = runtime.block_on(rstd::async::into_future(
      async_callable[usize()].as_Function().value.call_async(
          luato::MultiValue::make())));
  checks.expect(async_result.is_ok(), "serialize CallFrame on a Lua coroutine");
  if (async_result.is_ok())
    checks.expect(
        luato::from_value<SerdeRequest>(async_result->values()[usize()],
                                        options.deserialize_options())
            .is_ok(),
        "read coroutine callback value through existing reader");

  auto getter = luato::UserDataSpec<i64>("SerdeBindings"_Str);
  getter.raw_method("get"_Str,
                    [options](luato::CallFrame &frame) -> luato::BindingResult {
                      return luato::return_value(
                          frame, luato::to_value(frame, request(), options));
                    });
  state.register_userdata(rstd::move(getter)).unwrap();
  auto bindings = state.create_userdata(i64()).unwrap();
  state.globals()
      .unwrap()
      .set(key("bindings"_str), luato::LuaValue::UserData(rstd::move(bindings)))
      .unwrap();
  checks.expect(
      state.execute_entry(source("assert(bindings:get().optional == null)"_str))
          .is_ok(),
      "serialize bindings getter from a userdata callback");
  auto callable =
      state
          .evaluate_entry(
              source("return function() return serde_host.large() end"_str))
          .unwrap()
          .into_values();
  state.set_memory_limit(state.used_memory().unwrap() + usize(8192)).unwrap();
  auto limited = callable[usize()].as_Function().value.call_values(
      luato::MultiValue::make());
  state.set_memory_limit(usize()).unwrap();
  checks.expect(
      reached && limited.is_err(),
      "CallFrame table growth failure returns through protected Lua call");
  checks.expect(state
                    .execute_entry(
                        source("assert(serde_host.get().optional == null)"_str))
                    .is_ok(),
                "CallFrame remains usable after allocation failure");
}

void sequence_reads(Checks &checks, luato::State &state) {
  for (auto text :
       array<ref<str>, 5>{"return {[2]=1}"_str, "return {1,nil,3}"_str,
                          "return {1,extra=2}"_str, "return {[0]=1}"_str,
                          "return {value=1}"_str}) {
    auto values = state.evaluate_entry(source(text)).unwrap();
    checks.error(luato::from_value<Vec<i64>>(values.values()[usize()]),
                 "sequence table"_str);
  }
  for (auto text : array<ref<str>, 2>{"return {}"_str, "return {1,2,3}"_str}) {
    auto values = state.evaluate_entry(source(text)).unwrap();
    checks.expect(luato::from_value<Vec<i64>>(values.values()[usize()]).is_ok(),
                  "accept empty and contiguous sequences");
  }
  auto values = state
                    .evaluate_entry(
                        source("return {kind='leaf',numbers={1,extra=2}}"_str))
                    .unwrap();
  checks.error(luato::from_value<SerdeLeaf>(values.values()[usize()]),
               "numbers"_str);
}

void capacities(Checks &checks, luato::State &state,
                luato::SerializeOptions options) {
  auto sink = luato::ValueSerializer(state, options);
  auto invalid = usize(__INT_MAX__) + usize(1);
  auto sequence = sink.begin_sequence(invalid);
  auto map = sink.begin_map(invalid);
  checks.expect(sequence.is_err() && map.is_err(),
                "serde forwards capacity range errors");
  state.set_memory_limit(state.used_memory().unwrap() + usize(8192)).unwrap();
  sequence = sink.begin_sequence(usize(16384));
  map = sink.begin_map(usize(16384));
  state.set_memory_limit(usize()).unwrap();
  checks.expect(sequence.is_err() && map.is_err(),
                "serde reserves sequence and map storage at begin");
  auto empty_sequence = sink.begin_sequence(usize()).unwrap().end().unwrap();
  auto empty_map = sink.begin_map(usize()).unwrap().end().unwrap();
  auto empty_record =
      luato::to_value(state, SerdeEmptyRecord{}, options).unwrap();
  auto read_options = options.deserialize_options();
  checks.expect(
      luato::from_value<rstd::json::Value>(empty_sequence, read_options)
              .unwrap()
              .is_Array() &&
          luato::from_value<rstd::json::Value>(empty_map, read_options)
              .unwrap()
              .is_Object() &&
          luato::from_value<rstd::json::Value>(empty_record, read_options)
              .unwrap()
              .is_Object(),
      "zero capacities retain array, map and struct markers");
  checks.expect(luato::to_value(state, request(), options).is_ok(),
                "serde remains usable after capacity failures");
}

void failures(Checks &checks, luato::State &state,
              luato::SerializeOptions options) {
  Vec<u64> numbers;
  numbers.push(u64(1));
  numbers.push(u64(u64::MAX));
  checks.error(luato::to_value(state, SerdeUnsigned{rstd::move(numbers)}),
               "numbers[1]"_str);
  options.max_depth = usize(1);
  checks.error(luato::to_value(state, request(), options), "items"_str);
  options.max_depth = usize();
  checks.expect(luato::to_value(state, i64(1), options).is_ok(),
                "depth limit permits scalars");
  checks.error(luato::to_value(state, request(), options), "nesting"_str);
  auto foreign = luato::State::create(luato::StateOptions::base()).unwrap();
  auto marker = foreign.create_table().unwrap();
  options = {};
  options.null_value = &marker;
  checks.error(luato::to_value(state, None<i64>(), options),
               "cannot write"_str);
  options = {};
  options.array_metatable = &marker;
  checks.error(luato::to_value(state, Vec<i64>{}, options), "cannot write"_str);

  auto sink = luato::ValueSerializer(state);
  auto map = sink.begin_map(usize(1)).unwrap();
  checks.expect(map.value(true).is_err(), "reject map value without key");
  map.key("key"_str).unwrap();
  checks.expect(map.key("second"_str).is_err() && map.end().is_err(),
                "reject unfinished map protocol");
  map.value(true).unwrap();
  map.end().unwrap();
  checks.expect(map.end().is_err(), "reject repeated map end");
  state.set_memory_limit(usize(1)).unwrap();
  checks.expect(luato::to_value(state, Vec<i64>{}).is_err(),
                "table allocation failure returns an error");
  state.set_memory_limit(usize()).unwrap();
  checks.expect(luato::to_value(state, request()).is_ok(),
                "state remains usable after serialization errors");
  auto moved = rstd::move(foreign);
  checks.expect(luato::to_value(foreign, i64(1)).is_err(),
                "reject a moved State even for scalars");
}
} // namespace

auto expect_serde_write_contract() -> int {
  Checks checks;
  auto state = luato::State::create(luato::StateOptions::base()).unwrap();
  auto markers =
      state
          .evaluate_entry(source("null={}; array_meta={}; object_meta={}; "
                                 "return null, array_meta, object_meta"_str))
          .unwrap();
  luato::SerializeOptions options;
  options.null_value = &markers.values()[usize()].as_Table().value;
  options.array_metatable = &markers.values()[usize(1)].as_Table().value;
  options.object_metatable = &markers.values()[usize(2)].as_Table().value;
  scalars(checks, state);
  collections(checks, state, options);
  null_options(checks, state, options);
  internal_enums(checks, state, options);
  callbacks(checks, state, options);
  sequence_reads(checks, state);
  capacities(checks, state, options);
  failures(checks, state, options);
  __builtin_printf("serde write: %d assertions, %d failures\n",
                   checks.assertions, checks.failures);
  return checks.failures == 0 ? 0 : 1;
}
