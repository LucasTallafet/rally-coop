# Los tests enlazan solo la simulación, rally_io y el bucle sin dependencias: corren sin
# ventana ni mando.
add_executable(rally_tests
    test_main.cpp
    new_counter.cpp
    test_button_latch.cpp
    test_camera_space.cpp
    test_fixed_step_clock.cpp
    test_forest_layout.cpp
    test_frame_history.cpp
    test_input_shaping.cpp
    test_input_smoothing.cpp
    test_interpolation.cpp
    test_jolt.cpp
    test_pcg32.cpp
    test_perf_stats.cpp
    test_recording.cpp
    test_rmath.cpp
    test_sim_world.cpp
    test_test_zone.cpp
)
target_link_libraries(rally_tests PRIVATE rally_sim rally_io rally_loop doctest::doctest
    rally_warnings)
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

# Grabación incompleta: rally_abort_recording graba 600 pasos y aborta con un RALLY_CHECK de
# verdad; su manejador la guarda como la partida. Después rally_replay la repite hasta el
# último paso y avisa de que no hay hash. Se borra antes para no repetir una de otra pasada.
set(_abort_rrec ${CMAKE_CURRENT_BINARY_DIR}/abort_recording.rrec)
add_executable(rally_abort_recording abort_recording.cpp)
target_link_libraries(rally_abort_recording PRIVATE rally_io rally_sim rally_warnings)
add_test(NAME recording.abort_cleanup COMMAND ${CMAKE_COMMAND} -E rm -f ${_abort_rrec})
add_test(NAME recording.abort_saves_incomplete COMMAND rally_abort_recording ${_abort_rrec})
set_tests_properties(recording.abort_cleanup PROPERTIES FIXTURES_SETUP abort_recording)
set_tests_properties(recording.abort_saves_incomplete PROPERTIES
    FIXTURES_SETUP abort_recording
    DEPENDS recording.abort_cleanup
    PASS_REGULAR_EXPRESSION "grabación incompleta guardada: .*\\(600 pasos\\)"
    FAIL_REGULAR_EXPRESSION "NO_ABORT")
add_test(NAME replay.incomplete_recording COMMAND rally_replay run ${_abort_rrec})
set_tests_properties(replay.incomplete_recording PROPERTIES
    FIXTURES_REQUIRED abort_recording
    PASS_REGULAR_EXPRESSION "pasos: 600 .*aviso: grabación incompleta"
    FAIL_REGULAR_EXPRESSION "hash OK|FALLO")
# rally_replay con un archivo que no existe: error y código 2, sin colgarse.
add_test(NAME replay.missing_file COMMAND rally_replay info ${CMAKE_CURRENT_BINARY_DIR}/no_existe.rrec)
set_tests_properties(replay.missing_file PROPERTIES
    PASS_REGULAR_EXPRESSION "error: no se puede abrir")

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

# Zona de pruebas (tarea 2.2). rally_replay escribe el trazado en JSON y un test de Python
# (solo biblioteca estándar) lo comprueba contra las medidas de la tabla. Sin Python 3 el
# test falla con su nombre a la vista, en vez de desaparecer.
set(_zone_json ${CMAKE_CURRENT_BINARY_DIR}/zone.json)
add_test(NAME replay.zone_json COMMAND rally_replay zone --json ${_zone_json})
set_tests_properties(replay.zone_json PROPERTIES
    FIXTURES_SETUP zone_json
    PASS_REGULAR_EXPRESSION "zona escrita: ")
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME zone_json.python
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/telemetry/test_zone_json.py
                ${_zone_json})
else()
    add_test(NAME zone_json.python COMMAND ${CMAKE_COMMAND} -E false)
    message(WARNING "Python 3 no encontrado: el test zone_json.python fallará")
endif()
set_tests_properties(zone_json.python PROPERTIES FIXTURES_REQUIRED zone_json)
# Vuelta cronometrada grabada (la genera el test de la zona con RALLY_WRITE_TEST_RECORDINGS=1):
# rally_replay la repite con el mismo tiempo y el mismo hash.
add_test(NAME replay.zone_lap
    COMMAND rally_replay run ${CMAKE_CURRENT_SOURCE_DIR}/data/recordings/zone_lap.rrec)
set_tests_properties(replay.zone_lap PROPERTIES
    PASS_REGULAR_EXPRESSION "vuelta: [0-9]+ pasos .*hash OK"
    FAIL_REGULAR_EXPRESSION "FALLO|vuelta: ninguno")
