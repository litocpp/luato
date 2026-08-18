if(TARGET Lua::Lua)
  return()
endif()

find_package(Lua 5.5.1 QUIET)
if(Lua_FOUND)
  if(NOT TARGET Lua::Lua)
    add_library(luato.lua INTERFACE)
    add_library(Lua::Lua ALIAS luato.lua)
    target_include_directories(luato.lua SYSTEM INTERFACE ${LUA_INCLUDE_DIR})
    target_link_libraries(luato.lua INTERFACE ${LUA_LIBRARIES})
  endif()
  return()
endif()

include(FetchContent)
FetchContent_Declare(
  lua
  URL https://www.lua.org/ftp/lua-5.5.1.tar.gz
  URL_HASH SHA256=1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(lua)

set(LUATO_LUA_SOURCE_DIR "${lua_SOURCE_DIR}/src")
set(LUATO_LUA_SOURCES
    ${LUATO_LUA_SOURCE_DIR}/lapi.c
    ${LUATO_LUA_SOURCE_DIR}/lcode.c
    ${LUATO_LUA_SOURCE_DIR}/lctype.c
    ${LUATO_LUA_SOURCE_DIR}/ldebug.c
    ${LUATO_LUA_SOURCE_DIR}/ldo.c
    ${LUATO_LUA_SOURCE_DIR}/ldump.c
    ${LUATO_LUA_SOURCE_DIR}/lfunc.c
    ${LUATO_LUA_SOURCE_DIR}/lgc.c
    ${LUATO_LUA_SOURCE_DIR}/llex.c
    ${LUATO_LUA_SOURCE_DIR}/lmem.c
    ${LUATO_LUA_SOURCE_DIR}/lobject.c
    ${LUATO_LUA_SOURCE_DIR}/lopcodes.c
    ${LUATO_LUA_SOURCE_DIR}/lparser.c
    ${LUATO_LUA_SOURCE_DIR}/lstate.c
    ${LUATO_LUA_SOURCE_DIR}/lstring.c
    ${LUATO_LUA_SOURCE_DIR}/ltable.c
    ${LUATO_LUA_SOURCE_DIR}/ltm.c
    ${LUATO_LUA_SOURCE_DIR}/lundump.c
    ${LUATO_LUA_SOURCE_DIR}/lvm.c
    ${LUATO_LUA_SOURCE_DIR}/lzio.c
    ${LUATO_LUA_SOURCE_DIR}/lauxlib.c
    ${LUATO_LUA_SOURCE_DIR}/lbaselib.c
    ${LUATO_LUA_SOURCE_DIR}/lcorolib.c
    ${LUATO_LUA_SOURCE_DIR}/ldblib.c
    ${LUATO_LUA_SOURCE_DIR}/liolib.c
    ${LUATO_LUA_SOURCE_DIR}/lmathlib.c
    ${LUATO_LUA_SOURCE_DIR}/loadlib.c
    ${LUATO_LUA_SOURCE_DIR}/loslib.c
    ${LUATO_LUA_SOURCE_DIR}/lstrlib.c
    ${LUATO_LUA_SOURCE_DIR}/ltablib.c
    ${LUATO_LUA_SOURCE_DIR}/lutf8lib.c
    ${LUATO_LUA_SOURCE_DIR}/linit.c)

add_library(luato.lua-runtime STATIC ${LUATO_LUA_SOURCES})
target_compile_features(luato.lua-runtime PRIVATE c_std_99)
target_compile_definitions(
  luato.lua-runtime
  PRIVATE "$<$<PLATFORM_ID:Linux>:LUA_USE_LINUX>")
target_include_directories(luato.lua-runtime PRIVATE ${LUATO_LUA_SOURCE_DIR})
target_link_libraries(luato.lua-runtime PUBLIC ${CMAKE_DL_LIBS})
if(NOT WIN32)
  target_link_libraries(luato.lua-runtime PUBLIC m)
endif()
set_target_properties(luato.lua-runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(luato.lua INTERFACE)
add_library(Lua::Lua ALIAS luato.lua)
target_include_directories(luato.lua INTERFACE ${LUATO_LUA_SOURCE_DIR})
target_link_libraries(luato.lua INTERFACE luato.lua-runtime)
