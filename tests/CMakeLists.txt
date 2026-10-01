# Los tests enlazan solo la simulación: corren sin ventana ni mando.
add_executable(rally_tests
    test_main.cpp
    test_jolt.cpp
    test_pcg32.cpp
    test_rmath.cpp
)
target_link_libraries(rally_tests PRIVATE rally_sim doctest::doctest rally_warnings)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(rally_tests)

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
