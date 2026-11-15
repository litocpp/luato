#include <rstd/enum.hpp>
import luato;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::serde::DataPath;
using Dictionary = alloc::collections::BTreeMap<String, i64>;

namespace capture_tests {
class MapChoice {
  RSTD_ENUM(MapChoice, (Integer, (i64 value;)), (Text, (String value;)))
};
constexpr auto rstd_serde_definition(MapChoice::Integer_payload *) {
  using namespace rstd::serde;
  return RecordDefinition<
      MapChoice::Integer_payload, "Integer", Direction::Both,
      UnknownFieldPolicy::Ignore,
      Member<&MapChoice::Integer_payload::value, "value">>{};
}
constexpr auto rstd_serde_definition(MapChoice::Text_payload *) {
  using namespace rstd::serde;
  return RecordDefinition<MapChoice::Text_payload, "Text", Direction::Both,
                          UnknownFieldPolicy::Ignore,
                          Member<&MapChoice::Text_payload::value, "value">>{};
}
constexpr auto rstd_serde_definition(MapChoice *) {
  return rstd::serde::UntaggedDefinition<MapChoice, "MapChoice",
                                         rstd::serde::Direction::Both>{};
}

class SequenceChoice {
  RSTD_ENUM(SequenceChoice, (Integers, (Vec<i64> value;)),
            (Values, (Vec<rstd::json::Value> value;)))
};
constexpr auto rstd_serde_definition(SequenceChoice::Integers_payload *) {
  using T = SequenceChoice::Integers_payload;
  return rstd::serde::NewtypeDefinition<
      T, "Integers", rstd::serde::Direction::Both, &T::value>{};
}
constexpr auto rstd_serde_definition(SequenceChoice::Values_payload *) {
  using T = SequenceChoice::Values_payload;
  return rstd::serde::NewtypeDefinition<
      T, "Values", rstd::serde::Direction::Both, &T::value>{};
}
constexpr auto rstd_serde_definition(SequenceChoice *) {
  return rstd::serde::UntaggedDefinition<SequenceChoice, "SequenceChoice",
                                         rstd::serde::Direction::Both>{};
}

class TaggedChoice {
  RSTD_ENUM(TaggedChoice, (Data, (MapChoice value;)))
};
constexpr auto rstd_serde_definition(TaggedChoice::Data_payload *) {
  using T = TaggedChoice::Data_payload;
  return rstd::serde::NewtypeDefinition<T, "Data", rstd::serde::Direction::Both,
                                        &T::value>{};
}
constexpr auto rstd_serde_definition(TaggedChoice *) {
  return rstd::serde::InternalVariantDefinition<
      TaggedChoice, "TaggedChoice", rstd::serde::Direction::Both, "kind">{};
}

struct Nested {
  Vec<u64> numbers;
};
constexpr auto rstd_serde_definition(Nested *) {
  return rstd::serde::RecordDefinition<
      Nested, "Nested", rstd::serde::Direction::Both,
      rstd::serde::UnknownFieldPolicy::Reject,
      rstd::serde::Member<&Nested::numbers, "numbers">>{};
}

static_assert(rstd::serde::CapturingDeserializer<luato::ValueDeserializer>);
static_assert(rstd::serde::CapturingDeserializer<luato::SnapshotDeserializer>);
static_assert(
    rstd::mtp::same_as<
        decltype(rstd::declval<const luato::CapturedLuaValue &>().replay()),
        luato::ValueDeserializer>);
static_assert(rstd::mtp::same_as<
              decltype(rstd::declval<const luato::CapturedSnapshotValue &>()
                           .replay()),
              luato::SnapshotDeserializer>);

struct Checks {
  int failures{}, assertions{};
  void expect(bool value, const char *message) {
    ++assertions;
    if (!value) {
      ++failures;
      __builtin_printf("FAIL: serde capture: %s\n", message);
    }
  }
};

auto script(ref<str> text) -> luato::LuaModuleSource {
  return {"capture"_Str, "test:capture"_Str, "capture.lua"_Str,
          Vec<u8>::from(text.as_bytes())};
}
auto key(ref<str> text) -> luato::LuaValue {
  return luato::LuaValue::Bytes(Vec<u8>::from(text.as_bytes()));
}

template <class D> void scalar_replays(Checks &checks, D source, i64 expected) {
  auto captured = source.capture_value().unwrap();
  auto first = captured.replay();
  checks.expect(first.deserialize_string().is_err(),
                "a scalar branch can fail");
  auto second = captured.replay();
  checks.expect(second.deserialize_i64().unwrap() == expected,
                "later scalar branch preserves integer precision");
}

void scalars(Checks &checks) {
  for (auto number : {i64::MIN, i64::MAX, i64(9007199254740993LL)}) {
    auto live = luato::LuaValue::Integer(number);
    scalar_replays(checks, luato::ValueDeserializer(live), number);
    auto snapshot = luato::Value::Integer(number);
    scalar_replays(checks, luato::SnapshotDeserializer(snapshot), number);
  }
  auto number = luato::LuaValue::Number(f64(1));
  auto captured = luato::ValueDeserializer(number).capture_value().unwrap();
  auto integer = captured.replay();
  checks.expect(integer.deserialize_i64().is_err(),
                "float input does not become an integer");
  auto real = captured.replay();
  checks.expect(real.deserialize_f64().unwrap() == f64(1),
                "float input remains a float");
  auto bytes = Vec<u8>::make();
  bytes.push(u8(0xff));
  bytes.push(u8());
  auto replay = [&] {
    auto value = luato::LuaValue::Bytes(rstd::move(bytes));
    auto owned = luato::ValueDeserializer(value).capture_value().unwrap();
    return owned.replay();
  }();
  auto decoded = replay.deserialize_bytes().unwrap();
  checks.expect(decoded.len() == usize(2) && decoded[usize()] == u8(0xff) &&
                    decoded[usize(1)] == u8(),
                "replay owns binary bytes after input and capture destruction");
}

void branches(Checks &checks, luato::State &state) {
  auto values =
      state
          .evaluate_entry(script(
              "capture_runs=(capture_runs or 0)+1; return {1,'two'}, "
              "{value='kept'}, "
              "{kind='Data',value='kept'}, {kind='Data',value='kept',extra=true}"_str))
          .unwrap();
  auto sequence =
      luato::from_value<SequenceChoice>(values.values()[usize()]).unwrap();
  checks.expect(
      sequence.is_Values() && sequence.as_Values().value.len() == usize(2) &&
          sequence.as_Values().value[usize()].as_i64().unwrap() == i64(1) &&
          sequence.as_Values().value[usize(1)].as_str().unwrap() == "two"_str,
      "failed sequence branch cannot consume the next branch's cursor");
  auto sequence_snapshot = state.snapshot(values.values()[usize()]).unwrap();
  checks.expect(
      luato::from_value<SequenceChoice>(sequence_snapshot).unwrap().is_Values(),
      "snapshot sequence branches have independent cursors");
  auto object =
      luato::from_value<MapChoice>(values.values()[usize(1)]).unwrap();
  checks.expect(
      object.is_Text() && object.as_Text().value == "kept"_str,
      "failed map value branch cannot consume the next branch's cursor");
  auto snapshot = state.snapshot(values.values()[usize(1)]).unwrap();
  checks.expect(luato::from_value<MapChoice>(snapshot).unwrap().is_Text(),
                "snapshot map branches have independent cursors");
  checks.expect(
      luato::from_value<TaggedChoice>(values.values()[usize(2)])
          .unwrap()
          .as_Data()
          .value.is_Text(),
      "capture replays the same filtered internal map for every branch");
  checks.expect(
      luato::from_value<TaggedChoice>(values.values()[usize(3)]).is_err(),
      "strict internal unknown-field policy survives untagged replay");
  for (usize index = usize(2); index < usize(4); ++index) {
    auto native = state.snapshot(values.values()[index]).unwrap();
    checks.expect(
        luato::from_value<TaggedChoice>(native).is_ok() == (index == usize(2)),
        "snapshot capture retains tag filtering and inherited field policy");
  }
  checks.expect(
      state.execute_entry(script("assert(capture_runs == 1)"_str)).is_ok(),
      "branch replay does not execute the Lua input again");
}

void filtered_replays(Checks &checks, luato::State &state) {
  auto value =
      state.evaluate_entry(script("return {kind='Data',value='kept'}"_str))
          .unwrap();
  Vec<luato::LuaValue> keys;
  keys.push(key("value"_str));
  auto input = luato::ValueDeserializer(value.values()[usize()], {}, {}, {},
                                        Some(rstd::move(keys)), true);
  auto captured = input.capture_value().unwrap();
  auto first = captured.replay();
  auto map = first.begin_map().unwrap();
  checks.expect(map.next_key<String>().unwrap().unwrap() == "value"_str,
                "filtered replay preserves the selected key");
  checks.expect(map.next_value<i64>().is_err(),
                "filtered map branch fails after reading a key");
  checks.expect(first.capture_value().is_err(),
                "consumed filtered map cannot be captured again");
  auto second = captured.replay();
  checks.expect(
      rstd::serde::deserialize<MapChoice>(second).unwrap().is_Text(),
      "later replay owns an unconsumed copy of the filtered key view");
  auto snapshot = state.snapshot(value.values()[usize()]).unwrap();
  Vec<usize> indices;
  for (usize index{}; index < snapshot.as_Table().value->entries().len();
       ++index)
    if (snapshot.as_Table().value->entries()[index].key == "value"_str)
      indices.push(usize(index));
  auto source = luato::SnapshotDeserializer(snapshot, {}, {}, usize(128), true,
                                            Some(rstd::move(indices)));
  auto owned = source.capture_value().unwrap();
  auto invalid = owned.replay();
  auto failed_map = invalid.begin_map().unwrap();
  failed_map.next_key<String>().unwrap();
  checks.expect(failed_map.next_value<i64>().is_err(),
                "snapshot filtered branch fails");
  checks.expect(invalid.capture_value().is_err(),
                "snapshot filtered capture is single-consumption");
  auto fresh = owned.replay();
  checks.expect(rstd::serde::deserialize<MapChoice>(fresh).unwrap().is_Text(),
                "snapshot filtered indices are fresh for every replay");
}

void markers(Checks &checks, luato::State &state) {
  auto make_capture = [&](bool sequence) {
    auto handles = state.evaluate_entry(script("return {},{},{}"_str)).unwrap();
    luato::SerializeOptions options;
    options.null_value = &handles.values()[usize()].as_Table().value;
    options.object_metatable = &handles.values()[usize(1)].as_Table().value;
    options.array_metatable = &handles.values()[usize(2)].as_Table().value;
    auto value = sequence
                     ? luato::to_value(state, Vec<i64>{}, options).unwrap()
                     : luato::to_value(state, Dictionary{}, options).unwrap();
    return luato::ValueDeserializer(value, options.deserialize_options())
        .capture_value()
        .unwrap();
  };
  auto array = make_capture(true);
  auto sequence = array.replay();
  checks.expect(
      rstd::serde::deserialize<Vec<i64>>(sequence).unwrap().is_empty(),
      "array marker remains valid after borrowed marker handles die");
  auto wrong_map = array.replay();
  checks.expect(rstd::serde::deserialize<Dictionary>(wrong_map).is_err(),
                "marked empty array is not reinterpreted as a map");
  auto object = make_capture(false);
  auto map = object.replay();
  checks.expect(
      rstd::serde::deserialize<Dictionary>(map).unwrap().is_empty(),
      "object marker remains valid after borrowed marker handles die");
  auto wrong_sequence = object.replay();
  checks.expect(rstd::serde::deserialize<Vec<i64>>(wrong_sequence).is_err(),
                "marked empty object is not reinterpreted as a sequence");
  auto empty = state.evaluate_entry(script("return {}"_str)).unwrap();
  auto unmarked = luato::ValueDeserializer(empty.values()[usize()])
                      .capture_value()
                      .unwrap();
  auto plain_sequence = unmarked.replay();
  auto plain_map = unmarked.replay();
  checks.expect(rstd::serde::deserialize<Vec<i64>>(plain_sequence).is_ok() &&
                    rstd::serde::deserialize<Dictionary>(plain_map).is_ok(),
                "unmarked empty table retains both native interpretations");
  auto escaped_sequence = [&] {
    auto handles = state.evaluate_entry(script("return {}"_str)).unwrap();
    luato::SerializeOptions options;
    options.null_value = &handles.values()[usize()].as_Table().value;
    Vec<Option<i64>> values;
    values.push(None());
    auto value = luato::to_value(state, values, options).unwrap();
    auto captured =
        luato::ValueDeserializer(value, options.deserialize_options())
            .capture_value()
            .unwrap();
    auto source = captured.replay();
    return source.begin_sequence().unwrap();
  }();
  checks.expect(
      escaped_sequence.next<Option<i64>>().unwrap().unwrap().is_none(),
      "sequence access retains captured null marker after replay destruction");
  checks.expect(escaped_sequence.end().is_ok(),
                "escaped live sequence completes");
}

void lifetimes(Checks &checks, luato::State &state) {
  auto source = [&] {
    auto table = luato::Table::make();
    table.insert("value"_Str, luato::Value::String("owned"_Str)).unwrap();
    auto captured = luato::SnapshotDeserializer(table).capture_value().unwrap();
    return captured.replay();
  }();
  checks.expect(
      rstd::serde::deserialize<MapChoice>(source).unwrap().as_Text().value ==
          "owned"_str,
      "snapshot replay owns its input after source and capture destruction");
  auto access = [&] {
    auto table = luato::Table::make();
    table.set("value"_Str, i64(7)).unwrap();
    auto captured = luato::SnapshotDeserializer(table).capture_value().unwrap();
    auto replay = captured.replay();
    return replay.begin_map().unwrap();
  }();
  checks.expect(access.next_key<String>().unwrap().unwrap() == "value"_str &&
                    access.next_value<i64>().unwrap() == i64(7) &&
                    access.end().is_ok(),
                "snapshot map access retains captured entries independently");
  auto sequence = [&] {
    Vec<luato::Value> values;
    values.push(luato::Value::Integer(i64(8)));
    auto value = luato::Value::Array(luato::Array::from(rstd::move(values)));
    auto captured = luato::SnapshotDeserializer(value).capture_value().unwrap();
    auto replay = captured.replay();
    return replay.begin_sequence().unwrap();
  }();
  checks.expect(
      sequence.next<i64>().unwrap().unwrap() == i64(8) &&
          sequence.end().is_ok(),
      "snapshot sequence access retains captured elements independently");
  auto enumeration = [&] {
    auto table = luato::Table::make();
    table.insert("Text"_Str, luato::Value::String("enum"_Str)).unwrap();
    auto captured = luato::SnapshotDeserializer(table).capture_value().unwrap();
    auto replay = captured.replay();
    return replay.begin_enum().unwrap();
  }();
  checks.expect(enumeration.variant() == "Text"_str &&
                    enumeration.value<String>().unwrap() == "enum"_str,
                "snapshot enum access retains captured payload independently");
  auto internal = [&] {
    auto table = luato::Table::make();
    table.set("kind"_Str, "Data"_Str).unwrap();
    table.insert("value"_Str, luato::Value::String("internal"_Str)).unwrap();
    auto captured = luato::SnapshotDeserializer(table).capture_value().unwrap();
    auto replay = captured.replay();
    return replay
        .begin_internal_enum("kind"_str,
                             rstd::serde::UnknownFieldPolicy::Reject)
        .unwrap();
  }();
  checks.expect(internal.value<MapChoice>().unwrap().as_Text().value ==
                    "internal"_str,
                "snapshot internal enum retains its filtered captured owner");
  auto live_unit = [&] {
    auto marker = state.create_table().unwrap();
    luato::SerializeOptions options;
    options.null_value = &marker;
    auto null = luato::to_value(state, empty{}, options).unwrap();
    auto table = state.create_table().unwrap();
    table.raw_set(key("Unit"_str), null).unwrap();
    auto value = luato::LuaValue::Table(rstd::move(table));
    auto captured =
        luato::ValueDeserializer(value, options.deserialize_options())
            .capture_value()
            .unwrap();
    auto replay = captured.replay();
    return replay.begin_enum().unwrap();
  }();
  checks.expect(live_unit.unit().is_ok(),
                "live enum access retains captured null marker independently");
  auto expired = [&] {
    auto state = luato::State::create(luato::StateOptions::base()).unwrap();
    auto value = state.evaluate_entry(script("return {value=7}"_str)).unwrap();
    return luato::ValueDeserializer(value.values()[usize()])
        .capture_value()
        .unwrap();
  }();
  auto replay = expired.replay();
  checks.expect(
      rstd::serde::deserialize<MapChoice>(replay).is_err(),
      "capture does not change the VM's existing weak-handle lifetime policy");
}

void errors(Checks &checks, luato::State &state) {
  auto value =
      state.evaluate_entry(script("return {numbers={1,-1}}"_str)).unwrap();
  auto path = DataPath{}.with_field("payload"_str).with_index(usize(2));
  auto input =
      luato::ValueDeserializer(value.values()[usize()], {}, path.clone());
  auto captured = input.capture_value().unwrap();
  auto replay = captured.replay();
  auto failure = rstd::serde::deserialize<Nested>(replay);
  checks.expect(failure.is_err() &&
                    rstd::format("{}", failure.as_ref().unwrap_err())
                        .as_str()
                        .contains("payload[2].numbers[1]"_str),
                "live capture retains nested error paths");
  auto snapshot = state.snapshot(value.values()[usize()]).unwrap();
  auto native = luato::SnapshotDeserializer(snapshot, path.clone());
  auto owned = native.capture_value().unwrap();
  auto fresh = owned.replay();
  auto native_failure = rstd::serde::deserialize<Nested>(fresh);
  checks.expect(native_failure.is_err() &&
                    rstd::format("{}", native_failure.as_ref().unwrap_err())
                        .as_str()
                        .contains("payload[2].numbers[1]"_str),
                "snapshot capture retains nested error paths");
  luato::DeserializeOptions options;
  options.max_depth = usize(1);
  auto limited =
      luato::ValueDeserializer(value.values()[usize()], options, path.clone());
  auto bounded = limited.capture_value().unwrap();
  auto shallow = bounded.replay();
  auto depth_error = rstd::serde::deserialize<Nested>(shallow);
  checks.expect(depth_error.is_err() &&
                    rstd::format("{}", depth_error.as_ref().unwrap_err())
                        .as_str()
                        .contains("nesting"_str),
                "replay preserves max depth and does not add an artificial "
                "nesting level");
  auto bounded_snapshot =
      luato::SnapshotDeserializer(snapshot, path.clone(), {}, usize(1))
          .capture_value()
          .unwrap();
  auto shallow_snapshot = bounded_snapshot.replay();
  auto snapshot_depth_error =
      rstd::serde::deserialize<Nested>(shallow_snapshot);
  checks.expect(
      snapshot_depth_error.is_err() &&
          rstd::format("{}", snapshot_depth_error.as_ref().unwrap_err())
              .as_str()
              .contains("nesting"_str),
      "snapshot replay preserves the configured depth limit");
}

void callbacks(Checks &checks, luato::State &state) {
  auto module = luato::ModuleSpec("capture_host"_Str);
  module.raw_function(
      "read"_Str, usize(1),
      [](luato::CallFrame &frame) -> luato::BindingResult {
        auto value = rstd_try(luato::argument<luato::LuaValue>(frame, usize()));
        auto decoded = rstd_try(luato::from_value<MapChoice>(value));
        return luato::return_value(frame, decoded.is_Text());
      });
  state.register_module(rstd::move(module)).unwrap();
  checks.expect(
      state
          .execute_entry(
              script("assert(capture_host.read{value='callback'})"_str))
          .is_ok(),
      "capture and replay use the protected owner in an active CallFrame");
}
} // namespace capture_tests

auto expect_serde_capture_contract() -> int {
  capture_tests::Checks checks;
  auto state = luato::State::create(luato::StateOptions::base()).unwrap();
  capture_tests::scalars(checks);
  capture_tests::branches(checks, state);
  capture_tests::filtered_replays(checks, state);
  capture_tests::markers(checks, state);
  capture_tests::lifetimes(checks, state);
  capture_tests::errors(checks, state);
  capture_tests::callbacks(checks, state);
  __builtin_printf("serde capture: %d assertions, %d failures\n",
                   checks.assertions, checks.failures);
  return checks.failures == 0 ? 0 : 1;
}
