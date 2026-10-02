cmake_minimum_required(VERSION 3.25)

# Solo Debug y Release; Jolt solo fija sus configuraciones si es el proyecto raíz.
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

project(RallyCoop LANGUAGES C CXX)

if(NOT MSVC OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Rally Coop solo se compila con MSVC (cl.exe).")
endif()
# Desde VS 2022 (MSVC 19.30), /fp:precise no contrae a FMA salvo con /fp:contract.
if(MSVC_VERSION LESS 1930)
    message(FATAL_ERROR "Hace falta Visual Studio 2022 o posterior (MSVC_VERSION >= 1930).")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Solo x64.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Runtime estático (/MT, /MTd) en todo, dependencias incluidas: un solo .exe sin DLL.
# Tiene que coincidir con el que fija Jolt (USE_STATIC_MSVC_RUNTIME_LIBRARY).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(cmake/Dependencies.cmake)

# Flags de coma flotante para todo lo que compila simulación.
# /fp:precise y nunca /fp:fast ni /fp:contract; sin /arch (x64 ya es SSE2 y SSE4.2
# llega por los intrinsics de Jolt). Lo comprueba cmake/CheckBuildRules.cmake.
add_library(rally_fp_strict INTERFACE)
target_compile_options(rally_fp_strict INTERFACE /fp:precise)

# Avisos solo para nuestro código; las dependencias entran como SYSTEM.
add_library(rally_warnings INTERFACE)
target_compile_options(rally_warnings INTERFACE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)

# Jolt también compila simulación: mismos flags de coma flotante y la misma guarda.
# (target_compile_options y no target_link_libraries: Jolt usa la firma LINK_PUBLIC,
# que no se puede mezclar con PRIVATE).
target_compile_options(Jolt PRIVATE /fp:precise)
target_sources(Jolt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/sim/fp_guard.cpp)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/sim/fp_guard.cpp
    TARGET_DIRECTORY Jolt
    PROPERTIES COMPILE_OPTIONS /utf-8 SKIP_PRECOMPILE_HEADERS ON)
# Asserts de Jolt en Debug. Se declara PUBLIC en el target de Jolt para que el juego
# reciba la misma definición que la biblioteca (forma parte de JPH_VERSION_ID).
target_compile_definitions(Jolt PUBLIC $<$<CONFIG:Debug>:JPH_ENABLE_ASSERTS>)

add_subdirectory(src)
add_subdirectory(tools/replay)

enable_testing()
add_subdirectory(tests)

include(cmake/CheckBuildRules.cmake)
