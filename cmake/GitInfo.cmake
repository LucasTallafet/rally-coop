# Escribe OUTPUT (build_info.cpp) con el commit de SOURCE_DIR y la marca "-dirty" si hay
# cambios sin commit. Se ejecuta en cada build (target rally_build_info en src/io) y solo
# reescribe el archivo si cambia, para no recompilar sin motivo.
#   cmake -DSOURCE_DIR=<repo> -DOUTPUT=<archivo> -P GitInfo.cmake

set(_commit "desconocido")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" rev-parse --short=12 HEAD
        OUTPUT_VARIABLE _hash OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _result ERROR_QUIET)
    if(_result EQUAL 0 AND _hash)
        set(_commit "${_hash}")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" status --porcelain
            OUTPUT_VARIABLE _status OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _result ERROR_QUIET)
        if(NOT _result EQUAL 0 OR _status)
            string(APPEND _commit "-dirty")
        endif()
    endif()
endif()

set(_content "// Generado por cmake/GitInfo.cmake en cada build. No editar.
#include \"io/build_info.h\"

namespace rally::io {

const char* build_commit() { return \"${_commit}\"; }

} // namespace rally::io
")

set(_old "")
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" _old)
endif()
if(NOT _old STREQUAL _content)
    file(WRITE "${OUTPUT}" "${_content}")
endif()
