if(TARGET Lua::Lua)
  return()
endif()

get_property(luato_enabled_languages GLOBAL PROPERTY ENABLED_LANGUAGES)
if(NOT "C" IN_LIST luato_enabled_languages)
  enable_language(C)
endif()

set(LUATO_LUA_SOURCES
    ${lua_SOURCE_DIR}/src/lapi.c
    ${lua_SOURCE_DIR}/src/lcode.c
    ${lua_SOURCE_DIR}/src/lctype.c
    ${lua_SOURCE_DIR}/src/ldebug.c
    ${lua_SOURCE_DIR}/src/ldo.c
    ${lua_SOURCE_DIR}/src/ldump.c
    ${lua_SOURCE_DIR}/src/lfunc.c
    ${lua_SOURCE_DIR}/src/lgc.c
    ${lua_SOURCE_DIR}/src/llex.c
    ${lua_SOURCE_DIR}/src/lmem.c
    ${lua_SOURCE_DIR}/src/lobject.c
    ${lua_SOURCE_DIR}/src/lopcodes.c
    ${lua_SOURCE_DIR}/src/lparser.c
    ${lua_SOURCE_DIR}/src/lstate.c
    ${lua_SOURCE_DIR}/src/lstring.c
    ${lua_SOURCE_DIR}/src/ltable.c
    ${lua_SOURCE_DIR}/src/ltm.c
    ${lua_SOURCE_DIR}/src/lundump.c
    ${lua_SOURCE_DIR}/src/lvm.c
    ${lua_SOURCE_DIR}/src/lzio.c
    ${lua_SOURCE_DIR}/src/lauxlib.c
    ${lua_SOURCE_DIR}/src/lbaselib.c
    ${lua_SOURCE_DIR}/src/lcorolib.c
    ${lua_SOURCE_DIR}/src/ldblib.c
    ${lua_SOURCE_DIR}/src/liolib.c
    ${lua_SOURCE_DIR}/src/lmathlib.c
    ${lua_SOURCE_DIR}/src/loadlib.c
    ${lua_SOURCE_DIR}/src/loslib.c
    ${lua_SOURCE_DIR}/src/lstrlib.c
    ${lua_SOURCE_DIR}/src/ltablib.c
    ${lua_SOURCE_DIR}/src/lutf8lib.c
    ${lua_SOURCE_DIR}/src/linit.c)

add_library(luato.lua-runtime STATIC ${LUATO_LUA_SOURCES})
target_compile_features(luato.lua-runtime PRIVATE c_std_99)
target_compile_definitions(luato.lua-runtime PRIVATE LUA_USE_LINUX)
target_include_directories(luato.lua-runtime PRIVATE ${lua_SOURCE_DIR}/src)
target_link_libraries(luato.lua-runtime PUBLIC ${CMAKE_DL_LIBS} m)
set_target_properties(luato.lua-runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(luato.lua INTERFACE)
add_library(Lua::Lua ALIAS luato.lua)
add_dependencies(luato.lua luato.lua-runtime)
target_include_directories(luato.lua INTERFACE ${lua_SOURCE_DIR}/src)
target_link_directories(luato.lua INTERFACE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(luato.lua INTERFACE -lluato.lua-runtime ${CMAKE_DL_LIBS} m)

set(Lua_VERSION "5.5.0")
