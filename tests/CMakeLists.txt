# Tests por carril (tarea P1a): cada carpeta tiene su CMakeLists.txt y su ejecutable doctest, y
# un test que mezcla carriles va con el de lo que mide. Este archivo solo pone lo común y las
# añade. Los tests enlazan solo la simulación, rally_io y el bucle sin dependencias: corren sin
# ventana ni mando.

# Datos de los tests (coches, grabaciones, registros de fotogramas) y salidas que se pasan entre
# tests de carriles distintos (fixtures de ctest: son globales). Las salidas siguen en
# build/<preset>/tests/, como antes del reparto.
set(RALLY_TEST_DATA ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(RALLY_TESTS_OUT ${CMAKE_CURRENT_BINARY_DIR})

# Lo común a todos los ejecutables doctest: el main de doctest y el contador global de
# operator new (sustituye al de la biblioteca estándar en cada ejecutable que lo enlaza).
add_library(rally_test_common OBJECT common/test_main.cpp common/new_counter.cpp)
target_include_directories(rally_test_common PUBLIC common)
target_link_libraries(rally_test_common PUBLIC doctest::doctest PRIVATE rally_warnings)

# Un ejecutable doctest de un carril: lo común, la simulación, rally_io y el bucle, los datos de
# los tests y la raíz del repositorio (la tabla de teclas de docs/07 §8).
include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
function(rally_add_test_executable name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE rally_test_common rally_sim rally_io rally_loop
        doctest::doctest rally_warnings)
    target_compile_definitions(${name} PRIVATE
        RALLY_TEST_DATA_DIR="${RALLY_TEST_DATA}"
        RALLY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    doctest_discover_tests(${name})
endfunction()

add_subdirectory(sim)
add_subdirectory(platform)
add_subdirectory(io)
add_subdirectory(tools)
add_subdirectory(ci)
