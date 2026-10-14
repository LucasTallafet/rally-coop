# Carril de los formatos (src/io): grabaciones, su repetición y el guardado de emergencia.
rally_add_test_executable(rally_tests_io test_recording.cpp)
target_include_directories(rally_tests_io PRIVATE ${PROJECT_SOURCE_DIR}/tests/sim/common)

# Grabación incompleta: rally_abort_recording graba 600 pasos y aborta con un RALLY_CHECK de
# verdad; su manejador la guarda como la partida. Después rally_replay la repite hasta el
# último paso y avisa de que no hay hash. Se borra antes para no repetir una de otra pasada.
set(_abort_rrec ${RALLY_TESTS_OUT}/abort_recording.rrec)
add_executable(rally_abort_recording abort_recording.cpp)
target_link_libraries(rally_abort_recording PRIVATE rally_io rally_sim rally_warnings)
target_include_directories(rally_abort_recording PRIVATE ${PROJECT_SOURCE_DIR}/tests/sim/common)
target_compile_definitions(rally_abort_recording PRIVATE
    RALLY_TEST_DATA_DIR="${RALLY_TEST_DATA}")
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
add_test(NAME replay.missing_file COMMAND rally_replay info ${RALLY_TESTS_OUT}/no_existe.rrec)
set_tests_properties(replay.missing_file PROPERTIES
    PASS_REGULAR_EXPRESSION "error: no se puede abrir")
