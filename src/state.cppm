module;

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

export module luato:state;

export import :binding;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace luato
{

enum class StandardLibrary
{
    Base,
};

struct StateOptions {
    static constexpr auto none() noexcept -> StateOptions { return StateOptions(false); }
    static constexpr auto base() noexcept -> StateOptions { return StateOptions(true); }

    constexpr auto contains(StandardLibrary library) const noexcept -> bool {
        return library == StandardLibrary::Base && open_base_;
    }

private:
    constexpr explicit StateOptions(bool open_base) noexcept: open_base_(open_base) {}

    bool open_base_;
};

struct ExecutionReport {
    rstd::path::PathBuf  source;
    rstd::time::Duration elapsed;
};

class State {
public:
    State(const State&)            = delete;
    State& operator=(const State&) = delete;

    State(State&& other) noexcept;
    auto operator=(State&& other) noexcept -> State&;
    ~State();

    static auto create(StateOptions options) -> Result<State>;

    auto register_module(ModuleSpec module) -> Result<empty>;
    auto execute_file(ref<rstd::path::Path> path) -> Result<ExecutionReport>;

private:
    explicit State(void* storage) noexcept: storage_(storage) {}
    static int dispatch(void* lua_state);
    void       reset() noexcept;

    void* storage_;
};

} // namespace luato

namespace luato
{

enum class PendingValueKind
{
    Integer,
    Boolean,
    String,
};

struct PendingValue {
    PendingValueKind kind { PendingValueKind::Integer };
    i64              integer {};
    bool             boolean { false };
    String           string {};

    PendingValue() noexcept                          = default;
    PendingValue(PendingValue&&) noexcept            = default;
    PendingValue& operator=(PendingValue&&) noexcept = default;

    static auto from_integer(i64 value) -> PendingValue {
        PendingValue result;
        result.kind    = PendingValueKind::Integer;
        result.integer = value;
        return result;
    }

    static auto from_boolean(bool value) -> PendingValue {
        PendingValue result;
        result.kind    = PendingValueKind::Boolean;
        result.boolean = value;
        return result;
    }

    static auto from_string(String value) -> PendingValue {
        PendingValue result;
        result.kind   = PendingValueKind::String;
        result.string = rstd::move(value);
        return result;
    }
};

struct CallbackSlot {
    String              name;
    String              source;
    usize               arity;
    Box<NativeCallback> callback;
    Vec<PendingValue>   returns;
    Option<Error>       pending_error;

    CallbackSlot(String name, String source, usize arity, Box<NativeCallback> callback)
        : name(rstd::move(name)),
          source(rstd::move(source)),
          arity(arity),
          callback(rstd::move(callback)),
          returns(Vec<PendingValue>::make()),
          pending_error(None()) {}
};

struct StateStorage {
    lua_State*             lua;
    Vec<Box<CallbackSlot>> callbacks;
    Vec<String>            modules;

    explicit StateStorage(lua_State* state)
        : lua(state), callbacks(Vec<Box<CallbackSlot>>::make()), modules(Vec<String>::make()) {}

    ~StateStorage() {
        if (lua != nullptr) lua_close(lua);
    }
};

struct Invocation {
    lua_State*    lua;
    CallbackSlot* slot;
};

struct RegistrationRequest {
    StateStorage* storage;
    String const* module_name;
    usize         first_slot;
    usize         slot_count;
    lua_CFunction dispatcher;
};

constexpr int ERROR_TAG       = 1;
constexpr int ERROR_KIND      = 2;
constexpr int ERROR_SOURCE    = 3;
constexpr int ERROR_MESSAGE   = 4;
constexpr int ERROR_TRACEBACK = 5;

char error_marker;

auto copied_utf8(char const* bytes, size_t length) -> Option<String> {
    auto value = Vec<u8>::with_capacity(usize(length));
    for (size_t index = 0; index < length; ++index) {
        value.push(u8(static_cast<uint8_t>(static_cast<unsigned char>(bytes[index]))));
    }
    auto decoded = String::from_utf8(rstd::move(value));
    if (decoded.is_err()) return None();
    return Some(rstd::move(decoded).unwrap_unchecked());
}

auto copied_lua_string(lua_State* state, int index, ref<str> fallback) -> String {
    size_t      length {};
    char const* bytes = lua_tolstring(state, index, &length);
    if (bytes == nullptr) return String::make(fallback);
    auto value = copied_utf8(bytes, length);
    if (value.is_none()) return String::make("Lua value is not valid UTF-8"_str);
    return value.take().unwrap_unchecked();
}

void push_string(lua_State* state, String const& value) {
    auto view = value.as_str();
    lua_pushlstring(state, reinterpret_cast<char const*>(view.data()), view.len().to_primitive());
}

auto error_kind_from_integer(lua_Integer value, ErrorKind fallback) noexcept -> ErrorKind {
    switch (value) {
    case static_cast<lua_Integer>(ErrorKind::StateCreation): return ErrorKind::StateCreation;
    case static_cast<lua_Integer>(ErrorKind::Bootstrap): return ErrorKind::Bootstrap;
    case static_cast<lua_Integer>(ErrorKind::File): return ErrorKind::File;
    case static_cast<lua_Integer>(ErrorKind::Syntax): return ErrorKind::Syntax;
    case static_cast<lua_Integer>(ErrorKind::Runtime): return ErrorKind::Runtime;
    case static_cast<lua_Integer>(ErrorKind::Memory): return ErrorKind::Memory;
    case static_cast<lua_Integer>(ErrorKind::Binding): return ErrorKind::Binding;
    case static_cast<lua_Integer>(ErrorKind::Type): return ErrorKind::Type;
    case static_cast<lua_Integer>(ErrorKind::PanicInvariant): return ErrorKind::PanicInvariant;
    default: return fallback;
    }
}

auto is_luato_error(lua_State* state, int index) noexcept -> bool {
    if (! lua_istable(state, index)) return false;
    index = lua_absindex(state, index);
    lua_rawgeti(state, index, ERROR_TAG);
    auto tagged = lua_touserdata(state, -1) == static_cast<void*>(&error_marker);
    lua_pop(state, 1);
    return tagged;
}

auto error_field(lua_State* state, int object, int field, ref<str> fallback) -> String {
    lua_rawgeti(state, object, field);
    auto result = copied_lua_string(state, -1, fallback);
    lua_pop(state, 1);
    return result;
}

auto error_from_top(lua_State* state, ErrorKind default_kind, String default_source) -> Error {
    auto object = lua_absindex(state, -1);
    if (! is_luato_error(state, object)) {
        return { default_kind,
                 rstd::move(default_source),
                 copied_lua_string(state, object, "unknown Lua error"_str),
                 String::make() };
    }

    lua_rawgeti(state, object, ERROR_KIND);
    auto kind = lua_isinteger(state, -1)
                    ? error_kind_from_integer(lua_tointeger(state, -1), default_kind)
                    : default_kind;
    lua_pop(state, 1);

    auto source = error_field(state, object, ERROR_SOURCE, ""_str);
    if (source.is_empty()) source = rstd::move(default_source);
    auto message   = error_field(state, object, ERROR_MESSAGE, "unknown Lua error"_str);
    auto traceback = error_field(state, object, ERROR_TRACEBACK, ""_str);
    return { kind, rstd::move(source), rstd::move(message), rstd::move(traceback) };
}

int traceback_handler(lua_State* state) {
    if (is_luato_error(state, 1)) {
        lua_rawgeti(state, 1, ERROR_MESSAGE);
        size_t      length {};
        char const* message = lua_tolstring(state, -1, &length);
        if (message == nullptr) message = "unknown Lua error";
        luaL_traceback(state, state, message, 1);
        lua_rawseti(state, 1, ERROR_TRACEBACK);
        lua_settop(state, 1);
        return 1;
    }

    size_t      length {};
    char const* message = lua_tolstring(state, 1, &length);
    if (message == nullptr) {
        message = "Lua error object is not a string";
        length  = sizeof("Lua error object is not a string") - 1;
    }

    lua_createtable(state, 5, 0);
    auto object = lua_gettop(state);
    lua_pushlightuserdata(state, static_cast<void*>(&error_marker));
    lua_rawseti(state, object, ERROR_TAG);
    lua_pushinteger(state, static_cast<lua_Integer>(ErrorKind::Runtime));
    lua_rawseti(state, object, ERROR_KIND);
    lua_pushliteral(state, "");
    lua_rawseti(state, object, ERROR_SOURCE);
    lua_pushlstring(state, message, length);
    lua_rawseti(state, object, ERROR_MESSAGE);
    luaL_traceback(state, state, message, 1);
    lua_rawseti(state, object, ERROR_TRACEBACK);
    lua_replace(state, 1);
    return 1;
}

int bootstrap_state(lua_State* state) {
    auto libraries = lua_toboolean(state, 1) ? LUA_GLIBK : 0;
    luaL_openselectedlibs(state, libraries, 0);
    return 0;
}

int register_module(lua_State* state) {
    auto* request = static_cast<RegistrationRequest*>(lua_touserdata(state, 1));
    lua_createtable(state, 0, static_cast<int>(request->slot_count.to_primitive()));
    auto module = lua_gettop(state);

    for (auto offset = usize(); offset < request->slot_count; ++offset) {
        auto* slot = request->storage->callbacks[request->first_slot + offset].get();
        push_string(state, slot->name);
        lua_pushlightuserdata(state, static_cast<void*>(slot));
        lua_pushcclosure(state, request->dispatcher, 1);
        lua_rawset(state, module);
    }

    lua_pushglobaltable(state);
    auto globals = lua_gettop(state);
    push_string(state, *request->module_name);
    lua_pushvalue(state, module);
    lua_rawset(state, globals);
    return 0;
}

auto invocation_type_error(Invocation* invocation, usize index, ref<str> expected) -> Error {
    return { ErrorKind::Type,
             invocation->slot->source.clone(),
             rstd::format("{} argument {} must be {}",
                          invocation->slot->source.as_str(),
                          index + usize(1),
                          expected),
             String::make() };
}

auto read_i64(void* context, usize index) -> Result<i64> {
    auto* invocation = static_cast<Invocation*>(context);
    if (index >= usize(lua_gettop(invocation->lua)) ||
        ! lua_isinteger(invocation->lua, static_cast<int>(index.to_primitive() + 1))) {
        return Err(invocation_type_error(invocation, index, "an integer"_str));
    }

    auto value = rstd::try_from<i64>(
        lua_tointeger(invocation->lua, static_cast<int>(index.to_primitive() + 1)));
    if (value.is_err()) {
        return Err(invocation_type_error(invocation, index, "an i64 integer"_str));
    }
    return Ok(rstd::move(value).unwrap_unchecked());
}

auto read_string(void* context, usize index) -> Result<String> {
    auto* invocation = static_cast<Invocation*>(context);
    auto  lua_index  = static_cast<int>(index.to_primitive() + 1);
    if (index >= usize(lua_gettop(invocation->lua)) ||
        lua_type(invocation->lua, lua_index) != LUA_TSTRING) {
        return Err(invocation_type_error(invocation, index, "a string"_str));
    }

    size_t      length {};
    char const* bytes = lua_tolstring(invocation->lua, lua_index, &length);
    auto        value = copied_utf8(bytes, length);
    if (value.is_none()) {
        return Err(invocation_type_error(invocation, index, "a UTF-8 string"_str));
    }
    return Ok(value.take().unwrap_unchecked());
}

void stage_i64(void* context, i64 value) {
    auto* invocation = static_cast<Invocation*>(context);
    invocation->slot->returns.push(PendingValue::from_integer(value));
}

void stage_bool(void* context, bool value) {
    auto* invocation = static_cast<Invocation*>(context);
    invocation->slot->returns.push(PendingValue::from_boolean(value));
}

void stage_string(void* context, String value) {
    auto* invocation = static_cast<Invocation*>(context);
    invocation->slot->returns.push(PendingValue::from_string(rstd::move(value)));
}

int raise_pending_error(lua_State* state, CallbackSlot* slot) {
    auto* error = rstd::addressof(*slot->pending_error);
    lua_createtable(state, 5, 0);
    auto object = lua_gettop(state);
    lua_pushlightuserdata(state, static_cast<void*>(&error_marker));
    lua_rawseti(state, object, ERROR_TAG);
    lua_pushinteger(state, static_cast<lua_Integer>(error->kind));
    lua_rawseti(state, object, ERROR_KIND);
    push_string(state, error->source);
    lua_rawseti(state, object, ERROR_SOURCE);
    push_string(state, error->message);
    lua_rawseti(state, object, ERROR_MESSAGE);
    push_string(state, error->traceback);
    lua_rawseti(state, object, ERROR_TRACEBACK);
    return lua_error(state);
}

int push_pending_values(lua_State* state, CallbackSlot* slot, int result_count) {
    for (auto index = usize(); index < slot->returns.len(); ++index) {
        auto* value = rstd::addressof(slot->returns[index]);
        switch (value->kind) {
        case PendingValueKind::Integer:
            lua_pushinteger(state, static_cast<lua_Integer>(value->integer.to_primitive()));
            break;
        case PendingValueKind::Boolean: lua_pushboolean(state, value->boolean); break;
        case PendingValueKind::String: push_string(state, value->string); break;
        }
    }
    return result_count;
}

void clear_callback_transients(StateStorage* storage) {
    for (auto index = usize(); index < storage->callbacks.len(); ++index) {
        auto* slot          = storage->callbacks[index].get();
        slot->pending_error = None();
        slot->returns.clear();
    }
}

auto moved_state_error() -> Error {
    return Error::make(ErrorKind::PanicInvariant,
                       String::make(),
                       String::make("operation attempted on moved-from Luato state"_str));
}

State::State(State&& other) noexcept: storage_(other.storage_) {
    other.storage_ = nullptr;
}

auto State::operator=(State&& other) noexcept -> State& {
    if (this != rstd::addressof(other)) {
        reset();
        storage_       = other.storage_;
        other.storage_ = nullptr;
    }
    return *this;
}

State::~State() {
    reset();
}

void State::reset() noexcept {
    if (storage_ == nullptr) return;
    auto* raw  = static_cast<StateStorage*>(storage_);
    storage_   = nullptr;
    auto owned = Box<StateStorage>::from_raw(mut_ptr<StateStorage>::from_raw_parts(raw));
}

auto State::create(StateOptions options) -> Result<State> {
    auto* lua = luaL_newstate();
    if (lua == nullptr) {
        return Err(Error::make(ErrorKind::StateCreation,
                               String::make(),
                               String::make("Lua state allocation failed"_str)));
    }

    auto storage                                         = Box<StateStorage>::make(lua);
    *static_cast<StateStorage**>(lua_getextraspace(lua)) = storage.get();
    lua_atpanic(
        lua, +[](lua_State*) -> int {
            return 0;
        });

    if (! lua_checkstack(lua, 2)) {
        return Err(Error::make(ErrorKind::Memory,
                               String::make(),
                               String::make("Lua bootstrap stack allocation failed"_str)));
    }
    lua_pushcfunction(lua, bootstrap_state);
    lua_pushboolean(lua, options.contains(StandardLibrary::Base));
    auto status = lua_pcall(lua, 1, 0, 0);
    if (status != LUA_OK) {
        auto kind  = status == LUA_ERRMEM ? ErrorKind::Memory : ErrorKind::Bootstrap;
        auto error = Error::make(
            kind, String::make(), copied_lua_string(lua, -1, "Lua bootstrap failed"_str));
        lua_settop(lua, 0);
        return Err(rstd::move(error));
    }

    lua_settop(lua, 0);
    auto raw = rstd::move(storage).into_raw().as_raw_ptr();
    return Ok(State(static_cast<void*>(raw)));
}

auto State::register_module(ModuleSpec module) -> Result<empty> {
    if (storage_ == nullptr) return Err(moved_state_error());
    auto* storage = static_cast<StateStorage*>(storage_);
    auto* lua     = storage->lua;
    auto  old_top = lua_gettop(lua);

    if (module.name_.is_empty()) {
        return Err(Error::make(
            ErrorKind::Binding, String::make(), String::make("module name cannot be empty"_str)));
    }
    for (auto index = usize(); index < storage->modules.len(); ++index) {
        if (storage->modules[index] == module.name_.as_str()) {
            return Err(Error::make(
                ErrorKind::Binding,
                module.name_.clone(),
                rstd::format("module '{}' is already registered", module.name_.as_str())));
        }
    }
    for (auto index = usize(); index < module.functions_.len(); ++index) {
        if (module.functions_[index].name_.is_empty()) {
            return Err(Error::make(ErrorKind::Binding,
                                   module.name_.clone(),
                                   String::make("function name cannot be empty"_str)));
        }
        for (auto other = usize(); other < index; ++other) {
            if (module.functions_[other].name_ == module.functions_[index].name_.as_str()) {
                return Err(Error::make(ErrorKind::Binding,
                                       module.name_.clone(),
                                       rstd::format("duplicate function '{}.{}'",
                                                    module.name_.as_str(),
                                                    module.functions_[index].name_.as_str())));
            }
        }
    }

    auto first_slot = storage->callbacks.len();
    for (auto index = usize(); index < module.functions_.len(); ++index) {
        auto& function = module.functions_[index];
        auto  source   = rstd::format("{}.{}", module.name_.as_str(), function.name_.as_str());
        storage->callbacks.push(Box<CallbackSlot>::make(rstd::move(function.name_),
                                                        rstd::move(source),
                                                        function.arity_,
                                                        rstd::move(function.callback_)));
    }

    auto dispatcher = +[](lua_State* state) -> int {
        return State::dispatch(static_cast<void*>(state));
    };
    auto request = RegistrationRequest {
        storage, rstd::addressof(module.name_), first_slot, module.functions_.len(), dispatcher
    };

    if (! lua_checkstack(lua, 2)) {
        storage->callbacks.truncate(first_slot);
        return Err(Error::make(ErrorKind::Memory,
                               module.name_.clone(),
                               String::make("Lua registration stack allocation failed"_str)));
    }
    lua_pushcfunction(lua, luato::register_module);
    lua_pushlightuserdata(lua, static_cast<void*>(rstd::addressof(request)));
    auto status = lua_pcall(lua, 1, 0, 0);
    if (status != LUA_OK) {
        auto kind  = status == LUA_ERRMEM ? ErrorKind::Memory : ErrorKind::Binding;
        auto error = Error::make(kind,
                                 module.name_.clone(),
                                 copied_lua_string(lua, -1, "module registration failed"_str));
        storage->callbacks.truncate(first_slot);
        lua_settop(lua, old_top);
        return Err(rstd::move(error));
    }

    storage->modules.push(rstd::move(module.name_));
    lua_settop(lua, old_top);
    return Ok(empty {});
}

int State::dispatch(void* lua_state) {
    auto* lua           = static_cast<lua_State*>(lua_state);
    auto* slot          = static_cast<CallbackSlot*>(lua_touserdata(lua, lua_upvalueindex(1)));
    slot->pending_error = None();
    slot->returns.clear();

    auto actual = usize(lua_gettop(lua));
    if (actual != slot->arity) {
        slot->pending_error = Some(Error {
            ErrorKind::Type,
            slot->source.clone(),
            rstd::format(
                "{} expects {} arguments, received {}", slot->source.as_str(), slot->arity, actual),
            String::make(),
        });
        return raise_pending_error(lua, slot);
    }

    usize result_count {};
    {
        auto invocation = Invocation { lua, slot };
        auto frame      = CallFrame(static_cast<void*>(rstd::addressof(invocation)),
                                    actual,
                                    read_i64,
                                    read_string,
                                    stage_i64,
                                    stage_bool,
                                    stage_string);
        auto result     = slot->callback->operator()(frame);
        if (result.is_err()) {
            auto error = rstd::move(result).unwrap_err_unchecked();
            if (error.source.is_empty()) error.source = slot->source.clone();
            slot->pending_error = Some(rstd::move(error));
        } else {
            result_count = rstd::move(result).unwrap_unchecked();
        }
    }

    if (slot->pending_error.is_some()) return raise_pending_error(lua, slot);
    if (result_count != slot->returns.len()) {
        slot->pending_error = Some(Error {
            ErrorKind::Binding,
            slot->source.clone(),
            rstd::format("{} declared {} results but staged {}",
                         slot->source.as_str(),
                         result_count,
                         slot->returns.len()),
            String::make(),
        });
        return raise_pending_error(lua, slot);
    }

    int  count {};
    bool count_overflow {};
    {
        auto native_count = rstd::try_from<int>(result_count);
        if (native_count.is_err()) {
            count_overflow = true;
        } else {
            count = rstd::move(native_count).unwrap_unchecked();
        }
    }
    if (count_overflow) {
        slot->pending_error = Some(Error {
            ErrorKind::Memory,
            slot->source.clone(),
            String::make("Lua result stack allocation failed"_str),
            String::make(),
        });
        return raise_pending_error(lua, slot);
    }
    if (! lua_checkstack(lua, count)) {
        slot->pending_error = Some(Error {
            ErrorKind::Memory,
            slot->source.clone(),
            String::make("Lua result stack allocation failed"_str),
            String::make(),
        });
        return raise_pending_error(lua, slot);
    }
    return push_pending_values(lua, slot, count);
}

auto State::execute_file(ref<rstd::path::Path> path) -> Result<ExecutionReport> {
    if (storage_ == nullptr) return Err(moved_state_error());
    auto* storage = static_cast<StateStorage*>(storage_);
    auto* lua     = storage->lua;
    auto  old_top = lua_gettop(lua);
    auto  source  = path.to_string_lossy();
    auto  c_path  = path.to_cstring();
    if (c_path.is_err()) {
        return Err(Error::make(ErrorKind::File,
                               rstd::move(source),
                               String::make("script path contains an interior nul"_str)));
    }

    auto started    = rstd::time::Instant::now();
    auto owned_path = rstd::move(c_path).unwrap_unchecked();
    auto status     = luaL_loadfilex(lua, owned_path.as_ptr(), nullptr);
    if (status != LUA_OK) {
        auto kind  = status == LUA_ERRFILE     ? ErrorKind::File
                     : status == LUA_ERRSYNTAX ? ErrorKind::Syntax
                     : status == LUA_ERRMEM    ? ErrorKind::Memory
                                               : ErrorKind::Runtime;
        auto error = Error::make(
            kind, rstd::move(source), copied_lua_string(lua, -1, "Lua file load failed"_str));
        lua_settop(lua, old_top);
        return Err(rstd::move(error));
    }

    if (! lua_checkstack(lua, 1)) {
        lua_settop(lua, old_top);
        return Err(Error::make(ErrorKind::Memory,
                               rstd::move(source),
                               String::make("Lua execution stack allocation failed"_str)));
    }
    lua_pushcfunction(lua, traceback_handler);
    lua_insert(lua, old_top + 1);
    auto message_handler = old_top + 1;
    status               = lua_pcall(lua, 0, LUA_MULTRET, message_handler);
    if (status != LUA_OK) {
        auto default_kind = status == LUA_ERRMEM ? ErrorKind::Memory : ErrorKind::Runtime;
        auto error        = error_from_top(lua, default_kind, rstd::move(source));
        clear_callback_transients(storage);
        lua_settop(lua, old_top);
        return Err(rstd::move(error));
    }

    clear_callback_transients(storage);
    lua_settop(lua, old_top);
    return Ok(ExecutionReport { rstd::path::PathBuf::from(path), started.elapsed() });
}

} // namespace luato
