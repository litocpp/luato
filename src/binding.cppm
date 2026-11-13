module;
#include <rstd/enum.hpp>

export module luato:binding;

export import :error;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace luato {

class State;
class Table;
class TableHandle;
class Array;
class LuaValue;
class MultiValue;
class CallFrame;
class UserDataHandle;

template <typename T> inline const unsigned char userdata_type_key = 0;

struct OpaqueHandle {
  const void *identity{};
};

class Value {
  RSTD_ENUM(Value, (Nil, ()), (Number, (f64 value;)), (Integer, (i64 value;)),
            (Boolean, (bool value;)),
            (String, (::alloc::string::String value;)),
            (Opaque, (const void *value;)), (Table, (Box<luato::Table> value;)),
            (Array, (Box<luato::Array> value;)))

public:
  Value(const Value &) = delete;
  auto operator=(const Value &) -> Value & = delete;
  Value(Value &&) noexcept = default;
  auto operator=(Value &&) noexcept -> Value & = default;

  static auto Table(luato::Table value) -> Value;
  static auto Array(luato::Array value) -> Value;

  auto type_name() const noexcept -> ref<str>;
  auto clone() const -> Value;
};

class Array {
public:
  static auto make() -> Array { return Array(Vec<Value>::make()); }

  static auto from(Vec<Value> values) -> Array {
    return Array(rstd::move(values));
  }

  auto values() const noexcept -> slice<Value> { return values_.as_slice(); }
  auto len() const noexcept -> usize { return values_.len(); }
  auto is_empty() const noexcept -> bool { return values_.is_empty(); }
  auto clone() const -> Array;

private:
  explicit Array(Vec<Value> values) : values_(rstd::move(values)) {}

  Vec<Value> values_;
};

struct TableEntry {
  String key;
  Value value;

  auto clone() const -> TableEntry {
    return TableEntry{.key = key.clone(), .value = value.clone()};
  }
};

class ScalarValue {
  RSTD_ENUM(ScalarValue, (Integer, (i64 value;)), (Boolean, (bool value;)),
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
  auto contains(ref<str> key) const noexcept -> bool {
    return lookup(key) != nullptr;
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
  auto set(String key, OpaqueHandle value) -> Result<empty> {
    return insert(rstd::move(key), Value::Opaque(value.identity));
  }
  auto set(String key, Table value) -> Result<empty> {
    return insert(rstd::move(key), Value::Table(rstd::move(value)));
  }
  auto set(String key, Array value) -> Result<empty> {
    return insert(rstd::move(key), Value::Array(rstd::move(value)));
  }

  template <typename T>
    requires(rstd::mtp::same_as<T, i64> || rstd::mtp::same_as<T, bool> ||
             rstd::mtp::same_as<T, String> || rstd::mtp::same_as<T, Table> ||
             rstd::mtp::same_as<T, Array> ||
             rstd::mtp::same_as<T, OpaqueHandle>)
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
    } else if constexpr (rstd::mtp::same_as<T, Table>) {
      if (value->is_Table())
        return Ok(value->as_Table().value->clone());
      return Err(field_type_error(key, "a table"_str, *value));
    } else if constexpr (rstd::mtp::same_as<T, Array>) {
      if (value->is_Array())
        return Ok(value->as_Array().value->clone());
      return Err(field_type_error(key, "an array"_str, *value));
    } else {
      if (value->is_Opaque())
        return Ok(OpaqueHandle{.identity = value->as_Opaque().value});
      return Err(field_type_error(key, "an opaque handle"_str, *value));
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

inline auto Value::Array(luato::Array value) -> Value {
  return Value::Array(Box<luato::Array>::make(rstd::move(value)));
}

inline auto Array::clone() const -> Array {
  auto copied = values_.iter()
                    .map([](auto value) { return value->clone(); })
                    .collect<Vec<Value>>();
  return Array(rstd::move(copied));
}

inline auto Value::type_name() const noexcept -> ref<str> {
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

inline auto Value::clone() const -> Value {
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

inline auto Table::clone() const -> Table {
  auto copied = entries_.iter()
                    .map([](auto entry) { return entry->clone(); })
                    .collect<Vec<TableEntry>>();
  return Table(path_.clone(), rstd::move(copied));
}

inline auto Table::insert(String key, Value value) -> Result<empty> {
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

inline auto Table::lookup(ref<str> key) const noexcept -> const Value * {
  auto found =
      entries_.iter().find([&](auto entry) { return entry->key == key; });
  return found.is_some() ? rstd::addressof((*found)->value) : nullptr;
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

inline auto Table::scalar_entries() const -> Result<Vec<ScalarEntry>> {
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

class CallFrame {
public:
  CallFrame(const CallFrame &) = delete;
  CallFrame &operator=(const CallFrame &) = delete;

  auto argument_count() const noexcept -> usize { return argument_count_; }

  auto argument(usize index) -> Result<OpaqueHandle> {
    return read_value_(context_, index);
  }
  void return_value(OpaqueHandle value) { return_value_(context_, value); }
  auto new_table() -> Result<OpaqueHandle> { return new_table_(context_); }
  auto create_table() -> Result<TableHandle>;
  auto validate_value(const LuaValue &value) const -> Result<empty>;
  template <typename T> auto create_userdata(T value) -> Result<UserDataHandle>;

  template <typename T>
    requires(rstd::mtp::same_as<T, i64> || rstd::mtp::same_as<T, bool> ||
             rstd::mtp::same_as<T, String> || rstd::mtp::same_as<T, Table> ||
             rstd::mtp::same_as<T, Array> ||
             rstd::mtp::same_as<T, OpaqueHandle>)
  auto required(usize index) -> Result<T> {
    if constexpr (rstd::mtp::same_as<T, i64>) {
      return read_i64_(context_, index);
    } else if constexpr (rstd::mtp::same_as<T, bool>) {
      return read_bool_(context_, index);
    } else if constexpr (rstd::mtp::same_as<T, String>) {
      return read_string_(context_, index);
    } else if constexpr (rstd::mtp::same_as<T, Table>) {
      return read_table_(context_, index);
    } else if constexpr (rstd::mtp::same_as<T, Array>) {
      return read_array_(context_, index);
    } else {
      return read_opaque_(context_, index);
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
  void push(OpaqueHandle value) {
    push_value_(context_, Value::Opaque(value.identity));
  }
  void push(Value value) { push_value_(context_, rstd::move(value)); }
  void push_nil() { push_nil_(context_); }
  void raise(Value value) { raise_value_(context_, rstd::move(value)); }
  void raise_handle(OpaqueHandle value) { raise_handle_(context_, value); }

private:
  using ReadI64 = auto (*)(void *, usize) -> Result<i64>;
  using ReadBool = auto (*)(void *, usize) -> Result<bool>;
  using ReadString = auto (*)(void *, usize) -> Result<String>;
  using ReadTable = auto (*)(void *, usize) -> Result<Table>;
  using ReadArray = auto (*)(void *, usize) -> Result<Array>;
  using ReadOpaque = auto (*)(void *, usize) -> Result<OpaqueHandle>;
  using PushValue = void (*)(void *, Value);
  using PushNil = void (*)(void *);
  using RaiseValue = void (*)(void *, Value);
  using ReadValue = auto (*)(void *, usize) -> Result<OpaqueHandle>;
  using ReturnValue = void (*)(void *, OpaqueHandle);
  using NewTable = auto (*)(void *) -> Result<OpaqueHandle>;
  using RaiseHandle = void (*)(void *, OpaqueHandle);
  using NewUserData = auto (*)(void *, const void *, void *, void (*)(void *))
      -> Result<OpaqueHandle>;

  CallFrame(void *context, usize argument_count, ReadI64 read_i64,
            ReadBool read_bool, ReadString read_string, ReadTable read_table,
            ReadArray read_array, ReadOpaque read_opaque, PushValue push_value,
            PushNil push_nil, RaiseValue raise_value, ReadValue read_value,
            ReturnValue return_value, NewTable new_table,
            RaiseHandle raise_handle, NewUserData new_userdata) noexcept
      : context_(context), argument_count_(argument_count), read_i64_(read_i64),
        read_bool_(read_bool), read_string_(read_string),
        read_table_(read_table), read_array_(read_array),
        read_opaque_(read_opaque), push_value_(push_value), push_nil_(push_nil),
        raise_value_(raise_value), read_value_(read_value),
        return_value_(return_value), new_table_(new_table),
        raise_handle_(raise_handle), new_userdata_(new_userdata) {}

  void *context_;
  usize argument_count_;
  ReadI64 read_i64_;
  ReadBool read_bool_;
  ReadString read_string_;
  ReadTable read_table_;
  ReadArray read_array_;
  ReadOpaque read_opaque_;
  PushValue push_value_;
  PushNil push_nil_;
  RaiseValue raise_value_;
  ReadValue read_value_;
  ReturnValue return_value_;
  NewTable new_table_;
  RaiseHandle raise_handle_;
  NewUserData new_userdata_;

  friend class State;
};

inline constexpr auto variadic_arity = usize(18446744073709551615ULL);

using NativeCallback = dyn<FnMut<BindingResult(CallFrame &)>>;
class AsyncReturn {
  Option<Box<NativeCallback>> callback_;
  Option<Box<dyn<FnMut<void(const Error &)>>>> failure_;

  explicit AsyncReturn(Box<NativeCallback> callback)
      : callback_(Some(rstd::move(callback))) {}
  auto take_callback() -> Box<NativeCallback> {
    return rstd::move(callback_.take()).unwrap();
  }
  void failed(const Error &error) {
    if (failure_.is_some())
      (**failure_)(error);
  }
  friend class State;

public:
  AsyncReturn(AsyncReturn &&) noexcept = default;
  auto operator=(AsyncReturn &&) noexcept -> AsyncReturn & = default;
  template <typename F> static auto make(F &&finish) -> AsyncReturn {
    return AsyncReturn(Box<NativeCallback>::make(rstd::forward<F>(finish)));
  }
  static auto values(MultiValue values) -> AsyncReturn;
  template <typename F> auto on_error(F &&failure) && -> AsyncReturn {
    failure_ = Some(Box<dyn<FnMut<void(const Error &)>>>::make(
        [previous = failure_.take(),
         failure = rstd::forward<F>(failure)](const Error &error) mutable {
          if (previous.is_some())
            (**previous)(error);
          failure(error);
        }));
    return rstd::move(*this);
  }
};

template <typename T>
auto async_return(rstd::async::coro<Result<T>> task)
    -> rstd::async::coro<Result<AsyncReturn>> {
  auto result = co_await rstd::move(task);
  if (result.is_err())
    co_return Err(rstd::move(result).unwrap_err());
  if constexpr (rstd::mtp::same_as<T, AsyncReturn>)
    co_return Ok(rstd::move(*result));
  else {
    static_assert(rstd::mtp::same_as<T, MultiValue>);
    co_return Ok(AsyncReturn::values(rstd::move(*result)));
  }
}

using AsyncNativeStarter =
    dyn<FnMut<Result<rstd::async::coro<Result<Array>>>(CallFrame &)>>;
using AsyncHandleStarter =
    dyn<FnMut<Result<rstd::async::coro<Result<OpaqueHandle>>>(CallFrame &)>>;
using AsyncValuesStarter =
    dyn<FnMut<Result<rstd::async::coro<Result<MultiValue>>>(CallFrame &)>>;
using AsyncReturnStarter =
    dyn<FnMut<Result<rstd::async::coro<Result<AsyncReturn>>>(CallFrame &)>>;

} // namespace luato

namespace luato {

template <typename T>
concept NativeValue =
    rstd::mtp::any<T, i64, bool, String, Table, Array, OpaqueHandle>;

template <typename T> struct NativeReturn {
  static constexpr bool supported = NativeValue<T> ||
                                    rstd::mtp::same_as<T, Value> ||
                                    rstd::mtp::same_as<T, empty>;
  template <class Frame>
  static auto push(Frame &frame, T value) -> BindingResult
    requires supported
  {
    if constexpr (rstd::mtp::same_as<T, empty>)
      return Ok(usize{});
    else if constexpr (rstd::mtp::same_as<T, Array>)
      frame.push(Value::Array(rstd::move(value)));
    else
      frame.push(rstd::move(value));
    return Ok(usize(1));
  }
};

template <> struct NativeReturn<void> {
  static constexpr bool supported = true;
};

template <typename T> struct NativeReturn<Option<T>> {
  static constexpr bool supported = NativeValue<T>;
  template <class Frame>
  static auto push(Frame &frame, Option<T> value) -> BindingResult
    requires supported
  {
    if (value.is_some())
      return NativeReturn<T>::push(frame, rstd::move(*value));
    frame.push_nil();
    return Ok(usize(1));
  }
};

template <typename T> struct NativeReturn<Result<T>> {
  static constexpr bool supported =
      NativeReturn<T>::supported && !rstd::mtp::is_void<T>;
  template <class Frame>
  static auto push(Frame &frame, Result<T> value) -> BindingResult
    requires supported
  {
    if (value.is_err())
      return Err(rstd::move(value).unwrap_err());
    return NativeReturn<T>::push(frame, rstd::move(value).unwrap());
  }
};

template <typename Signature> struct NativeAdapter {};

template <typename R, typename... Args> struct NativeAdapter<R(Args...)> {
  static constexpr bool supported =
      NativeReturn<R>::supported &&
      ((NativeValue<rstd::mtp::rm_cvf<Args>> &&
        (!rstd::mtp::is_ref<Args> ||
         rstd::mtp::same_as<Args, const rstd::mtp::rm_cvf<Args> &>)) &&
       ...);
  using Arguments = rstd::tuple<Option<rstd::mtp::rm_cvf<Args>>...>;

  template <class Frame, rstd::size_t I = 0>
  static auto read(Frame &frame, Arguments &arguments) -> Result<empty> {
    if constexpr (I == sizeof...(Args))
      return Ok(empty{});
    else {
      using T = rstd::mtp::rm_cvf<
          typename rstd::mtp::func_traits<R(Args...)>::template argument<I>>;
      auto value = frame.template required<T>(usize(I));
      if (value.is_err())
        return Err(rstd::move(value).unwrap_err());
      rstd::get<I>(arguments) = Some(rstd::move(value).unwrap());
      return read<Frame, I + 1>(frame, arguments);
    }
  }

  template <typename F, class Frame, rstd::size_t... Is>
  static auto call(F &function, Frame &frame, Arguments &arguments,
                   rstd::mtp::index_sequence<Is...>) -> BindingResult {
    if constexpr (rstd::mtp::is_void<R>) {
      function(static_cast<Args>(rstd::move(*rstd::get<Is>(arguments)))...);
      return Ok(usize{});
    } else {
      return NativeReturn<R>::push(frame, function(static_cast<Args>(rstd::move(
                                              *rstd::get<Is>(arguments)))...));
    }
  }

  template <typename F>
  static constexpr bool accepts =
      supported && requires(F &function, Args... args) {
        { function(rstd::forward<Args>(args)...) } -> rstd::mtp::same_as<R>;
      };

  template <typename F> static auto wrap(F &&function) {
    return [function = rstd::forward<F>(function)](
               CallFrame &frame) mutable -> BindingResult {
      auto arguments = Arguments{};
      auto loaded = read(frame, arguments);
      if (loaded.is_err())
        return Err(rstd::move(loaded).unwrap_err());
      return call(function, frame, arguments,
                  rstd::mtp::make_index_sequence<sizeof...(Args)>{});
    };
  }
};

template <typename T> struct AsyncValue {
  static auto make(T value) -> Value {
    if constexpr (rstd::mtp::same_as<T, i64>)
      return Value::Integer(value);
    else if constexpr (rstd::mtp::same_as<T, bool>)
      return Value::Boolean(value);
    else if constexpr (rstd::mtp::same_as<T, String>)
      return Value::String(rstd::move(value));
    else if constexpr (rstd::mtp::same_as<T, Table>)
      return Value::Table(rstd::move(value));
    else if constexpr (rstd::mtp::same_as<T, Array>)
      return Value::Array(rstd::move(value));
    else if constexpr (rstd::mtp::same_as<T, Value>)
      return rstd::move(value);
    else
      static_assert(!rstd::mtp::same_as<T, T>, "unsupported async return type");
  }
};

template <typename T> struct AsyncValue<Option<T>> {
  static auto make(Option<T> value) -> Value {
    if (value.is_some())
      return AsyncValue<T>::make(rstd::move(*value));
    return Value::Nil();
  }
};

template <typename Signature> struct NativeAsyncAdapter {};

template <typename R, typename... Args> struct NativeAsyncAdapter<R(Args...)> {
  static constexpr bool supported = NativeAdapter<R(Args...)>::supported;
  using Arguments = rstd::tuple<Option<rstd::mtp::rm_cvf<Args>>...>;

  template <typename F, typename Tuple, rstd::size_t... Is>
  static auto run(F &function, Tuple arguments,
                  rstd::mtp::index_sequence<Is...>)
      -> rstd::async::coro<Result<Array>> {
    auto result = co_await function(rstd::move(rstd::get<Is>(arguments))...);
    if (result.is_err())
      co_return Err(rstd::move(result).unwrap_err());
    auto values = Vec<Value>::make();
    if constexpr (!rstd::mtp::is_void<R>) {
      auto value = rstd::move(result).unwrap();
      values.push(AsyncValue<rstd::mtp::rm_cvf<R>>::make(rstd::move(value)));
    }
    co_return Ok(Array::from(rstd::move(values)));
  }

  template <typename F, rstd::size_t... Is>
  static auto take(Arguments &arguments, rstd::mtp::index_sequence<Is...>)
      -> rstd::tuple<rstd::mtp::rm_cvf<Args>...> {
    return rstd::tuple<rstd::mtp::rm_cvf<Args>...>(
        rstd::move(*rstd::get<Is>(arguments))...);
  }

  template <typename F> static auto wrap(F &&function) {
    return [function = rstd::forward<F>(function)](CallFrame &frame) mutable
               -> Result<rstd::async::coro<Result<Array>>> {
      auto arguments = Arguments{};
      auto loaded = NativeAdapter<R(Args...)>::read(frame, arguments);
      if (loaded.is_err())
        return Err(rstd::move(loaded).unwrap_err());
      auto values =
          take<F>(arguments, rstd::mtp::make_index_sequence<sizeof...(Args)>{});
      return Ok(run(function, rstd::move(values),
                    rstd::mtp::make_index_sequence<sizeof...(Args)>{}));
    };
  }
};

} // namespace luato

export namespace luato {

class NativeFunctionSpec {
public:
  NativeFunctionSpec(NativeFunctionSpec &&) noexcept = default;
  NativeFunctionSpec &operator=(NativeFunctionSpec &&) noexcept = default;

  /// Owns the callable; borrowed captures must outlive the State.
  /// Arguments are decoded in order and only owned values are returned to Lua.
  template <typename Signature, typename F>
    requires NativeAdapter<Signature>::template
  accepts<rstd::mtp::decay<F>> static auto typed(String name, F &&function)
      -> NativeFunctionSpec {
    return make(rstd::move(name),
                usize(rstd::mtp::func_traits<Signature>::arity),
                NativeAdapter<Signature>::wrap(rstd::forward<F>(function)));
  }

  template <typename F>
    requires requires {
      typename rstd::mtp::func_traits<F>::signature;
    } && NativeAdapter<typename rstd::mtp::func_traits<F>::signature>::template
  accepts<rstd::mtp::decay<F>> static auto typed(String name, F &&function)
      -> NativeFunctionSpec {
    return typed<typename rstd::mtp::func_traits<F>::signature>(
        rstd::move(name), rstd::forward<F>(function));
  }

  template <typename Callback>
  static auto make(String name, usize arity, Callback &&callback)
      -> NativeFunctionSpec {
    return NativeFunctionSpec(
        rstd::move(name), arity,
        Box<NativeCallback>::make(rstd::forward<Callback>(callback)));
  }

private:
  NativeFunctionSpec(String name, usize arity, Box<NativeCallback> callback)
      : name_(rstd::move(name)), arity_(arity),
        callback_(rstd::move(callback)) {}

  String name_;
  usize arity_;
  Box<NativeCallback> callback_;

  friend class State;
};

template <typename T> struct AsyncReturnOf;
template <typename R>
struct AsyncReturnOf<rstd::async::coro<luato::Result<R>>> {
  using type = R;
};

template <typename Signature> struct AsyncSignature;
template <typename C, typename... Args> struct AsyncSignature<C(Args...)> {
  using type = typename AsyncReturnOf<C>::type(Args...);
};

class AsyncNativeFunctionSpec {
public:
  AsyncNativeFunctionSpec(AsyncNativeFunctionSpec &&) noexcept = default;
  AsyncNativeFunctionSpec &
  operator=(AsyncNativeFunctionSpec &&) noexcept = default;

  template <typename Signature, typename F>
    requires NativeAsyncAdapter<Signature>::supported &&
             requires(F &function, CallFrame &frame) {
               {
                 NativeAsyncAdapter<Signature>::wrap(
                     rstd::forward<F>(function))(frame)
               }
               -> rstd::mtp::same_as<Result<rstd::async::coro<Result<Array>>>>;
             }
  static auto typed(String name, F &&function) -> AsyncNativeFunctionSpec {
    auto wrapped =
        NativeAsyncAdapter<Signature>::wrap(rstd::forward<F>(function));
    return AsyncNativeFunctionSpec(
        rstd::move(name), usize(rstd::mtp::func_traits<Signature>::arity),
        Box<AsyncNativeStarter>::make(rstd::move(wrapped)));
  }

  template <typename F>
    requires requires { typename rstd::mtp::func_traits<F>::signature; }
  static auto typed(String name, F &&function) -> AsyncNativeFunctionSpec {
    using converted = typename AsyncSignature<
        typename rstd::mtp::func_traits<F>::signature>::type;
    return typed<converted>(rstd::move(name), rstd::forward<F>(function));
  }

private:
  AsyncNativeFunctionSpec(String name, usize arity,
                          Box<AsyncNativeStarter> starter)
      : name_(rstd::move(name)), arity_(arity), starter_(rstd::move(starter)) {}

  String name_;
  usize arity_;
  Box<AsyncNativeStarter> starter_;

  friend class State;
};

class AsyncNativeHandleFunctionSpec {
public:
  AsyncNativeHandleFunctionSpec(AsyncNativeHandleFunctionSpec &&) noexcept =
      default;
  AsyncNativeHandleFunctionSpec &
  operator=(AsyncNativeHandleFunctionSpec &&) noexcept = default;

  template <typename Callable>
  static auto make(String name, usize arity, Callable &&callable)
      -> AsyncNativeHandleFunctionSpec {
    return AsyncNativeHandleFunctionSpec(
        rstd::move(name), arity,
        Box<AsyncHandleStarter>::make(rstd::forward<Callable>(callable)));
  }

private:
  AsyncNativeHandleFunctionSpec(String name, usize arity,
                                Box<AsyncHandleStarter> starter)
      : name_(rstd::move(name)), arity_(arity), starter_(rstd::move(starter)) {}

  String name_;
  usize arity_;
  Box<AsyncHandleStarter> starter_;

  friend class State;
};

class AsyncNativeValuesFunctionSpec {
public:
  AsyncNativeValuesFunctionSpec(AsyncNativeValuesFunctionSpec &&) noexcept;
  AsyncNativeValuesFunctionSpec &
  operator=(AsyncNativeValuesFunctionSpec &&) noexcept;
  ~AsyncNativeValuesFunctionSpec();

  template <typename Callable>
  static auto make(String name, usize arity, Callable &&callable)
      -> AsyncNativeValuesFunctionSpec {
    auto starter =
        [callable = rstd::forward<Callable>(callable)](CallFrame &frame) mutable
        -> Result<rstd::async::coro<Result<AsyncReturn>>> {
      auto started = callable(frame);
      if (started.is_err())
        return Err(rstd::move(started).unwrap_err());
      return Ok(async_return(rstd::move(*started)));
    };
    return AsyncNativeValuesFunctionSpec(
        rstd::move(name), arity,
        Box<AsyncReturnStarter>::make(rstd::move(starter)));
  }

private:
  AsyncNativeValuesFunctionSpec(String name, usize arity,
                                Box<AsyncReturnStarter> starter);

  String name_;
  usize arity_;
  Box<AsyncReturnStarter> starter_;

  friend class State;
};

class ModuleSpec {
public:
  explicit ModuleSpec(String name)
      : name_(rstd::move(name)), functions_(Vec<NativeFunctionSpec>::make()),
        async_functions_(Vec<AsyncNativeFunctionSpec>::make()),
        async_handle_functions_(Vec<AsyncNativeHandleFunctionSpec>::make()),
        async_values_functions_(Vec<AsyncNativeValuesFunctionSpec>::make()),
        fields_(Vec<TableEntry>::make()) {}

  ModuleSpec(ModuleSpec &&) noexcept = default;
  ModuleSpec &operator=(ModuleSpec &&) noexcept = default;

  void add(NativeFunctionSpec function) {
    functions_.push(rstd::move(function));
  }

  template <typename Callback>
  void raw_function(String name, usize arity, Callback &&callback) {
    add(NativeFunctionSpec::make(rstd::move(name), arity,
                                 rstd::forward<Callback>(callback)));
  }

  template <typename Callback>
  void raw_variadic_function(String name, Callback &&callback) {
    add(NativeFunctionSpec::make(rstd::move(name), variadic_arity,
                                 rstd::forward<Callback>(callback)));
  }

  template <typename F>
    requires requires(String name, F &&f) {
      NativeFunctionSpec::typed(rstd::move(name), rstd::forward<F>(f));
    }
  void function(String name, F &&callable) {
    add(NativeFunctionSpec::typed(rstd::move(name),
                                  rstd::forward<F>(callable)));
  }

  template <typename Signature, typename F>
    requires requires(String name, F &&f) {
      NativeFunctionSpec::typed<Signature>(rstd::move(name),
                                           rstd::forward<F>(f));
    }
  void function(String name, F &&callable) {
    add(NativeFunctionSpec::typed<Signature>(rstd::move(name),
                                             rstd::forward<F>(callable)));
  }

  template <typename F>
    requires requires(String name, F &&f) {
      AsyncNativeFunctionSpec::typed(rstd::move(name), rstd::forward<F>(f));
    }
  void async_function(String name, F &&callable) {
    async_functions_.push(AsyncNativeFunctionSpec::typed(
        rstd::move(name), rstd::forward<F>(callable)));
  }

  template <typename Callable>
  void async_handle_function(String name, usize arity, Callable &&callable) {
    async_handle_functions_.push(AsyncNativeHandleFunctionSpec::make(
        rstd::move(name), arity, rstd::forward<Callable>(callable)));
  }
  // Capture frame arguments before returning the task; results retain Lua
  // identity.
  template <typename Callable>
  void async_values_function(String name, usize arity, Callable &&callable) {
    async_values_functions_.push(AsyncNativeValuesFunctionSpec::make(
        rstd::move(name), arity, rstd::forward<Callable>(callable)));
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
  void set(String name, Array value) {
    fields_.push(TableEntry{rstd::move(name), Value::Array(rstd::move(value))});
  }

private:
  String name_;
  Vec<NativeFunctionSpec> functions_;
  Vec<AsyncNativeFunctionSpec> async_functions_;
  Vec<AsyncNativeHandleFunctionSpec> async_handle_functions_;
  Vec<AsyncNativeValuesFunctionSpec> async_values_functions_;
  Vec<TableEntry> fields_;

  friend class State;
};

class NativeRequireModuleSpec {
public:
  NativeRequireModuleSpec(String require_name, String identity,
                          ModuleSpec module)
      : require_name_(rstd::move(require_name)),
        identity_(rstd::move(identity)), global_alias_(None()),
        module_(rstd::move(module)) {}

  NativeRequireModuleSpec(NativeRequireModuleSpec &&) noexcept = default;
  auto operator=(NativeRequireModuleSpec &&) noexcept
      -> NativeRequireModuleSpec & = default;

  void set_global_alias(String alias) {
    global_alias_ = Some(rstd::move(alias));
  }

private:
  String require_name_;
  String identity_;
  Option<String> global_alias_;
  ModuleSpec module_;

  friend class State;
};

} // namespace luato
