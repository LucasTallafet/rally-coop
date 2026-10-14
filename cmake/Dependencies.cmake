# Dependencias externas con FetchContent, fijadas por archivo de release y SHA256.
# Para actualizar una: cambiar URL y hash (Get-FileHash -Algorithm SHA256) y anotar
# el tag aquí. SYSTEM hace que sus cabeceras no generen avisos en nuestro código.

include(FetchContent)

# --- Jolt Physics v5.6.0 ------------------------------------------------------
set(DOUBLE_PRECISION ON)
set(CROSS_PLATFORM_DETERMINISTIC ON)
# Objetivo SSE4.2: nada de AVX, LZCNT, TZCNT, F16C ni FMA (requisitos mínimos).
set(USE_SSE4_1 ON)
set(USE_SSE4_2 ON)
set(USE_AVX OFF)
set(USE_AVX2 OFF)
set(USE_AVX512 OFF)
set(USE_LZCNT OFF)
set(USE_TZCNT OFF)
set(USE_F16C OFF)
set(USE_FMADD OFF)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY ON)
# Sin cómputo por GPU (pelo), sin profiler, renderizador de depuración ni ObjectStream.
set(JPH_USE_DX12 OFF)
set(JPH_USE_VK OFF)
set(JPH_USE_MTL OFF)
set(JPH_USE_CPU_COMPUTE OFF)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF)
set(ENABLE_OBJECT_STREAM OFF)
# /Wall /WX en código ajeno rompe al actualizar MSVC; LTCG solo en Jolt complica el enlace.
set(ENABLE_ALL_WARNINGS OFF)
set(INTERPROCEDURAL_OPTIMIZATION OFF)
set(ENABLE_INSTALL OFF)

FetchContent_Declare(JoltPhysics
    URL https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.6.0.tar.gz
    URL_HASH SHA256=6e069ee0172478cc78182047aac87e5310ba14a67a53348ae14cc37801fd3f8e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR Build
    SYSTEM
)

# --- SDL 3.4.16 (estática) ----------------------------------------------------
set(SDL_SHARED OFF)
set(SDL_STATIC ON)
set(SDL_TEST_LIBRARY OFF)
set(SDL_TESTS OFF)
set(SDL_EXAMPLES OFF)
set(SDL_INSTALL OFF)

FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
    URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
)

# --- raylib 6.0 con backend SDL -----------------------------------------------
# raylib usa SDL3::SDL3 si el target ya existe, por eso SDL se hace disponible antes.
set(PLATFORM SDL)
set(BUILD_EXAMPLES OFF)

FetchContent_Declare(raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/6.0.tar.gz
    URL_HASH SHA256=2b3ee1e2120c7a0796b33062c7e9a694dd8a8caa56a96319ac8c8ecf54a90d0b
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
)

# --- doctest v2.5.3 (solo tests) ----------------------------------------------
set(DOCTEST_WITH_TESTS OFF)
set(DOCTEST_NO_INSTALL ON)
set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF)

FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz
    URL_HASH SHA256=174ebc4e769928959614789c5b4e9c3d0a0f81a62bb608756b127bfebfb21331
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
)

# --- Fuentes compartidas entre copias de trabajo (tools/ci/deps.ps1) --------------------------
# Con RALLY_DEPS_SRC en el entorno, cada dependencia sale de <RALLY_DEPS_SRC>/<nombre>-<8 del
# hash> si su sello .rally-sha256 es el hash de arriba; si no, se descarga como siempre. Los
# hashes se leen de los FetchContent_Declare de este archivo (como hace deps.ps1): una sola copia.
# Solo se comparten las fuentes: cada copia compila en su build/ (nunca FETCHCONTENT_BASE_DIR).
# Se decide en cada configuración: FetchContent copia a la caché la variable normal, así que la
# de caché se borra antes. Sin eso, al quitar RALLY_DEPS_SRC o al cambiar de versión se quedaría
# pegada en CMakeCache.txt (medido en la P1a). Un -DFETCHCONTENT_SOURCE_DIR_* a mano no se respeta.
file(READ "${CMAKE_CURRENT_LIST_FILE}" _rally_deps_text)
string(REGEX MATCHALL
    "FetchContent_Declare\\([ \t\r\n]*[A-Za-z0-9_]+[ \t\r\n]+URL[ \t\r\n]+[^ \t\r\n]+[ \t\r\n]+URL_HASH[ \t\r\n]+SHA256=[0-9a-f]+"
    _rally_deps_declares "${_rally_deps_text}")
foreach(_rally_declare IN LISTS _rally_deps_declares)
    string(REGEX REPLACE "^FetchContent_Declare\\([ \t\r\n]*([A-Za-z0-9_]+).*SHA256=([0-9a-f]+)$"
        "\\1;\\2" _rally_pair "${_rally_declare}")
    list(GET _rally_pair 0 _rally_name)
    list(GET _rally_pair 1 _rally_sha)
    string(TOUPPER "${_rally_name}" _rally_upper)
    unset(FETCHCONTENT_SOURCE_DIR_${_rally_upper})
    unset(FETCHCONTENT_SOURCE_DIR_${_rally_upper} CACHE)
    if(DEFINED ENV{RALLY_DEPS_SRC} AND NOT "$ENV{RALLY_DEPS_SRC}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{RALLY_DEPS_SRC}" _rally_root)
        string(SUBSTRING "${_rally_sha}" 0 8 _rally_short)
        set(_rally_dir "${_rally_root}/${_rally_name}-${_rally_short}")
        set(_rally_stamp "")
        if(EXISTS "${_rally_dir}/.rally-sha256")
            file(READ "${_rally_dir}/.rally-sha256" _rally_stamp)
            string(STRIP "${_rally_stamp}" _rally_stamp)
        endif()
        if(_rally_stamp STREQUAL _rally_sha)
            set(FETCHCONTENT_SOURCE_DIR_${_rally_upper} "${_rally_dir}")
            message(STATUS "${_rally_name}: fuentes compartidas de ${_rally_dir}")
        else()
            message(WARNING "${_rally_name}: RALLY_DEPS_SRC no tiene ${_rally_name}-${_rally_short} "
                "con su sello; se descarga. Para compartirla: tools/ci/deps.ps1")
        endif()
    endif()
endforeach()
list(LENGTH _rally_deps_declares _rally_deps_count)
if(NOT _rally_deps_count EQUAL 4)
    message(FATAL_ERROR "Dependencies.cmake: esperaba 4 FetchContent_Declare con URL y SHA256 "
        "y hay ${_rally_deps_count} (tools/ci/deps.ps1 los lee igual)")
endif()

FetchContent_MakeAvailable(JoltPhysics SDL3 raylib doctest)

if(NOT TARGET SDL3::SDL3)
    message(FATAL_ERROR "SDL3::SDL3 no existe: raylib no usaría el backend SDL.")
endif()
