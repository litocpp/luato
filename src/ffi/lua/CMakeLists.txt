add_library(luato.ffi.lua STATIC)
add_library(luato::ffi::lua ALIAS luato.ffi.lua)

target_compile_features(luato.ffi.lua PUBLIC cxx_std_20)
if(NOT luato_IS_TOP_LEVEL)
  target_compile_options(
    luato.ffi.lua
    PRIVATE -Wall
            -Wpedantic
            -Wno-gnu-statement-expression
            -Wno-deprecated-declarations
            -Wunknown-attributes)
endif()
target_compile_definitions(luato.ffi.lua PRIVATE LUA_USE_LINUX)
target_include_directories(luato.ffi.lua PRIVATE ${lua_SOURCE_DIR}/src)
target_sources(
  luato.ffi.lua
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         mod.cppm
         types.cppm
         constants.cppm
         functions.cppm)
target_link_libraries(luato.ffi.lua PUBLIC luato.lua-runtime)
set_target_properties(
  luato.ffi.lua
  PROPERTIES CXX_EXTENSIONS OFF
             CXX_SCAN_FOR_MODULES ON
             POSITION_INDEPENDENT_CODE ON)
