module;
#include <rstd/enum.hpp>
export module luato:state;

export import :binding;
export import :module_loader;
export import :memory;

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace luato {

enum class StandardLibrary {
  Base,
  String,
  Table,
  Math,
  Utf8,
  Coroutine,
};

struct StateOptions {
  static constexpr auto restricted(u64 instructions = u64(1000000)) noexcept
      -> StateOptions {
    return StateOptions(true, true, true, true, true, true, instructions);
  }
  constexpr auto is_restricted() const noexcept -> bool { return restricted_; }
  constexpr auto instruction_limit() const noexcept -> u64 {
    return instructions_;
  }
  constexpr auto with_text_only(bool enabled = true) const noexcept
      -> StateOptions {
    auto result = *this;
    result.text_only_ = enabled;
    return result;
  }
  // The hook provides a cooperative limit, not isolation from untrusted code.
  constexpr auto with_instruction_limit(u64 limit) const noexcept
      -> StateOptions {
    auto result = *this;
    result.instructions_ = limit;
    return result;
  }
  constexpr auto text_only() const noexcept -> bool { return text_only_; }
  constexpr auto with_coroutine(bool enabled = true) const noexcept
      -> StateOptions {
    auto result = *this;
    result.open_coroutine_ = enabled;
    return result;
  }
  static constexpr auto none() noexcept -> StateOptions {
    return StateOptions(false, false, false, false, false);
  }
  static constexpr auto base() noexcept -> StateOptions {
    return StateOptions(true, false, false, false, false);
  }
  static constexpr auto build_script() noexcept -> StateOptions {
    return StateOptions(true, true, true, true, true);
  }

  constexpr auto contains(StandardLibrary library) const noexcept -> bool {
    switch (library) {
    case StandardLibrary::Base:
      return open_base_;
    case StandardLibrary::String:
      return open_string_;
    case StandardLibrary::Table:
      return open_table_;
    case StandardLibrary::Math:
      return open_math_;
    case StandardLibrary::Utf8:
      return open_utf8_;
    case StandardLibrary::Coroutine:
      return open_coroutine_;
    }
    return false;
  }

private:
  constexpr explicit StateOptions(bool open_base, bool open_string,
                                  bool open_table, bool open_math,
                                  bool open_utf8, bool restricted = false,
                                  u64 instructions = u64()) noexcept
      : open_base_(open_base), open_string_(open_string),
        open_table_(open_table), open_math_(open_math), open_utf8_(open_utf8),
        restricted_(restricted), text_only_(restricted),
        instructions_(instructions) {}

  bool open_base_;
  bool open_string_;
  bool open_table_;
  bool open_math_;
  bool open_utf8_;
  bool open_coroutine_{};
  bool restricted_;
  bool text_only_;
  u64 instructions_;
};

struct ExecutionReport {
  rstd::path::PathBuf source;
  rstd::time::Duration elapsed;
};

class MultiValue;
class LuaValue;
struct LuaValueAccess;
template <typename T> class UserDataSpec;

// Unlike empty, this accepts and discards any number of Lua return values.
struct IgnoreReturns {};

struct ReturnOptions {
  // Overrides return conversion failures, never Lua execution failures.
  Option<Error> conversion_error;
};

class UserDataHandle {
public:
  UserDataHandle(const UserDataHandle &) = delete;
  auto operator=(const UserDataHandle &) -> UserDataHandle & = delete;
  UserDataHandle(UserDataHandle &&other) noexcept;
  auto operator=(UserDataHandle &&other) noexcept -> UserDataHandle &;
  ~UserDataHandle();
  auto clone() const -> Result<UserDataHandle>;
  auto same_object(const UserDataHandle &other) const -> Result<bool>;
  template <typename T> auto is() const -> Result<bool> {
    return is_type(&userdata_type_key<T>);
  }

  template <typename T, typename F>
  auto with(F &&function) const
      -> decltype(function(rstd::declval<const T &>())) {
    return access<T, false>(rstd::forward<F>(function));
  }
  template <typename T, typename F>
  auto with_mut(F &&function) const
      -> decltype(function(rstd::declval<T &>())) {
    return access<T, true>(rstd::forward<F>(function));
  }
  template <typename T> auto take() const -> Result<T> {
    auto pointer = take_erased(&userdata_type_key<T>);
    if (pointer.is_err())
      return Err(rstd::move(pointer).unwrap_err());
    auto owned = Box<T>::from_raw(
        mut_ptr<T>::from_raw_parts(static_cast<T *>(*pointer)));
    return Ok(rstd::move(*owned));
  }

private:
  template <typename T, bool Mutable, typename F>
  auto access(F &&function) const -> decltype(function(
      rstd::declval<rstd::mtp::cond<Mutable, T, const T> &>())) {
    using Object = rstd::mtp::cond<Mutable, T, const T>;
    using Output = decltype(function(rstd::declval<Object &>()));
    static_assert(!rstd::mtp::is_ref<Output>,
                  "userdata borrows cannot escape the callback");
    static_assert(!rstd::mtp::is_ref<typename Output::value_type> &&
                      !rstd::mtp::is_ptr<typename Output::value_type>,
                  "userdata callbacks must return owned values");
    struct Context {
      F &function;
      Option<Output> output;
    } context{function, None()};
    auto result = access_erased(
        &userdata_type_key<T>, Mutable, &context, +[](void *raw, void *object) {
          auto &context = *static_cast<Context *>(raw);
          context.output =
              Some(context.function(*static_cast<Object *>(object)));
        });
    if (result.is_err())
      return Err(rstd::move(result).unwrap_err());
    return rstd::move(*context.output);
  }
  explicit UserDataHandle(void *handle) noexcept : handle_(handle) {}
  auto access_erased(const void *type, bool mutable_access, void *context,
                     void (*invoke)(void *, void *)) const -> Result<empty>;
  auto take_erased(const void *type) const -> Result<void *>;
  auto is_type(const void *type) const -> Result<bool>;
  void reset() noexcept;
  void *handle_;
  friend class State;
  friend struct LuaValueAccess;
};

class FunctionHandle {
public:
  // Handles do not keep their VM alive and must not be used concurrently.
  FunctionHandle(const FunctionHandle &) = delete;
  auto operator=(const FunctionHandle &) -> FunctionHandle & = delete;
  FunctionHandle(FunctionHandle &&other) noexcept;
  auto operator=(FunctionHandle &&other) noexcept -> FunctionHandle &;
  ~FunctionHandle();
  auto clone() const -> Result<FunctionHandle>;
  auto same_object(const FunctionHandle &other) const -> Result<bool>;
  auto call_multi(Array arguments) -> Result<Array>;
  // Arguments and results retain Lua object identity.
  auto call_values(MultiValue arguments) -> Result<MultiValue>;
  // Distinct handles may overlap on one thread while async calls are suspended.
  // Destroying a suspended call closes its Lua coroutine.
  auto call_async(MultiValue arguments)
      -> rstd::async::coro<Result<MultiValue>>;
  auto call_async_nested(MultiValue arguments)
      -> rstd::async::coro<Result<MultiValue>>;
  template <typename R, typename... Args> auto call(Args... args) -> Result<R>;
  template <typename R, typename... Args>
  auto call(ReturnOptions options, Args... args) -> Result<R>;
  template <typename R, typename... Args>
  auto call_async_typed(Args... args) -> rstd::async::coro<Result<R>>;
  template <typename R, typename... Args>
  auto call_async_typed(ReturnOptions options, Args... args)
      -> rstd::async::coro<Result<R>>;

private:
  auto call_values_impl(MultiValue arguments, bool ignore_returns)
      -> Result<MultiValue>;
  auto call_async_owned(MultiValue arguments, bool ignore_returns)
      -> rstd::async::coro<Result<MultiValue>>;
  static auto call_async_impl(FunctionHandle handle, MultiValue arguments,
                              bool nested, bool ignore_returns)
      -> rstd::async::coro<Result<MultiValue>>;
  explicit FunctionHandle(void *handle) noexcept : handle_(handle) {}
  void reset() noexcept;
  void *handle_;
  friend class TableHandle;
  friend struct LuaValueAccess;
};

class TableHandle {
public:
  TableHandle(const TableHandle &) = delete;
  auto operator=(const TableHandle &) -> TableHandle & = delete;
  TableHandle(TableHandle &&other) noexcept;
  auto operator=(TableHandle &&other) noexcept -> TableHandle &;
  ~TableHandle();
  auto clone() const -> Result<TableHandle>;
  auto get_function(ref<str> name) -> Result<FunctionHandle>;
  auto raw_get_function(ref<str> name) -> Result<FunctionHandle>;
  auto get_table(ref<str> name) -> Result<TableHandle>;
  auto raw_get_table(ref<str> name) -> Result<TableHandle>;
  auto same_object(const TableHandle &other) const -> Result<bool>;
  auto same_object_in_call(const TableHandle &other) const -> Result<bool>;
  auto get_value(ref<str> name) -> Result<LuaValue>;
  auto raw_get_value(ref<str> name) -> Result<LuaValue>;
  auto get(const LuaValue &key) -> Result<LuaValue>;
  auto raw_get(const LuaValue &key) -> Result<LuaValue>;
  auto set(const LuaValue &key, const LuaValue &value) -> Result<empty>;
  auto metatable() -> Result<Option<TableHandle>>;
  auto metatable_in_call() -> Result<Option<TableHandle>>;
  auto set_metatable(const TableHandle *value) -> Result<empty>;
  auto set_metatable_in_call(const TableHandle *value) -> Result<empty>;
  auto length() -> Result<usize>;
  auto keys() -> Result<Vec<LuaValue>>;
  auto to_table() -> Result<Table>;
  auto get_in_call(const LuaValue &key) -> Result<LuaValue>;
  auto set_in_call(const LuaValue &key, const LuaValue &value) -> Result<empty>;
  // Scalar field lookups honor __index and Lua string/number coercion.
  auto string_field_in_call(ref<str> name) -> Result<Option<String>>;
  auto number_field_in_call(ref<str> name) -> Result<Option<f64>>;
  // Creates an error value through the same owner as native callback failures.
  auto set_error_in_call(const LuaValue &key, const Error &error)
      -> Result<empty>;
  // Only recognizes error values created by this owner, not ordinary Lua
  // tables.
  auto error_in_call() -> Result<Option<Error>>;
  auto to_value() -> Result<Value>;
  auto raw_set(const LuaValue &key, const LuaValue &value) -> Result<empty>;

private:
  explicit TableHandle(void *handle) noexcept : handle_(handle) {}
  void reset() noexcept;
  auto get_function_impl(ref<str> name, bool raw) -> Result<FunctionHandle>;
  auto get_table_impl(ref<str> name, bool raw) -> Result<TableHandle>;
  auto get_value_impl(ref<str> name, bool raw) -> Result<LuaValue>;
  auto get_impl(const LuaValue &key, bool raw, bool in_call = false)
      -> Result<LuaValue>;
  auto metatable_impl(bool in_call) -> Result<Option<TableHandle>>;
  auto set_metatable_impl(const TableHandle *value, bool in_call)
      -> Result<empty>;
  auto set_impl(const LuaValue &key, const LuaValue &value, bool raw,
                bool in_call = false) -> Result<empty>;
  auto scalar_field_in_call(ref<str> name, bool number) -> Result<LuaValue>;
  void *handle_;
  friend class State;
  friend struct LuaValueAccess;
};

// Bytes are owned and may contain invalid UTF-8; tables and functions are
// handles.
class LuaValue {
  RSTD_ENUM(LuaValue, (Nil, ()), (Boolean, (bool value;)),
            (Integer, (i64 value;)), (Number, (f64 value;)),
            (Bytes, (Vec<u8> value;)), (Table, (TableHandle value;)),
            (Function, (FunctionHandle value;)),
            (UserData, (UserDataHandle value;)), (Opaque, (const void *value;)))
public:
  LuaValue(const LuaValue &) = delete;
  auto operator=(const LuaValue &) -> LuaValue & = delete;
  LuaValue(LuaValue &&) noexcept = default;
  auto operator=(LuaValue &&) noexcept -> LuaValue & = default;
  auto clone() const -> Result<LuaValue>;
  // The returned view borrows this value's bytes.
  auto as_utf8() const & -> Result<ref<str>>;
  auto as_utf8() const && -> Result<ref<str>> = delete;
};

class MultiValue {
public:
  static auto make() -> MultiValue { return MultiValue(Vec<LuaValue>::make()); }
  static auto from(Vec<LuaValue> values) -> MultiValue {
    return MultiValue(rstd::move(values));
  }
  auto values() const noexcept -> slice<LuaValue> { return values_.as_slice(); }
  auto len() const noexcept -> usize { return values_.len(); }
  auto is_empty() const noexcept -> bool { return values_.is_empty(); }
  auto into_values() && -> Vec<LuaValue> { return rstd::move(values_); }
  void push(LuaValue value) { values_.push(rstd::move(value)); }

private:
  explicit MultiValue(Vec<LuaValue> values) : values_(rstd::move(values)) {}
  Vec<LuaValue> values_;
};

class State {
public:
  State(const State &) = delete;
  State &operator=(const State &) = delete;

  State(State &&other) noexcept;
  auto operator=(State &&other) noexcept -> State &;
  ~State();

  static auto create(StateOptions options, MemoryOptions memory = {})
      -> Result<State>;

  auto register_module(ModuleSpec module) -> Result<empty>;
  auto register_native_require_module(NativeRequireModuleSpec module)
      -> Result<empty>;
  auto set_module_resolver(ModuleResolverSpec resolver) -> Result<empty>;
  auto execute_entry(LuaModuleSource source) -> Result<ExecutionReport>;
  auto evaluate_entry(LuaModuleSource source) -> Result<MultiValue>;
  auto execute_file(ref<rstd::path::Path> path) -> Result<ExecutionReport>;
  auto module_table(ref<str> module) -> Result<TableHandle>;
  auto globals() -> Result<TableHandle>;
  auto create_table(TableCapacity capacity = {}) -> Result<TableHandle>;
  auto validate_value(const LuaValue &value) const -> Result<empty>;
  template <typename T>
  auto register_userdata(UserDataSpec<T> spec) -> Result<empty>;
  template <typename T>
  auto create_userdata(T value) -> Result<UserDataHandle> {
    return create_userdata_erased(
        &userdata_type_key<T>, new T(rstd::move(value)),
        +[](void *object) { delete static_cast<T *>(object); });
  }
  // Copies tables recursively; empty Lua tables decode as Table, not Array.
  auto snapshot(const LuaValue &value) -> Result<Value>;
  // Lua removes nil table entries; snapshots containing them are rejected.
  auto from_snapshot(const Value &value) -> Result<LuaValue>;
  auto call_module(ref<str> module, ref<str> function, Array arguments)
      -> Result<Option<Value>>;
  // Values are snapshots; the function is obtained using a raw table lookup.
  auto call_module_multi(ref<str> module, ref<str> function, Array arguments)
      -> Result<Array>;
  // Counts Lua allocator bytes, excluding host values and callbacks.
  auto used_memory() const -> Result<usize>;
  // Zero disables the limit; returns the previous limit.
  auto set_memory_limit(usize limit) -> Result<usize>;
  auto memory_statistics() const -> Result<MemoryStatistics>;
  auto memory_observer() const -> Result<MemoryObserver>;
  auto trim_memory(usize retain = {}) const
      -> Result<rstd::alloc::PoolTrimReport>;
  auto collect_garbage() -> Result<empty>;
  auto loaded_modules() const noexcept -> slice<LoadedModule>;

private:
  explicit State(void *storage) noexcept : storage_(storage) {}
  static int dispatch(void *lua_state);
  static int dispatch_async(void *lua_state);
  static int dispatch_async_handle(void *lua_state);
  static int restore_async(void *lua_state, void *operation);
  auto register_module_table(ModuleSpec module, Option<String> global_name,
                             Option<String> native_identity,
                             const void *userdata_type = nullptr,
                             Vec<String> field_getters = {}) -> Result<empty>;
  auto create_userdata_erased(const void *type, void *object,
                              void (*destroy)(void *))
      -> Result<UserDataHandle>;
  void reset() noexcept;
  auto call_module_impl(ref<str> module, ref<str> function, Array arguments,
                        bool multi) -> Result<Vec<Value>>;
  auto execute_entry_impl(LuaModuleSource source, MultiValue *values)
      -> Result<ExecutionReport>;

  void *storage_;
};

auto take_lua_value(OpaqueHandle handle) -> LuaValue;

template <typename T>
auto CallFrame::create_userdata(T value) -> Result<UserDataHandle> {
  auto result = new_userdata_(
      context_, &userdata_type_key<T>, new T(rstd::move(value)),
      +[](void *object) { delete static_cast<T *>(object); });
  if (result.is_err())
    return Err(rstd::move(result).unwrap_err());
  auto object = take_lua_value(*result);
  return Ok(rstd::move(object.as_UserData().value));
}

} // namespace luato
