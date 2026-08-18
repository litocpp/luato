add_subdirectory(ffi/lua)

add_library(luato.lib STATIC)
add_library(luato::luato ALIAS luato.lib)
target_compile_features(luato.lib PUBLIC cxx_std_20)
if(NOT luato_IS_TOP_LEVEL)
  target_compile_options(
    luato.lib
    PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Wall>"
            "$<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>"
            "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>"
            "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>"
            "$<$<COMPILE_LANGUAGE:CXX>:-Wno-gnu-statement-expression>"
            "$<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-declarations>"
            "$<$<COMPILE_LANGUAGE:CXX>:-Wunknown-attributes>"
            "$<$<COMPILE_LANGUAGE:CXX>:-fsized-deallocation>")
endif()
target_link_libraries(luato.lib PUBLIC rstd::rstd luato::ffi::lua)
target_sources(
  luato.lib
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         error.cppm
         binding.cppm
         state.cppm
         lib.cppm)
