# Carril de las herramientas: rally_replay (zona en JSON, columnas, curvas del neumático) y los
# scripts de Python de tools/telemetry. Leen los CSV que escriben los tests de tests/sim
# (fixtures telemetry_csv y road_csv) desde RALLY_TESTS_OUT.

# Zona de pruebas (tarea 2.2). rally_replay escribe el trazado en JSON y un test de Python
# (solo biblioteca estándar) lo comprueba contra las medidas de la tabla. Sin Python 3 el
# test falla con su nombre a la vista, en vez de desaparecer.
set(_zone_json ${RALLY_TESTS_OUT}/zone.json)
add_test(NAME replay.zone_json COMMAND rally_replay zone --json ${_zone_json})
set_tests_properties(replay.zone_json PROPERTIES
    FIXTURES_SETUP zone_json
    PASS_REGULAR_EXPRESSION "zona escrita: ")
# El Python del PATH antes que el último del registro de Windows: es el mismo que `python` en
# la terminal, donde se instala matplotlib (tools/telemetry/requirements.txt).
set(Python3_FIND_REGISTRY LAST)
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

# Telemetría (tarea 2.3). La tabla de columnas generada coincide con tools/telemetry/columns.md
# (sin mirar los finales de línea: git puede dejarlo con CRLF).
set(_columns_md ${RALLY_TESTS_OUT}/columns.md)
add_test(NAME replay.columns_md COMMAND rally_replay columns --md ${_columns_md})
set_tests_properties(replay.columns_md PROPERTIES
    FIXTURES_SETUP columns_md
    PASS_REGULAR_EXPRESSION "columnas escritas: ")
add_test(NAME replay.columns_md_matches_repository
    COMMAND ${CMAKE_COMMAND} -E compare_files --ignore-eol ${_columns_md}
            ${PROJECT_SOURCE_DIR}/tools/telemetry/columns.md)
set_tests_properties(replay.columns_md_matches_repository PROPERTIES
    FIXTURES_REQUIRED columns_md)
# Curvas del neumático (tarea 2.5) del coche congelado, para tools/telemetry/tire_curves.py.
set(_tire_curves_csv ${RALLY_TESTS_OUT}/tire_curves.csv)
add_test(NAME replay.tire_curves
    COMMAND rally_replay tire-curves ${RALLY_TEST_DATA}/cars/reference.car
            --csv ${_tire_curves_csv})
set_tests_properties(replay.tire_curves PROPERTIES
    FIXTURES_SETUP tire_curves
    PASS_REGULAR_EXPRESSION "asfalto \\| 1.10 \\| 7.0.*curvas escritas: ")

# Scripts de telemetría en Python: con Python 3.12 o posterior; plot.py y compare.py, además,
# con matplotlib. Si falta, se avisa y esos tests no se añaden.
set(_telemetry_dir ${PROJECT_SOURCE_DIR}/tools/telemetry)
if(Python3_Interpreter_FOUND AND Python3_VERSION VERSION_GREATER_EQUAL 3.12)
    add_test(NAME telemetry_py.drift_check_synthetic
        COMMAND ${Python3_EXECUTABLE} ${_telemetry_dir}/test_telemetry.py synthetic)
    add_test(NAME telemetry_py.recording
        COMMAND ${Python3_EXECUTABLE} ${_telemetry_dir}/test_telemetry.py recording
                ${RALLY_TESTS_OUT}/telemetry_first.csv ${_zone_json})
    set_tests_properties(telemetry_py.recording PROPERTIES
        FIXTURES_REQUIRED "telemetry_csv;zone_json")
    add_test(NAME telemetry_py.road_check_synthetic
        COMMAND ${Python3_EXECUTABLE} ${_telemetry_dir}/test_road_check.py synthetic)
    add_test(NAME telemetry_py.road_check_recordings
        COMMAND ${Python3_EXECUTABLE} ${_telemetry_dir}/test_road_check.py recordings
                ${RALLY_TESTS_OUT}/road_run.csv ${RALLY_TESTS_OUT}/road_off.csv
                ${_zone_json})
    set_tests_properties(telemetry_py.road_check_recordings PROPERTIES
        FIXTURES_REQUIRED "road_csv;zone_json")
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import matplotlib"
        RESULT_VARIABLE _matplotlib_missing OUTPUT_QUIET ERROR_QUIET)
    if(_matplotlib_missing EQUAL 0)
        foreach(_script plot compare)
            add_test(NAME telemetry_py.${_script}
                COMMAND ${Python3_EXECUTABLE} ${_telemetry_dir}/test_plots.py ${_script}
                        ${RALLY_TESTS_OUT}/telemetry_first.csv
                        ${RALLY_TESTS_OUT}/plots)
            set_tests_properties(telemetry_py.${_script} PROPERTIES
                FIXTURES_REQUIRED telemetry_csv
                PASS_REGULAR_EXPRESSION "imagen correcta: ")
        endforeach()
        add_test(NAME telemetry_py.tire_curves
            COMMAND ${Python3_EXECUTABLE} ${_telemetry_dir}/test_plots.py tire_curves
                    ${_tire_curves_csv} ${RALLY_TESTS_OUT}/plots)
        set_tests_properties(telemetry_py.tire_curves PROPERTIES
            FIXTURES_REQUIRED tire_curves
            PASS_REGULAR_EXPRESSION "imagen correcta: ")
    else()
        message(WARNING "matplotlib no está en ${Python3_EXECUTABLE}: los tests "
            "telemetry_py.plot y telemetry_py.compare no se añaden "
            "(python -m pip install --user -r tools/telemetry/requirements.txt)")
    endif()
else()
    message(WARNING "Python 3.12 o posterior no encontrado: los tests telemetry_py.* no se "
        "añaden")
endif()

# rally_replay info (lote 2): las ayudas de la descripción del mundo y, con el formato 7, las
# acciones, el número de eventos (12 en actions_v7.rrec) y el hash de la línea de tiempo. Una
# grabación anterior a la 7 enseña las ayudas por defecto y nada de acciones.
add_test(NAME replay.info_actions_v7
    COMMAND rally_replay info ${RALLY_TEST_DATA}/recordings/actions_v7.rrec)
set_tests_properties(replay.info_actions_v7 PROPERTIES
    PASS_REGULAR_EXPRESSION
    "ayuda de dirección: +sí.*caja de cambios: +automática.*ABS: +sí.*control de tracción: +alto.*suspensión: +blanda.*action = guardar_tablet.*action = prueba_parado.*eventos: 12\n  paso 0, copiloto, petición, acción 0 \\(guardar_tablet\\).*y 2 más\nhash línea de tiempo: 0xf3fdf0407b175af8")
add_test(NAME replay.info_zone_lap_no_actions
    COMMAND rally_replay info ${RALLY_TEST_DATA}/recordings/zone_lap.rrec)
set_tests_properties(replay.info_zone_lap_no_actions PROPERTIES
    PASS_REGULAR_EXPRESSION
    "ayuda de dirección: +no.*caja de cambios: +automática.*ABS: +no.*control de tracción: +no.*suspensión: +media"
    FAIL_REGULAR_EXPRESSION "acciones|eventos|línea de tiempo|action =")
