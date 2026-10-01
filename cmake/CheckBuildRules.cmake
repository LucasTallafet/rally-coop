# Comprobaciones al configurar. Cualquier fallo para el configure con FATAL_ERROR.
#   1. Ningún target nuestro define JPH_*: llegan solo del target de Jolt (PUBLIC).
#   2. Nada de lo que compila simulación (rally_sim, rally_tests, Jolt) usa /fp:fast ni
#      /fp:contract.
#   3. Ningún target ni carpeta, dependencias incluidas, usa /arch:AVX, AVX2 o AVX512
#      (objetivo SSE4.2).
#   4. rally_sim, rally_loop y rally_tests no enlazan raylib ni SDL, ni directa ni
#      indirectamente.
# Complementa a src/sim/fp_guard.h, que hace lo mismo con las macros del compilador.

set(_our_targets rally_sim rally_loop rally_platform rally rally_tests rally_fp_strict
    rally_warnings)
set(_sim_targets rally_sim rally_tests Jolt)
set(_flag_vars
    CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
    CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE)

function(_rally_collect_dirs dir out)
    set(_dirs "${dir}")
    get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sub IN LISTS _subdirs)
        _rally_collect_dirs("${_sub}" _more)
        list(APPEND _dirs ${_more})
    endforeach()
    set(${out} ${_dirs} PARENT_SCOPE)
endfunction()

# Todo lo que se pasa al compilador en un target: opciones propias, las que hereda y
# los flags de su carpeta.
function(_rally_compile_flags target out)
    set(_flags "")
    foreach(_prop COMPILE_OPTIONS INTERFACE_COMPILE_OPTIONS COMPILE_DEFINITIONS
                  INTERFACE_COMPILE_DEFINITIONS)
        get_target_property(_v ${target} ${_prop})
        if(_v)
            list(APPEND _flags ${_v})
        endif()
    endforeach()
    get_target_property(_type ${target} TYPE)
    if(NOT _type STREQUAL "INTERFACE_LIBRARY")
        get_target_property(_dir ${target} SOURCE_DIR)
        foreach(_var IN LISTS _flag_vars)
            get_directory_property(_v DIRECTORY "${_dir}" DEFINITION ${_var})
            list(APPEND _flags ${_v})
        endforeach()
        get_directory_property(_v DIRECTORY "${_dir}" COMPILE_OPTIONS)
        list(APPEND _flags ${_v})
        get_directory_property(_v DIRECTORY "${_dir}" COMPILE_DEFINITIONS)
        list(APPEND _flags ${_v})
    endif()
    set(${out} "${_flags}" PARENT_SCOPE)
endfunction()

# Cierre transitivo de lo que enlaza un target (nombres tal cual y targets resueltos).
function(_rally_link_closure target out)
    set(_seen "")
    set(_queue ${target})
    while(_queue)
        list(POP_FRONT _queue _t)
        if(_t IN_LIST _seen)
            continue()
        endif()
        list(APPEND _seen ${_t})
        if(NOT TARGET ${_t})
            continue()
        endif()
        get_target_property(_aliased ${_t} ALIASED_TARGET)
        if(_aliased)
            list(APPEND _queue ${_aliased})
            continue()
        endif()
        foreach(_prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(_libs ${_t} ${_prop})
            if(NOT _libs)
                continue()
            endif()
            foreach(_lib IN LISTS _libs)
                string(REGEX REPLACE "^\\$<LINK_ONLY:(.*)>$" "\\1" _lib "${_lib}")
                list(APPEND _queue "${_lib}")
            endforeach()
        endforeach()
    endwhile()
    set(${out} ${_seen} PARENT_SCOPE)
endfunction()

set(_errors "")

# 1. JPH_* solo desde Jolt.
foreach(_t IN LISTS _our_targets)
    _rally_compile_flags(${_t} _flags)
    foreach(_f IN LISTS _flags)
        if(_f MATCHES "JPH_")
            list(APPEND _errors "${_t} define '${_f}': las JPH_* solo pueden venir del target de Jolt")
        endif()
    endforeach()
endforeach()

# 2. Coma flotante en lo que compila simulación.
foreach(_t IN LISTS _sim_targets)
    _rally_compile_flags(${_t} _flags)
    foreach(_f IN LISTS _flags)
        if(_f MATCHES "[/-]fp:(fast|contract)")
            list(APPEND _errors "${_t} compila con '${_f}' (prohibido en simulación)")
        endif()
    endforeach()
endforeach()

# 3. Sin AVX en ningún target ni carpeta.
_rally_collect_dirs("${PROJECT_SOURCE_DIR}" _all_dirs)
foreach(_dir IN LISTS _all_dirs)
    foreach(_var IN LISTS _flag_vars)
        get_directory_property(_v DIRECTORY "${_dir}" DEFINITION ${_var})
        if(_v MATCHES "[/-]arch:AVX")
            list(APPEND _errors "${_var} en ${_dir} contiene /arch:AVX*")
        endif()
    endforeach()
    get_property(_targets DIRECTORY "${_dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        foreach(_prop COMPILE_OPTIONS INTERFACE_COMPILE_OPTIONS)
            get_target_property(_v ${_t} ${_prop})
            if(_v MATCHES "[/-]arch:AVX")
                list(APPEND _errors "${_t} (${_prop}) contiene /arch:AVX*")
            endif()
        endforeach()
    endforeach()
endforeach()

# 4. La simulación y los tests no tocan raylib ni SDL.
foreach(_t rally_sim rally_loop rally_tests)
    _rally_link_closure(${_t} _closure)
    foreach(_lib IN LISTS _closure)
        if(_lib MATCHES "raylib|SDL")
            list(APPEND _errors "${_t} enlaza '${_lib}': la simulación y los tests no pueden usar raylib ni SDL")
        endif()
    endforeach()
endforeach()

if(_errors)
    list(JOIN _errors "\n  " _msg)
    message(FATAL_ERROR "Reglas de compilación incumplidas:\n  ${_msg}")
endif()
message(STATUS "Reglas de compilación: JPH_* solo desde Jolt, /fp:precise sin contracción, sin AVX, simulación sin raylib.")
