add_executable(luato.contract main.cpp)
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
  luato.contract PRIVATE LUATO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(luato.contract PRIVATE luato::luato)

add_test(NAME luato.contract COMMAND luato.contract)
