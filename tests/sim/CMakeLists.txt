# Carril de la simulación: coche, mundo, zona, carretera, rmath y PCG, la huella, las reservas y
# el hash de la telemetría, y la reproducibilidad de las grabaciones de tests/data/recordings/.
# common/car_worlds.h (los mundos de prueba del coche) es de este carril; los demás lo incluyen.
rally_add_test_executable(rally_tests_sim
    test_aids.cpp
    test_car.cpp
    test_drivetrain.cpp
    test_input_smoothing.cpp
    test_jolt.cpp
    test_pcg32.cpp
    test_rmath.cpp
    test_sim_world.cpp
    test_steer_assist.cpp
    test_telemetry.cpp
    test_test_road.cpp
    test_test_zone.cpp
    test_tire.cpp
)
target_include_directories(rally_tests_sim PRIVATE common)

# Test de muerte: doctest no los tiene. Un ejecutable propio que tiene que abortar con el
# mensaje de RALLY_CHECK; si sigue después de la llamada prohibida, escribe NO_ABORT.
add_executable(rally_check_abort check_abort.cpp)
target_link_libraries(rally_check_abort PRIVATE rally_sim rally_warnings)
add_test(NAME sim_world.build_after_seal_aborts COMMAND rally_check_abort build_after_seal)
set_tests_properties(sim_world.build_after_seal_aborts PROPERTIES
    PASS_REGULAR_EXPRESSION "RALLY_CHECK: SimWorld::build: .*tras seal"
    FAIL_REGULAR_EXPRESSION "NO_ABORT")
# Tarea 2.8: la suspensión blanda con un coche sin [settings] (una grabación antigua) aborta.
add_test(NAME sim_world.soft_without_settings_aborts
    COMMAND rally_check_abort soft_without_settings)
set_tests_properties(sim_world.soft_without_settings_aborts PROPERTIES
    PASS_REGULAR_EXPRESSION "RALLY_CHECK: SimWorld::build: .*\\[settings\\]"
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

# Vuelta cronometrada grabada (la genera el test de la zona con RALLY_WRITE_TEST_RECORDINGS=1):
# rally_replay la repite con el mismo tiempo y el mismo hash.
add_test(NAME replay.zone_lap
    COMMAND rally_replay run ${RALLY_TEST_DATA}/recordings/zone_lap.rrec)
set_tests_properties(replay.zone_lap PROPERTIES
    PASS_REGULAR_EXPRESSION "vuelta: [0-9]+ pasos .*hash OK"
    FAIL_REGULAR_EXPRESSION "FALLO|vuelta: ninguno|aviso")
# La misma vuelta con otro hash de la zona: rally_replay avisa y no compara el hash final.
add_test(NAME replay.zone_changed
    COMMAND rally_replay run ${RALLY_TEST_DATA}/recordings/zone_lap_other_zone.rrec)
set_tests_properties(replay.zone_changed PROPERTIES
    PASS_REGULAR_EXPRESSION "aviso: la zona ha cambiado"
    FAIL_REGULAR_EXPRESSION "FALLO|hash OK")

# Carretera de prueba (tarea 2.6b): la pasada del conductor automático y la que se sale en la
# horquilla (las genera el test de la carretera con RALLY_WRITE_TEST_RECORDINGS=1). rally_replay
# las repite con la misma pasada y el mismo hash, y escribe su telemetría para road_check.py.
foreach(_road road_run road_off)
    add_test(NAME replay.${_road}
        COMMAND rally_replay run ${RALLY_TEST_DATA}/recordings/${_road}.rrec
                --telemetry ${RALLY_TESTS_OUT}/${_road}.csv)
    set_tests_properties(replay.${_road} PROPERTIES
        FIXTURES_SETUP road_csv
        PASS_REGULAR_EXPRESSION "carretera: [0-9]+ pasos .*1 pasadas.*hash OK"
        FAIL_REGULAR_EXPRESSION "FALLO|aviso")
endforeach()

# Grabación sintética del coche (tarea 2.4): 120 s en la zona con el coche congelado
# (rally_replay synth). La repetición da el mismo hash; --bench mide con ella (a mano).
add_test(NAME replay.bench_lap
    COMMAND rally_replay run ${RALLY_TEST_DATA}/recordings/bench_lap.rrec)
set_tests_properties(replay.bench_lap PROPERTIES
    PASS_REGULAR_EXPRESSION "pasos: 28800 .*hash OK"
    FAIL_REGULAR_EXPRESSION "FALLO|aviso")
# synth es determinista: lo que escribe ahora es lo mismo que la guardada.
set(_bench_synth ${RALLY_TESTS_OUT}/bench_lap_synth.rrec)
add_test(NAME replay.bench_lap_synth
    COMMAND rally_replay synth ${_bench_synth} --car ${RALLY_TEST_DATA}/cars/reference.car)
set_tests_properties(replay.bench_lap_synth PROPERTIES
    FIXTURES_SETUP bench_synth
    PASS_REGULAR_EXPRESSION "grabación sintética escrita: ")
add_test(NAME replay.bench_lap_synth_matches
    COMMAND rally_replay run ${_bench_synth})
set_tests_properties(replay.bench_lap_synth_matches PROPERTIES
    FIXTURES_REQUIRED bench_synth
    PASS_REGULAR_EXPRESSION "hash OK")

# La misma grabación da el mismo CSV byte a byte dos veces (la telemetría de la simulación, la
# misma en Debug y en Release, la comprueba kExpectedTelemetryHash en
# tests/sim/test_telemetry.cpp). Los CSV los usan también los tests de Python de tests/tools.
set(_lap_rrec ${RALLY_TEST_DATA}/recordings/zone_lap.rrec)
foreach(_run first second)
    add_test(NAME replay.telemetry_${_run}
        COMMAND rally_replay run ${_lap_rrec} --telemetry
                ${RALLY_TESTS_OUT}/telemetry_${_run}.csv)
    set_tests_properties(replay.telemetry_${_run} PROPERTIES
        FIXTURES_SETUP telemetry_csv
        PASS_REGULAR_EXPRESSION "telemetría: .*128 columnas.*hash OK"
        FAIL_REGULAR_EXPRESSION "FALLO|aviso")
endforeach()
add_test(NAME replay.telemetry_twice_identical
    COMMAND ${CMAKE_COMMAND} -E compare_files ${RALLY_TESTS_OUT}/telemetry_first.csv
            ${RALLY_TESTS_OUT}/telemetry_second.csv)
set_tests_properties(replay.telemetry_twice_identical PROPERTIES FIXTURES_REQUIRED telemetry_csv)
