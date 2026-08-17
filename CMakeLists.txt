cmake_minimum_required(VERSION 3.28)

project(
  luato
  VERSION 0.1.0
  DESCRIPTION "Typed Lua bindings for C++ modules"
  LANGUAGES C CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "luato currently supports Clang only")
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT WIN32)
  message(FATAL_ERROR "luato currently supports Linux and Windows only")
endif()

if(PROJECT_IS_TOP_LEVEL)
  include(GNUInstallDirs)

  set(CMAKE_C_STANDARD 99)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_C_EXTENSIONS ON)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)

  add_compile_options(
    "$<$<COMPILE_LANGUAGE:CXX>:-Wall>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wno-gnu-statement-expression>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-declarations>"
    "$<$<COMPILE_LANGUAGE:CXX>:-Wunknown-attributes>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fsized-deallocation>")

  install(FILES LICENSE-MIT LICENSE-APACHE
          DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/luato)
endif()

option(LUATO_BUILD_TESTS "Build standalone Luato contract tests" ${PROJECT_IS_TOP_LEVEL})

include(FetchContent)

if(NOT TARGET Lua::Lua)
  find_package(Lua 5.5.0)
  if(Lua_FOUND)
    if(NOT TARGET Lua::Lua)
      add_library(Lua::Lua INTERFACE IMPORTED)
      set_target_properties(
        Lua::Lua
        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${LUA_INCLUDE_DIR}"
                   INTERFACE_LINK_LIBRARIES "${LUA_LIBRARIES}")
    endif()
  else()
    FetchContent_Declare(
      lua
      URL https://www.lua.org/ftp/lua-5.5.1.tar.gz
      URL_HASH SHA256=1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(lua)
  endif()
endif()

if(NOT TARGET rstd::rstd)
  set(RSTD_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    rstd
    GIT_REPOSITORY https://github.com/litocpp/rstd.git
    GIT_TAG b7b252b5fad3599974dd2c1899616205920c5000)
  FetchContent_MakeAvailable(rstd)
endif()

add_subdirectory(src)

if(LUATO_BUILD_TESTS)
  enable_testing()

  add_executable(luato.contract tests/main.cpp)
  target_compile_features(luato.contract PRIVATE cxx_std_20)
  if(NOT luato_IS_TOP_LEVEL)
    target_compile_options(
      luato.contract
      PRIVATE -Wall
              -Wpedantic
              -fno-exceptions
              -fno-rtti
              -Wno-gnu-statement-expression
              -Wno-deprecated-declarations
              -Wunknown-attributes
              -fsized-deallocation)
  endif()
  target_compile_definitions(
    luato.contract
    PRIVATE LUATO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
  target_link_libraries(luato.contract PRIVATE luato::luato)

  add_test(NAME luato.contract COMMAND luato.contract)
endif()
