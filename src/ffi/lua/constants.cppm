module;

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace luato_lua_ffi
{
inline constexpr auto version_major_n        = LUA_VERSION_MAJOR_N;
inline constexpr auto version_minor_n        = LUA_VERSION_MINOR_N;
inline constexpr auto version_release_n      = LUA_VERSION_RELEASE_N;
inline constexpr auto version_num            = LUA_VERSION_NUM;
inline constexpr auto version_release_num    = LUA_VERSION_RELEASE_NUM;
inline constexpr auto version_major          = LUA_VERSION_MAJOR;
inline constexpr auto version_minor          = LUA_VERSION_MINOR;
inline constexpr auto version_release        = LUA_VERSION_RELEASE;
inline constexpr auto version                = LUA_VERSION;
inline constexpr auto release                = LUA_RELEASE;
inline constexpr auto copyright              = LUA_COPYRIGHT;
inline constexpr auto authors                = LUA_AUTHORS;
inline constexpr auto signature              = LUA_SIGNATURE;
inline constexpr auto multret                = LUA_MULTRET;
inline constexpr auto registry_index         = LUA_REGISTRYINDEX;
inline constexpr auto ok                     = LUA_OK;
inline constexpr auto yield                  = LUA_YIELD;
inline constexpr auto error_run              = LUA_ERRRUN;
inline constexpr auto error_syntax           = LUA_ERRSYNTAX;
inline constexpr auto error_memory           = LUA_ERRMEM;
inline constexpr auto error_handler          = LUA_ERRERR;
inline constexpr auto type_none              = LUA_TNONE;
inline constexpr auto type_nil               = LUA_TNIL;
inline constexpr auto type_boolean           = LUA_TBOOLEAN;
inline constexpr auto type_light_userdata    = LUA_TLIGHTUSERDATA;
inline constexpr auto type_number            = LUA_TNUMBER;
inline constexpr auto type_string            = LUA_TSTRING;
inline constexpr auto type_table             = LUA_TTABLE;
inline constexpr auto type_function          = LUA_TFUNCTION;
inline constexpr auto type_userdata          = LUA_TUSERDATA;
inline constexpr auto type_thread            = LUA_TTHREAD;
inline constexpr auto number_of_types        = LUA_NUMTYPES;
inline constexpr auto minimum_stack          = LUA_MINSTACK;
inline constexpr auto registry_globals       = LUA_RIDX_GLOBALS;
inline constexpr auto registry_main_thread   = LUA_RIDX_MAINTHREAD;
inline constexpr auto registry_last          = LUA_RIDX_LAST;
inline constexpr auto operation_add          = LUA_OPADD;
inline constexpr auto operation_subtract     = LUA_OPSUB;
inline constexpr auto operation_multiply     = LUA_OPMUL;
inline constexpr auto operation_modulo       = LUA_OPMOD;
inline constexpr auto operation_power        = LUA_OPPOW;
inline constexpr auto operation_divide       = LUA_OPDIV;
inline constexpr auto operation_floor_divide = LUA_OPIDIV;
inline constexpr auto operation_bit_and      = LUA_OPBAND;
inline constexpr auto operation_bit_or       = LUA_OPBOR;
inline constexpr auto operation_bit_xor      = LUA_OPBXOR;
inline constexpr auto operation_shift_left   = LUA_OPSHL;
inline constexpr auto operation_shift_right  = LUA_OPSHR;
inline constexpr auto operation_unary_minus  = LUA_OPUNM;
inline constexpr auto operation_bit_not      = LUA_OPBNOT;
inline constexpr auto compare_equal          = LUA_OPEQ;
inline constexpr auto compare_less           = LUA_OPLT;
inline constexpr auto compare_less_equal     = LUA_OPLE;
inline constexpr auto gc_stop                = LUA_GCSTOP;
inline constexpr auto gc_restart             = LUA_GCRESTART;
inline constexpr auto gc_collect             = LUA_GCCOLLECT;
inline constexpr auto gc_count               = LUA_GCCOUNT;
inline constexpr auto gc_count_bytes         = LUA_GCCOUNTB;
inline constexpr auto gc_step                = LUA_GCSTEP;
inline constexpr auto gc_is_running          = LUA_GCISRUNNING;
inline constexpr auto gc_generational        = LUA_GCGEN;
inline constexpr auto gc_incremental         = LUA_GCINC;
inline constexpr auto gc_parameter           = LUA_GCPARAM;
inline constexpr auto gc_minor_multiplier    = LUA_GCPMINORMUL;
inline constexpr auto gc_major_minor         = LUA_GCPMAJORMINOR;
inline constexpr auto gc_minor_major         = LUA_GCPMINORMAJOR;
inline constexpr auto gc_pause               = LUA_GCPPAUSE;
inline constexpr auto gc_step_multiplier     = LUA_GCPSTEPMUL;
inline constexpr auto gc_step_size           = LUA_GCPSTEPSIZE;
inline constexpr auto gc_parameter_count     = LUA_GCPN;
inline constexpr auto number_to_string_size  = LUA_N2SBUFFSZ;
inline constexpr auto hook_call              = LUA_HOOKCALL;
inline constexpr auto hook_return            = LUA_HOOKRET;
inline constexpr auto hook_line              = LUA_HOOKLINE;
inline constexpr auto hook_count             = LUA_HOOKCOUNT;
inline constexpr auto hook_tail_call         = LUA_HOOKTAILCALL;
inline constexpr auto mask_call              = LUA_MASKCALL;
inline constexpr auto mask_return            = LUA_MASKRET;
inline constexpr auto mask_line              = LUA_MASKLINE;
inline constexpr auto mask_count             = LUA_MASKCOUNT;
inline constexpr auto error_file             = LUA_ERRFILE;
inline constexpr auto no_reference           = LUA_NOREF;
inline constexpr auto nil_reference          = LUA_REFNIL;
inline constexpr auto global_name            = LUA_GNAME;
inline constexpr auto loaded_table           = LUA_LOADED_TABLE;
inline constexpr auto preload_table          = LUA_PRELOAD_TABLE;
inline constexpr auto file_handle            = LUA_FILEHANDLE;
inline constexpr auto version_suffix         = LUA_VERSUFFIX;
inline constexpr auto global_library         = LUA_GLIBK;
inline constexpr auto load_library_name      = LUA_LOADLIBNAME;
inline constexpr auto load_library           = LUA_LOADLIBK;
inline constexpr auto coroutine_library_name = LUA_COLIBNAME;
inline constexpr auto coroutine_library      = LUA_COLIBK;
inline constexpr auto debug_library_name     = LUA_DBLIBNAME;
inline constexpr auto debug_library          = LUA_DBLIBK;
inline constexpr auto io_library_name        = LUA_IOLIBNAME;
inline constexpr auto io_library             = LUA_IOLIBK;
inline constexpr auto math_library_name      = LUA_MATHLIBNAME;
inline constexpr auto math_library           = LUA_MATHLIBK;
inline constexpr auto os_library_name        = LUA_OSLIBNAME;
inline constexpr auto os_library             = LUA_OSLIBK;
inline constexpr auto string_library_name    = LUA_STRLIBNAME;
inline constexpr auto string_library         = LUA_STRLIBK;
inline constexpr auto table_library_name     = LUA_TABLIBNAME;
inline constexpr auto table_library          = LUA_TABLIBK;
inline constexpr auto utf8_library_name      = LUA_UTF8LIBNAME;
inline constexpr auto utf8_library           = LUA_UTF8LIBK;
inline constexpr auto auxiliary_number_sizes = LUAL_NUMSIZES;
inline constexpr auto extra_space            = LUA_EXTRASPACE;
inline constexpr auto debug_source_size      = LUA_IDSIZE;
inline constexpr auto buffer_size            = LUAL_BUFFERSIZE;
inline constexpr auto maximum_integer        = LUA_MAXINTEGER;
inline constexpr auto minimum_integer        = LUA_MININTEGER;
inline constexpr auto maximum_unsigned       = LUA_MAXUNSIGNED;
} // namespace luato_lua_ffi

#undef LUA_VERSION_MAJOR_N
#undef LUA_VERSION_MINOR_N
#undef LUA_VERSION_RELEASE_N
#undef LUA_VERSION_NUM
#undef LUA_VERSION_RELEASE_NUM
#undef LUA_VERSION_MAJOR
#undef LUA_VERSION_MINOR
#undef LUA_VERSION_RELEASE
#undef LUA_VERSION
#undef LUA_RELEASE
#undef LUA_COPYRIGHT
#undef LUA_AUTHORS
#undef LUA_SIGNATURE
#undef LUA_MULTRET
#undef LUA_REGISTRYINDEX
#undef LUA_OK
#undef LUA_YIELD
#undef LUA_ERRRUN
#undef LUA_ERRSYNTAX
#undef LUA_ERRMEM
#undef LUA_ERRERR
#undef LUA_TNONE
#undef LUA_TNIL
#undef LUA_TBOOLEAN
#undef LUA_TLIGHTUSERDATA
#undef LUA_TNUMBER
#undef LUA_TSTRING
#undef LUA_TTABLE
#undef LUA_TFUNCTION
#undef LUA_TUSERDATA
#undef LUA_TTHREAD
#undef LUA_NUMTYPES
#undef LUA_MINSTACK
#undef LUA_RIDX_GLOBALS
#undef LUA_RIDX_MAINTHREAD
#undef LUA_RIDX_LAST
#undef LUA_OPADD
#undef LUA_OPSUB
#undef LUA_OPMUL
#undef LUA_OPMOD
#undef LUA_OPPOW
#undef LUA_OPDIV
#undef LUA_OPIDIV
#undef LUA_OPBAND
#undef LUA_OPBOR
#undef LUA_OPBXOR
#undef LUA_OPSHL
#undef LUA_OPSHR
#undef LUA_OPUNM
#undef LUA_OPBNOT
#undef LUA_OPEQ
#undef LUA_OPLT
#undef LUA_OPLE
#undef LUA_GCSTOP
#undef LUA_GCRESTART
#undef LUA_GCCOLLECT
#undef LUA_GCCOUNT
#undef LUA_GCCOUNTB
#undef LUA_GCSTEP
#undef LUA_GCISRUNNING
#undef LUA_GCGEN
#undef LUA_GCINC
#undef LUA_GCPARAM
#undef LUA_GCPMINORMUL
#undef LUA_GCPMAJORMINOR
#undef LUA_GCPMINORMAJOR
#undef LUA_GCPPAUSE
#undef LUA_GCPSTEPMUL
#undef LUA_GCPSTEPSIZE
#undef LUA_GCPN
#undef LUA_N2SBUFFSZ
#undef LUA_HOOKCALL
#undef LUA_HOOKRET
#undef LUA_HOOKLINE
#undef LUA_HOOKCOUNT
#undef LUA_HOOKTAILCALL
#undef LUA_MASKCALL
#undef LUA_MASKRET
#undef LUA_MASKLINE
#undef LUA_MASKCOUNT
#undef LUA_ERRFILE
#undef LUA_NOREF
#undef LUA_REFNIL
#undef LUA_GNAME
#undef LUA_LOADED_TABLE
#undef LUA_PRELOAD_TABLE
#undef LUA_FILEHANDLE
#undef LUA_VERSUFFIX
#undef LUA_GLIBK
#undef LUA_LOADLIBNAME
#undef LUA_LOADLIBK
#undef LUA_COLIBNAME
#undef LUA_COLIBK
#undef LUA_DBLIBNAME
#undef LUA_DBLIBK
#undef LUA_IOLIBNAME
#undef LUA_IOLIBK
#undef LUA_MATHLIBNAME
#undef LUA_MATHLIBK
#undef LUA_OSLIBNAME
#undef LUA_OSLIBK
#undef LUA_STRLIBNAME
#undef LUA_STRLIBK
#undef LUA_TABLIBNAME
#undef LUA_TABLIBK
#undef LUA_UTF8LIBNAME
#undef LUA_UTF8LIBK
#undef LUAL_NUMSIZES
#undef LUA_EXTRASPACE
#undef LUA_IDSIZE
#undef LUAL_BUFFERSIZE
#undef LUA_MAXINTEGER
#undef LUA_MININTEGER
#undef LUA_MAXUNSIGNED

export module lua:constants;

export {
    inline constexpr auto LUA_VERSION_MAJOR_N     = luato_lua_ffi::version_major_n;
    inline constexpr auto LUA_VERSION_MINOR_N     = luato_lua_ffi::version_minor_n;
    inline constexpr auto LUA_VERSION_RELEASE_N   = luato_lua_ffi::version_release_n;
    inline constexpr auto LUA_VERSION_NUM         = luato_lua_ffi::version_num;
    inline constexpr auto LUA_VERSION_RELEASE_NUM = luato_lua_ffi::version_release_num;
    inline constexpr auto LUA_VERSION_MAJOR       = luato_lua_ffi::version_major;
    inline constexpr auto LUA_VERSION_MINOR       = luato_lua_ffi::version_minor;
    inline constexpr auto LUA_VERSION_RELEASE     = luato_lua_ffi::version_release;
    inline constexpr auto LUA_VERSION             = luato_lua_ffi::version;
    inline constexpr auto LUA_RELEASE             = luato_lua_ffi::release;
    inline constexpr auto LUA_COPYRIGHT           = luato_lua_ffi::copyright;
    inline constexpr auto LUA_AUTHORS             = luato_lua_ffi::authors;
    inline constexpr auto LUA_SIGNATURE           = luato_lua_ffi::signature;
    inline constexpr auto LUA_MULTRET             = luato_lua_ffi::multret;
    inline constexpr auto LUA_REGISTRYINDEX       = luato_lua_ffi::registry_index;
    inline constexpr auto LUA_OK                  = luato_lua_ffi::ok;
    inline constexpr auto LUA_YIELD               = luato_lua_ffi::yield;
    inline constexpr auto LUA_ERRRUN              = luato_lua_ffi::error_run;
    inline constexpr auto LUA_ERRSYNTAX           = luato_lua_ffi::error_syntax;
    inline constexpr auto LUA_ERRMEM              = luato_lua_ffi::error_memory;
    inline constexpr auto LUA_ERRERR              = luato_lua_ffi::error_handler;
    inline constexpr auto LUA_TNONE               = luato_lua_ffi::type_none;
    inline constexpr auto LUA_TNIL                = luato_lua_ffi::type_nil;
    inline constexpr auto LUA_TBOOLEAN            = luato_lua_ffi::type_boolean;
    inline constexpr auto LUA_TLIGHTUSERDATA      = luato_lua_ffi::type_light_userdata;
    inline constexpr auto LUA_TNUMBER             = luato_lua_ffi::type_number;
    inline constexpr auto LUA_TSTRING             = luato_lua_ffi::type_string;
    inline constexpr auto LUA_TTABLE              = luato_lua_ffi::type_table;
    inline constexpr auto LUA_TFUNCTION           = luato_lua_ffi::type_function;
    inline constexpr auto LUA_TUSERDATA           = luato_lua_ffi::type_userdata;
    inline constexpr auto LUA_TTHREAD             = luato_lua_ffi::type_thread;
    inline constexpr auto LUA_NUMTYPES            = luato_lua_ffi::number_of_types;
    inline constexpr auto LUA_MINSTACK            = luato_lua_ffi::minimum_stack;
    inline constexpr auto LUA_RIDX_GLOBALS        = luato_lua_ffi::registry_globals;
    inline constexpr auto LUA_RIDX_MAINTHREAD     = luato_lua_ffi::registry_main_thread;
    inline constexpr auto LUA_RIDX_LAST           = luato_lua_ffi::registry_last;
    inline constexpr auto LUA_OPADD               = luato_lua_ffi::operation_add;
    inline constexpr auto LUA_OPSUB               = luato_lua_ffi::operation_subtract;
    inline constexpr auto LUA_OPMUL               = luato_lua_ffi::operation_multiply;
    inline constexpr auto LUA_OPMOD               = luato_lua_ffi::operation_modulo;
    inline constexpr auto LUA_OPPOW               = luato_lua_ffi::operation_power;
    inline constexpr auto LUA_OPDIV               = luato_lua_ffi::operation_divide;
    inline constexpr auto LUA_OPIDIV              = luato_lua_ffi::operation_floor_divide;
    inline constexpr auto LUA_OPBAND              = luato_lua_ffi::operation_bit_and;
    inline constexpr auto LUA_OPBOR               = luato_lua_ffi::operation_bit_or;
    inline constexpr auto LUA_OPBXOR              = luato_lua_ffi::operation_bit_xor;
    inline constexpr auto LUA_OPSHL               = luato_lua_ffi::operation_shift_left;
    inline constexpr auto LUA_OPSHR               = luato_lua_ffi::operation_shift_right;
    inline constexpr auto LUA_OPUNM               = luato_lua_ffi::operation_unary_minus;
    inline constexpr auto LUA_OPBNOT              = luato_lua_ffi::operation_bit_not;
    inline constexpr auto LUA_OPEQ                = luato_lua_ffi::compare_equal;
    inline constexpr auto LUA_OPLT                = luato_lua_ffi::compare_less;
    inline constexpr auto LUA_OPLE                = luato_lua_ffi::compare_less_equal;
    inline constexpr auto LUA_GCSTOP              = luato_lua_ffi::gc_stop;
    inline constexpr auto LUA_GCRESTART           = luato_lua_ffi::gc_restart;
    inline constexpr auto LUA_GCCOLLECT           = luato_lua_ffi::gc_collect;
    inline constexpr auto LUA_GCCOUNT             = luato_lua_ffi::gc_count;
    inline constexpr auto LUA_GCCOUNTB            = luato_lua_ffi::gc_count_bytes;
    inline constexpr auto LUA_GCSTEP              = luato_lua_ffi::gc_step;
    inline constexpr auto LUA_GCISRUNNING         = luato_lua_ffi::gc_is_running;
    inline constexpr auto LUA_GCGEN               = luato_lua_ffi::gc_generational;
    inline constexpr auto LUA_GCINC               = luato_lua_ffi::gc_incremental;
    inline constexpr auto LUA_GCPARAM             = luato_lua_ffi::gc_parameter;
    inline constexpr auto LUA_GCPMINORMUL         = luato_lua_ffi::gc_minor_multiplier;
    inline constexpr auto LUA_GCPMAJORMINOR       = luato_lua_ffi::gc_major_minor;
    inline constexpr auto LUA_GCPMINORMAJOR       = luato_lua_ffi::gc_minor_major;
    inline constexpr auto LUA_GCPPAUSE            = luato_lua_ffi::gc_pause;
    inline constexpr auto LUA_GCPSTEPMUL          = luato_lua_ffi::gc_step_multiplier;
    inline constexpr auto LUA_GCPSTEPSIZE         = luato_lua_ffi::gc_step_size;
    inline constexpr auto LUA_GCPN                = luato_lua_ffi::gc_parameter_count;
    inline constexpr auto LUA_N2SBUFFSZ           = luato_lua_ffi::number_to_string_size;
    inline constexpr auto LUA_HOOKCALL            = luato_lua_ffi::hook_call;
    inline constexpr auto LUA_HOOKRET             = luato_lua_ffi::hook_return;
    inline constexpr auto LUA_HOOKLINE            = luato_lua_ffi::hook_line;
    inline constexpr auto LUA_HOOKCOUNT           = luato_lua_ffi::hook_count;
    inline constexpr auto LUA_HOOKTAILCALL        = luato_lua_ffi::hook_tail_call;
    inline constexpr auto LUA_MASKCALL            = luato_lua_ffi::mask_call;
    inline constexpr auto LUA_MASKRET             = luato_lua_ffi::mask_return;
    inline constexpr auto LUA_MASKLINE            = luato_lua_ffi::mask_line;
    inline constexpr auto LUA_MASKCOUNT           = luato_lua_ffi::mask_count;
    inline constexpr auto LUA_ERRFILE             = luato_lua_ffi::error_file;
    inline constexpr auto LUA_NOREF               = luato_lua_ffi::no_reference;
    inline constexpr auto LUA_REFNIL              = luato_lua_ffi::nil_reference;
    inline constexpr auto LUA_GNAME               = luato_lua_ffi::global_name;
    inline constexpr auto LUA_LOADED_TABLE        = luato_lua_ffi::loaded_table;
    inline constexpr auto LUA_PRELOAD_TABLE       = luato_lua_ffi::preload_table;
    inline constexpr auto LUA_FILEHANDLE          = luato_lua_ffi::file_handle;
    inline constexpr auto LUA_VERSUFFIX           = luato_lua_ffi::version_suffix;
    inline constexpr auto LUA_GLIBK               = luato_lua_ffi::global_library;
    inline constexpr auto LUA_LOADLIBNAME         = luato_lua_ffi::load_library_name;
    inline constexpr auto LUA_LOADLIBK            = luato_lua_ffi::load_library;
    inline constexpr auto LUA_COLIBNAME           = luato_lua_ffi::coroutine_library_name;
    inline constexpr auto LUA_COLIBK              = luato_lua_ffi::coroutine_library;
    inline constexpr auto LUA_DBLIBNAME           = luato_lua_ffi::debug_library_name;
    inline constexpr auto LUA_DBLIBK              = luato_lua_ffi::debug_library;
    inline constexpr auto LUA_IOLIBNAME           = luato_lua_ffi::io_library_name;
    inline constexpr auto LUA_IOLIBK              = luato_lua_ffi::io_library;
    inline constexpr auto LUA_MATHLIBNAME         = luato_lua_ffi::math_library_name;
    inline constexpr auto LUA_MATHLIBK            = luato_lua_ffi::math_library;
    inline constexpr auto LUA_OSLIBNAME           = luato_lua_ffi::os_library_name;
    inline constexpr auto LUA_OSLIBK              = luato_lua_ffi::os_library;
    inline constexpr auto LUA_STRLIBNAME          = luato_lua_ffi::string_library_name;
    inline constexpr auto LUA_STRLIBK             = luato_lua_ffi::string_library;
    inline constexpr auto LUA_TABLIBNAME          = luato_lua_ffi::table_library_name;
    inline constexpr auto LUA_TABLIBK             = luato_lua_ffi::table_library;
    inline constexpr auto LUA_UTF8LIBNAME         = luato_lua_ffi::utf8_library_name;
    inline constexpr auto LUA_UTF8LIBK            = luato_lua_ffi::utf8_library;
    inline constexpr auto LUAL_NUMSIZES           = luato_lua_ffi::auxiliary_number_sizes;
    inline constexpr auto LUA_EXTRASPACE          = luato_lua_ffi::extra_space;
    inline constexpr auto LUA_IDSIZE              = luato_lua_ffi::debug_source_size;
    inline constexpr auto LUAL_BUFFERSIZE         = luato_lua_ffi::buffer_size;
    inline constexpr auto LUA_MAXINTEGER          = luato_lua_ffi::maximum_integer;
    inline constexpr auto LUA_MININTEGER          = luato_lua_ffi::minimum_integer;
    inline constexpr auto LUA_MAXUNSIGNED         = luato_lua_ffi::maximum_unsigned;
}
