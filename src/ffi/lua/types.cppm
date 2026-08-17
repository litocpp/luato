module;

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

export module luato:ffi.lua.types;

export {
  using ::lua_Alloc;
  using ::lua_CFunction;
  using ::lua_Debug;
  using ::lua_Hook;
  using ::lua_Integer;
  using ::lua_KContext;
  using ::lua_KFunction;
  using ::lua_Number;
  using ::lua_Reader;
  using ::lua_State;
  using ::lua_Unsigned;
  using ::lua_WarnFunction;
  using ::lua_Writer;
  using ::luaL_Buffer;
  using ::luaL_Reg;
  using ::luaL_Stream;
}
