module;

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#undef lua_upvalueindex
#undef lua_call
#undef lua_pcall
#undef lua_yield
#undef lua_getextraspace
#undef lua_tonumber
#undef lua_tointeger
#undef lua_pop
#undef lua_newtable
#undef lua_register
#undef lua_pushcfunction
#undef lua_isfunction
#undef lua_istable
#undef lua_islightuserdata
#undef lua_isnil
#undef lua_isboolean
#undef lua_isthread
#undef lua_isnone
#undef lua_isnoneornil
#undef lua_pushliteral
#undef lua_pushglobaltable
#undef lua_tostring
#undef lua_insert
#undef lua_remove
#undef lua_replace
#undef lua_newuserdata
#undef lua_getuservalue
#undef lua_setuservalue
#undef lua_resetthread
#undef luaL_checkversion
#undef luaL_loadfile
#undef luaL_newlibtable
#undef luaL_newlib
#undef luaL_argcheck
#undef luaL_argexpected
#undef luaL_checkstring
#undef luaL_optstring
#undef luaL_typename
#undef luaL_dofile
#undef luaL_dostring
#undef luaL_getmetatable
#undef luaL_opt
#undef luaL_loadbuffer
#undef luaL_pushfail
#undef luaL_bufflen
#undef luaL_buffaddr
#undef luaL_addchar
#undef luaL_addsize
#undef luaL_buffsub
#undef luaL_prepbuffer
#undef luaL_openlibs

export module lua:functions;

export import :constants;
export import :types;

export {
    using ::lua_absindex;
    using ::lua_arith;
    using ::lua_atpanic;
    using ::lua_callk;
    using ::lua_checkstack;
    using ::lua_close;
    using ::lua_closeslot;
    using ::lua_closethread;
    using ::lua_compare;
    using ::lua_concat;
    using ::lua_copy;
    using ::lua_createtable;
    using ::lua_dump;
    using ::lua_error;
    using ::lua_gc;
    using ::lua_getallocf;
    using ::lua_getfield;
    using ::lua_getglobal;
    using ::lua_gethook;
    using ::lua_gethookcount;
    using ::lua_gethookmask;
    using ::lua_geti;
    using ::lua_getinfo;
    using ::lua_getiuservalue;
    using ::lua_getlocal;
    using ::lua_getmetatable;
    using ::lua_getstack;
    using ::lua_gettable;
    using ::lua_gettop;
    using ::lua_getupvalue;
    using ::lua_ident;
    using ::lua_iscfunction;
    using ::lua_isinteger;
    using ::lua_isnumber;
    using ::lua_isstring;
    using ::lua_isuserdata;
    using ::lua_isyieldable;
    using ::lua_len;
    using ::lua_load;
    using ::lua_newstate;
    using ::lua_newthread;
    using ::lua_newuserdatauv;
    using ::lua_next;
    using ::lua_numbertocstring;
    using ::lua_pcallk;
    using ::lua_pushboolean;
    using ::lua_pushcclosure;
    using ::lua_pushexternalstring;
    using ::lua_pushfstring;
    using ::lua_pushinteger;
    using ::lua_pushlightuserdata;
    using ::lua_pushlstring;
    using ::lua_pushnil;
    using ::lua_pushnumber;
    using ::lua_pushstring;
    using ::lua_pushthread;
    using ::lua_pushvalue;
    using ::lua_pushvfstring;
    using ::lua_rawequal;
    using ::lua_rawget;
    using ::lua_rawgeti;
    using ::lua_rawgetp;
    using ::lua_rawlen;
    using ::lua_rawset;
    using ::lua_rawseti;
    using ::lua_rawsetp;
    using ::lua_resume;
    using ::lua_rotate;
    using ::lua_setallocf;
    using ::lua_setfield;
    using ::lua_setglobal;
    using ::lua_sethook;
    using ::lua_seti;
    using ::lua_setiuservalue;
    using ::lua_setlocal;
    using ::lua_setmetatable;
    using ::lua_settable;
    using ::lua_settop;
    using ::lua_setupvalue;
    using ::lua_setwarnf;
    using ::lua_status;
    using ::lua_stringtonumber;
    using ::lua_toboolean;
    using ::lua_tocfunction;
    using ::lua_toclose;
    using ::lua_tointegerx;
    using ::lua_tolstring;
    using ::lua_tonumberx;
    using ::lua_topointer;
    using ::lua_tothread;
    using ::lua_touserdata;
    using ::lua_type;
    using ::lua_typename;
    using ::lua_upvalueid;
    using ::lua_upvaluejoin;
    using ::lua_version;
    using ::lua_warning;
    using ::lua_xmove;
    using ::lua_yieldk;

    using ::luaL_addgsub;
    using ::luaL_addlstring;
    using ::luaL_addstring;
    using ::luaL_addvalue;
    using ::luaL_alloc;
    using ::luaL_argerror;
    using ::luaL_buffinit;
    using ::luaL_buffinitsize;
    using ::luaL_callmeta;
    using ::luaL_checkany;
    using ::luaL_checkinteger;
    using ::luaL_checklstring;
    using ::luaL_checknumber;
    using ::luaL_checkoption;
    using ::luaL_checkstack;
    using ::luaL_checktype;
    using ::luaL_checkudata;
    using ::luaL_checkversion_;
    using ::luaL_error;
    using ::luaL_execresult;
    using ::luaL_fileresult;
    using ::luaL_getmetafield;
    using ::luaL_getsubtable;
    using ::luaL_gsub;
    using ::luaL_len;
    using ::luaL_loadbufferx;
    using ::luaL_loadfilex;
    using ::luaL_loadstring;
    using ::luaL_makeseed;
    using ::luaL_newmetatable;
    using ::luaL_newstate;
    using ::luaL_openselectedlibs;
    using ::luaL_optinteger;
    using ::luaL_optlstring;
    using ::luaL_optnumber;
    using ::luaL_prepbuffsize;
    using ::luaL_pushresult;
    using ::luaL_pushresultsize;
    using ::luaL_ref;
    using ::luaL_requiref;
    using ::luaL_setfuncs;
    using ::luaL_setmetatable;
    using ::luaL_testudata;
    using ::luaL_tolstring;
    using ::luaL_traceback;
    using ::luaL_typeerror;
    using ::luaL_unref;
    using ::luaL_where;

    using ::luaopen_base;
    using ::luaopen_coroutine;
    using ::luaopen_debug;
    using ::luaopen_io;
    using ::luaopen_math;
    using ::luaopen_os;
    using ::luaopen_package;
    using ::luaopen_string;
    using ::luaopen_table;
    using ::luaopen_utf8;

    inline constexpr auto lua_upvalueindex(int index) noexcept -> int {
        return LUA_REGISTRYINDEX - index;
    }

    inline void lua_call(lua_State* state, int argument_count, int result_count) {
        lua_callk(state, argument_count, result_count, 0, nullptr);
    }

    inline auto
    lua_pcall(lua_State* state, int argument_count, int result_count, int error_function) -> int {
        return lua_pcallk(state, argument_count, result_count, error_function, 0, nullptr);
    }

    inline auto lua_yield(lua_State* state, int result_count) -> int {
        return lua_yieldk(state, result_count, 0, nullptr);
    }

    inline auto lua_getextraspace(lua_State* state) noexcept -> void* {
        return static_cast<void*>(reinterpret_cast<char*>(state) - LUA_EXTRASPACE);
    }

    inline auto lua_tonumber(lua_State* state, int index) -> lua_Number {
        return lua_tonumberx(state, index, nullptr);
    }

    inline auto lua_tointeger(lua_State* state, int index) -> lua_Integer {
        return lua_tointegerx(state, index, nullptr);
    }

    inline void lua_pop(lua_State* state, int count) {
        lua_settop(state, -count - 1);
    }

    inline void lua_newtable(lua_State* state) {
        lua_createtable(state, 0, 0);
    }

    inline void lua_register(lua_State* state, char const* name, lua_CFunction function) {
        lua_pushcclosure(state, function, 0);
        lua_setglobal(state, name);
    }

    inline void lua_pushcfunction(lua_State* state, lua_CFunction function) {
        lua_pushcclosure(state, function, 0);
    }

    inline auto lua_isfunction(lua_State* state, int index) -> bool {
        return lua_type(state, index) == LUA_TFUNCTION;
    }

    inline auto lua_istable(lua_State* state, int index) -> bool {
        return lua_type(state, index) == LUA_TTABLE;
    }

    inline auto lua_islightuserdata(lua_State* state, int index) -> bool {
        return lua_type(state, index) == LUA_TLIGHTUSERDATA;
    }

    inline auto lua_isnil(lua_State* state, int index) -> bool {
        return lua_type(state, index) == LUA_TNIL;
    }

    inline auto lua_isboolean(lua_State* state, int index) -> bool {
        return lua_type(state, index) == LUA_TBOOLEAN;
    }

    inline auto lua_isthread(lua_State* state, int index) -> bool {
        return lua_type(state, index) == LUA_TTHREAD;
    }

    inline auto lua_isnone(lua_State* state, int index) -> bool {
        return lua_type(state, index) == LUA_TNONE;
    }

    inline auto lua_isnoneornil(lua_State* state, int index) -> bool {
        return lua_type(state, index) <= LUA_TNIL;
    }

    inline auto lua_pushliteral(lua_State* state, char const* value) -> char const* {
        return lua_pushstring(state, value);
    }

    inline void lua_pushglobaltable(lua_State* state) {
        static_cast<void>(lua_rawgeti(state, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS));
    }

    inline auto lua_tostring(lua_State* state, int index) -> char const* {
        return lua_tolstring(state, index, nullptr);
    }

    inline void lua_insert(lua_State* state, int index) {
        lua_rotate(state, index, 1);
    }

    inline void lua_remove(lua_State* state, int index) {
        lua_rotate(state, index, -1);
        lua_pop(state, 1);
    }

    inline void lua_replace(lua_State* state, int index) {
        lua_copy(state, -1, index);
        lua_pop(state, 1);
    }

    inline auto lua_newuserdata(lua_State* state, size_t size) -> void* {
        return lua_newuserdatauv(state, size, 1);
    }

    inline auto lua_getuservalue(lua_State* state, int index) -> int {
        return lua_getiuservalue(state, index, 1);
    }

    inline auto lua_setuservalue(lua_State* state, int index) -> int {
        return lua_setiuservalue(state, index, 1);
    }

    inline auto lua_resetthread(lua_State* state) -> int {
        return lua_closethread(state, nullptr);
    }

    inline void luaL_checkversion(lua_State* state) {
        luaL_checkversion_(state, LUA_VERSION_NUM, LUAL_NUMSIZES);
    }

    inline auto luaL_loadfile(lua_State* state, char const* file) -> int {
        return luaL_loadfilex(state, file, nullptr);
    }

    template<size_t Size>
    inline void luaL_newlibtable(lua_State* state, luaL_Reg const (&)[Size]) {
        lua_createtable(state, 0, static_cast<int>(Size - 1));
    }

    template<size_t Size>
    inline void luaL_newlib(lua_State* state, luaL_Reg const (&library)[Size]) {
        luaL_checkversion(state);
        luaL_newlibtable(state, library);
        luaL_setfuncs(state, library, 0);
    }

    inline void luaL_argcheck(lua_State* state, bool condition, int argument, char const* message) {
        if (! condition) static_cast<void>(luaL_argerror(state, argument, message));
    }

    inline void
    luaL_argexpected(lua_State* state, bool condition, int argument, char const* type_name) {
        if (! condition) static_cast<void>(luaL_typeerror(state, argument, type_name));
    }

    inline auto luaL_checkstring(lua_State* state, int argument) -> char const* {
        return luaL_checklstring(state, argument, nullptr);
    }

    inline auto luaL_optstring(lua_State* state, int argument, char const* fallback)
        -> char const* {
        return luaL_optlstring(state, argument, fallback, nullptr);
    }

    inline auto luaL_typename(lua_State* state, int index) -> char const* {
        return lua_typename(state, lua_type(state, index));
    }

    inline auto luaL_dofile(lua_State* state, char const* file) -> int {
        return luaL_loadfile(state, file) || lua_pcall(state, 0, LUA_MULTRET, 0);
    }

    inline auto luaL_dostring(lua_State* state, char const* source) -> int {
        return luaL_loadstring(state, source) || lua_pcall(state, 0, LUA_MULTRET, 0);
    }

    inline auto luaL_getmetatable(lua_State* state, char const* name) -> int {
        return lua_getfield(state, LUA_REGISTRYINDEX, name);
    }

    template<typename Function, typename Default>
    inline auto luaL_opt(lua_State* state, Function function, int argument, Default fallback)
        -> decltype(function(state, argument)) {
        if (lua_isnoneornil(state, argument)) return fallback;
        return function(state, argument);
    }

    inline auto luaL_loadbuffer(lua_State* state, char const* buffer, size_t size, char const* name)
        -> int {
        return luaL_loadbufferx(state, buffer, size, name, nullptr);
    }

    inline void luaL_pushfail(lua_State* state) {
        lua_pushnil(state);
    }

    inline auto luaL_bufflen(luaL_Buffer const* buffer) noexcept -> size_t {
        return buffer->n;
    }

    inline auto luaL_buffaddr(luaL_Buffer* buffer) noexcept -> char* {
        return buffer->b;
    }

    inline void luaL_addchar(luaL_Buffer* buffer, char value) {
        if (buffer->n >= buffer->size) static_cast<void>(luaL_prepbuffsize(buffer, 1));
        buffer->b[buffer->n++] = value;
    }

    inline void luaL_addsize(luaL_Buffer* buffer, size_t size) {
        buffer->n += size;
    }

    inline void luaL_buffsub(luaL_Buffer* buffer, size_t size) {
        buffer->n -= size;
    }

    inline auto luaL_prepbuffer(luaL_Buffer* buffer) -> char* {
        return luaL_prepbuffsize(buffer, LUAL_BUFFERSIZE);
    }

    inline void luaL_openlibs(lua_State* state) {
        luaL_openselectedlibs(state, ~0, 0);
    }
}
