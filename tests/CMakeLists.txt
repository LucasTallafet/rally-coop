# Los tests enlazan solo la simulación y el bucle sin dependencias: corren sin ventana ni mando.
add_executable(rally_tests
    test_main.cpp
    new_counter.cpp
    test_button_latch.cpp
    test_camera_space.cpp
    test_fixed_step_clock.cpp
    test_frame_history.cpp
    test_interpolation.cpp
    test_jolt.cpp
    test_pcg32.cpp
    test_rmath.cpp
    test_sim_world.cpp
)
target_link_libraries(rally_tests PRIVATE rally_sim rally_loop doctest::doctest rally_warnings)
# Datos medidos para los tests (registros de fotogramas).
target_compile_definitions(rally_tests PRIVATE
    RALLY_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(rally_tests)

# Test de muerte: doctest no los tiene. Un ejecutable propio que tiene que abortar con el
# mensaje de RALLY_CHECK; si sigue después de la llamada prohibida, escribe NO_ABORT.
add_executable(rally_check_abort check_abort.cpp)
target_link_libraries(rally_check_abort PRIVATE rally_sim rally_warnings)
add_test(NAME sim_world.build_after_seal_aborts COMMAND rally_check_abort)
set_tests_properties(sim_world.build_after_seal_aborts PROPERTIES
    PASS_REGULAR_EXPRESSION "RALLY_CHECK: SimWorld::build: .*tras seal"
    FAIL_REGULAR_EXPRESSION "NO_ABORT")

# Reglas de simulación (cmake -P, sin Python). Se prueba el propio script con dos
# árboles de ejemplo: uno con un número exacto de infracciones y otro limpio.
set(_rules_script ${PROJECT_SOURCE_DIR}/cmake/CheckSimRules.cmake)
add_test(NAME sim_rules.repo
    COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -P ${_rules_script})
add_test(NAME sim_rules.fixture_bad
    COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/rules/bad -DEXPECTED=13
            -P ${_rules_script})
add_test(NAME sim_rules.fixture_good
    COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/rules/good -P ${_rules_script})
