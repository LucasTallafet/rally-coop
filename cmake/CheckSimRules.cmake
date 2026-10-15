# Comprueba las reglas de reproducibilidad en el código de simulación (cmake -P, sin Python).
#
#   cmake -DROOT=<raíz del repo> [-DEXPECTED=<n>] -P cmake/CheckSimRules.cmake
#
# Revisa src/sim, src/gen, src/net y src/game (las que existan):
#   - matemática de std:: o de C directa (todo debe pasar por rmath), y <cmath>/<math.h>;
#   - <random>, rand()/srand();
#   - unordered_map/unordered_set (orden de iteración no determinista);
#   - reloj (<chrono>, <ctime>, time(), clock(), contadores de Windows): solo en src/sim, src/gen y src/game.
# src/sim/rmath.h queda excluido: es el único sitio que envuelve std::.
# Una línea se acepta si lleva "// rmath-allow: <motivo>"; sin motivo cuenta como infracción.
# Se ignoran los comentarios; las cadenas no se analizan (un falso positivo ahí se justifica
# con rmath-allow).
# Sin EXPECTED, falla si hay alguna infracción; con EXPECTED, si el número no coincide
# exactamente (así se prueba el propio script con árboles de ejemplo).

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "Falta -DROOT=<raíz del repositorio>.")
endif()
file(REAL_PATH "${ROOT}" ROOT)
if(NOT DEFINED EXPECTED)
    set(EXPECTED 0)
endif()

# Separadores de palabra: lo que puede ir antes de un nombre que no sea parte de otro
# identificador ni un acceso a miembro (., ->) o a un ámbito (::).
set(_free "(^|[^A-Za-z0-9_.>:])")

set(_math_fns "sin|cos|tan|asin|acos|atan|atan2|sinh|cosh|tanh|asinh|acosh|atanh|sqrt|cbrt|hypot|pow|exp|exp2|expm1|log|log2|log10|log1p|fma|fmod|remainder|floor|ceil|round|trunc|lround|llround|rint|nearbyint|abs|fabs|copysign")
set(_c_math_fns "sinf?|cosf?|tanf?|asinf?|acosf?|atanf?|atan2f?|sqrtf?|powf?|expf?|logf?|log10f?|fabsf?|floorf?|ceilf?|fmodf?|hypotf?")

# Reglas para todas las carpetas: pares "patrón" "mensaje".
set(_rules_all
    "std::(${_math_fns})[ \t]*\\("
    "matemática de std:: directa: usa rmath::"
    "${_free}(${_c_math_fns})[ \t]*\\("
    "matemática de C sin calificar: usa rmath::"
    "#[ \t]*include[ \t]*<(cmath|math\\.h)>"
    "incluye <cmath>/<math.h>: usa sim/rmath.h"
    "#[ \t]*include[ \t]*<random>"
    "incluye <random>: usa el PCG propio (sim/pcg32.h)"
    "${_free}(std::)?s?rand[ \t]*\\("
    "rand() o srand(): usa el PCG propio (sim/pcg32.h)"
    "(^|[^A-Za-z0-9_])unordered_(multi)?(map|set)"
    "unordered_map/unordered_set: el orden de iteración no es determinista"
)
# Reglas de reloj: solo src/sim, src/gen y src/game (la red sí puede medir tiempo).
set(_rules_clock
    "#[ \t]*include[ \t]*<(chrono|ctime|time\\.h)>"
    "incluye una cabecera de reloj: la simulación avanza solo por pasos"
    "std::chrono"
    "usa std::chrono: la simulación avanza solo por pasos"
    "${_free}(std::)?(time|clock)[ \t]*\\("
    "lee el reloj (time/clock): la simulación avanza solo por pasos"
    "(QueryPerformanceCounter|GetTickCount|timeGetTime|GetSystemTime)"
    "lee un reloj de Windows: la simulación avanza solo por pasos"
)

# Los pares se guardan en listas de CMake: ni patrones ni mensajes pueden llevar ';'.
foreach(_list _rules_all _rules_clock)
    list(LENGTH ${_list} _len)
    math(EXPR _odd "${_len} % 2")
    if(_odd)
        message(FATAL_ERROR "${_list} no tiene pares patrón/mensaje (¿un ';' en un mensaje?).")
    endif()
endforeach()

set(_violations 0)

function(_check_file file rel use_clock)
    set(_rules ${_rules_all})
    if(use_clock)
        list(APPEND _rules ${_rules_clock})
    endif()
    list(LENGTH _rules _rules_len)
    math(EXPR _last "${_rules_len} - 1")

    file(READ "${file}" _content)
    # ; y [ ] rompen las listas de CMake: se sustituyen antes de partir en líneas.
    string(REPLACE ";" "<SEMI>" _content "${_content}")
    string(REPLACE "[" "<LB>" _content "${_content}")
    string(REPLACE "]" "<RB>" _content "${_content}")
    string(REPLACE "\r" "" _content "${_content}")
    string(REPLACE "\n" ";" _lines "${_content}")

    set(_count 0)
    set(_lineno 0)
    set(_in_block FALSE)
    foreach(_line IN LISTS _lines)
        math(EXPR _lineno "${_lineno} + 1")

        # Excepción explícita.
        if(_line MATCHES "//[ \t]*rmath-allow:(.*)$")
            string(STRIP "${CMAKE_MATCH_1}" _reason)
            if(_reason STREQUAL "")
                message("${rel}:${_lineno}: rmath-allow sin motivo")
                math(EXPR _count "${_count} + 1")
            endif()
            continue()
        endif()

        # Quitar comentarios.
        set(_code "${_line}")
        if(_in_block)
            string(FIND "${_code}" "*/" _end)
            if(_end EQUAL -1)
                continue()
            endif()
            math(EXPR _end "${_end} + 2")
            string(SUBSTRING "${_code}" ${_end} -1 _code)
            set(_in_block FALSE)
        endif()
        string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" " " _code "${_code}")
        string(FIND "${_code}" "/*" _start)
        if(NOT _start EQUAL -1)
            string(SUBSTRING "${_code}" 0 ${_start} _code)
            set(_in_block TRUE)
        endif()
        string(FIND "${_code}" "//" _comment)
        if(NOT _comment EQUAL -1)
            string(SUBSTRING "${_code}" 0 ${_comment} _code)
        endif()

        foreach(_i RANGE 0 ${_last} 2)
            math(EXPR _j "${_i} + 1")
            list(GET _rules ${_i} _pattern)
            list(GET _rules ${_j} _message)
            if(_code MATCHES "${_pattern}")
                message("${rel}:${_lineno}: ${_message}")
                math(EXPR _count "${_count} + 1")
            endif()
        endforeach()
    endforeach()

    set(_file_violations ${_count} PARENT_SCOPE)
endfunction()

foreach(_dir sim gen net game)
    set(_abs "${ROOT}/src/${_dir}")
    if(NOT IS_DIRECTORY "${_abs}")
        continue()
    endif()
    set(_use_clock FALSE)
    if(_dir STREQUAL "sim" OR _dir STREQUAL "gen" OR _dir STREQUAL "game")
        set(_use_clock TRUE)
    endif()
    file(GLOB_RECURSE _files LIST_DIRECTORIES FALSE
        "${_abs}/*.h" "${_abs}/*.hpp" "${_abs}/*.cpp" "${_abs}/*.inl")
    list(SORT _files)
    foreach(_file IN LISTS _files)
        file(RELATIVE_PATH _rel "${ROOT}" "${_file}")
        if(_rel STREQUAL "src/sim/rmath.h")
            continue()
        endif()
        _check_file("${_file}" "${_rel}" ${_use_clock})
        math(EXPR _violations "${_violations} + ${_file_violations}")
    endforeach()
endforeach()

if(NOT _violations EQUAL EXPECTED)
    message(FATAL_ERROR
        "Reglas de simulación: ${_violations} infracciones (se esperaban ${EXPECTED}).")
endif()
message(STATUS "Reglas de simulación: ${_violations} infracciones (esperadas ${EXPECTED}).")
