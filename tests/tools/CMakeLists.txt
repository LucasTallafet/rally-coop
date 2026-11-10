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

# rally_replay info con el tramo (lote 5, replay-tramo). Valores exactos: el nombre y los 2887 B
# son de data/stages/lazadas.stage (= tests/sim/stages/lazadas.stage, byte a byte, con sus CRLF);
# el hash del suelo y la pasada los calculan otros carriles (kExpectedStageGroundHash en
# tests/sim, la recolocación en tests/io): aquí solo la forma. La relación de la pasada (m:ss,cc =
# pasos / 240 truncado a la centésima, igual en info y en run) la comprueba check_pass_line.py.
add_test(NAME replay.info_stage_v8
    COMMAND rally_replay info ${RALLY_TEST_DATA}/recordings/stage_lazadas.rrec)
set_tests_properties(replay.info_stage_v8 PROPERTIES
    PASS_REGULAR_EXPRESSION
    "formato: +8\n.*\ntramo: +Lazadas \\(test\\), 1150\\.0 m, texto de 2887 bytes\nhash de la zona: +0x0000000000000000 \\(sin zona\\)\n.*\nhash del suelo: +0x[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]\npasada: +[0-9]+:[0-9][0-9],[0-9][0-9] \\([0-9]+ pasos\\), [0-9]+ recolocaciones, [0-9]+ atajos\n")
# Sin tramo, ni una línea más: una grabación de formato 6 y otra de formato 8 sin tramo (la de la
# zona reescrita al formato actual: cada grabación de la zona que se haga de ahora en adelante).
add_test(NAME replay.info_no_stage_v6
    COMMAND rally_replay info ${RALLY_TEST_DATA}/recordings/zone_lap.rrec)
set_tests_properties(replay.info_no_stage_v6 PROPERTIES
    PASS_REGULAR_EXPRESSION "formato: +6\n"
    FAIL_REGULAR_EXPRESSION "tramo:|hash del suelo|pasada:")
set(_zone_v8 ${RALLY_TESTS_OUT}/zone_lap_v8.rrec)
add_test(NAME replay.info_no_stage_v8_copy
    COMMAND ${CMAKE_COMMAND} -E copy ${RALLY_TEST_DATA}/recordings/zone_lap.rrec ${_zone_v8})
set_tests_properties(replay.info_no_stage_v8_copy PROPERTIES FIXTURES_SETUP zone_v8_copy)
add_test(NAME replay.info_no_stage_v8_rewrite COMMAND rally_replay rewrite ${_zone_v8})
set_tests_properties(replay.info_no_stage_v8_rewrite PROPERTIES
    FIXTURES_REQUIRED zone_v8_copy FIXTURES_SETUP zone_v8)
add_test(NAME replay.info_no_stage_v8 COMMAND rally_replay info ${_zone_v8})
set_tests_properties(replay.info_no_stage_v8 PROPERTIES
    FIXTURES_REQUIRED zone_v8
    PASS_REGULAR_EXPRESSION "formato: +8\n"
    FAIL_REGULAR_EXPRESSION "tramo:|hash del suelo|pasada:")

# rally_replay run con el tramo (lote 5, replay-run). `run` imprime la pasada repetida con el
# formato de `info`; el CSV de telemetría lleva las seis columnas del tramo con sus valores; un
# tramo cambiado es un aviso (código 0), no un FALLO; y `info` dice «sin meta» y «mundo: no
# válido» (con el tramo dentro) en vez de callar. Las copias modificadas las hace
# make_rrec_variant.py sobre stage_lazadas.rrec, sin cambiar ninguna longitud.
add_test(NAME replay.run_stage_pass
    COMMAND rally_replay run ${RALLY_TEST_DATA}/recordings/stage_lazadas.rrec)
set_tests_properties(replay.run_stage_pass PROPERTIES
    PASS_REGULAR_EXPRESSION
    "\npasada: +[0-9]+:[0-9][0-9],[0-9][0-9] [(][0-9]+ pasos[)], [0-9]+ recolocaciones, [0-9]+ atajos\n.*hash OK"
    FAIL_REGULAR_EXPRESSION "FALLO|aviso")
# Sin tramo, `run` no imprime la pasada.
add_test(NAME replay.run_no_stage_pass
    COMMAND rally_replay run ${RALLY_TEST_DATA}/recordings/zone_lap.rrec)
set_tests_properties(replay.run_no_stage_pass PROPERTIES
    PASS_REGULAR_EXPRESSION "hash OK"
    FAIL_REGULAR_EXPRESSION "pasada:|FALLO")
if(Python3_Interpreter_FOUND AND Python3_VERSION VERSION_GREATER_EQUAL 3.12)
    set(_stage_rrec ${RALLY_TEST_DATA}/recordings/stage_lazadas.rrec)
    set(_stage_csv ${RALLY_TESTS_OUT}/stage_run.csv)
    add_test(NAME replay.run_stage_csv
        COMMAND rally_replay run ${_stage_rrec} --telemetry ${_stage_csv})
    set_tests_properties(replay.run_stage_csv PROPERTIES
        FIXTURES_SETUP stage_run_csv
        PASS_REGULAR_EXPRESSION "139 columnas.*hash OK" FAIL_REGULAR_EXPRESSION "FALLO|aviso")
    add_test(NAME replay.run_stage_csv_columns
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_stage_csv.py ${_stage_csv})
    set_tests_properties(replay.run_stage_csv_columns PROPERTIES
        FIXTURES_REQUIRED stage_run_csv PASS_REGULAR_EXPRESSION "CSV del tramo correcto: ")

    # La pasada de stage_lazadas.rrec (lote 6, replay-desacople): relación, no cifra. El tiempo es
    # pasos / 240 truncado a la centésima y la línea es igual en info y en run.
    set(_pass_py ${CMAKE_CURRENT_SOURCE_DIR}/check_pass_line.py)
    add_test(NAME replay.pass_line_relation
        COMMAND ${Python3_EXECUTABLE} ${_pass_py} --run $<TARGET_FILE:rally_replay> ${_stage_rrec})
    set_tests_properties(replay.pass_line_relation PROPERTIES
        PASS_REGULAR_EXPRESSION "pasada correcta: ")
    # Salidas fabricadas (cifras que no son las de la grabación). El control positivo va primero:
    # sin él, un modo archivo roto haría «pasar» los negativos. 14639 pasos = 60,9958 s: truncado
    # 1:00,99 (redondeado sería 1:01,00).
    set(_fake_ok ${RALLY_TESTS_OUT}/pass_line_ok.txt)
    set(_fake_time ${RALLY_TESTS_OUT}/pass_line_bad_time.txt)
    set(_fake_steps ${RALLY_TESTS_OUT}/pass_line_bad_steps.txt)
    set(_fake_other ${RALLY_TESTS_OUT}/pass_line_other.txt)
    file(WRITE ${_fake_ok} "pasada: 1:00,99 (14639 pasos), 1 recolocaciones, 0 atajos\n")
    file(WRITE ${_fake_time} "pasada: 1:01,00 (14639 pasos), 1 recolocaciones, 0 atajos\n")
    file(WRITE ${_fake_steps} "pasada: 1:00,99 (14640 pasos), 1 recolocaciones, 0 atajos\n")
    file(WRITE ${_fake_other} "pasada: 1:00,99 (14639 pasos), 2 recolocaciones, 0 atajos\n")
    add_test(NAME replay.pass_line_fake_ok COMMAND ${Python3_EXECUTABLE} ${_pass_py} ${_fake_ok})
    set_tests_properties(replay.pass_line_fake_ok PROPERTIES
        PASS_REGULAR_EXPRESSION "pasada correcta: ")
    # Los negativos piden el mensaje del script (que sale con 1 por sys.exit): un Traceback o un
    # archivo que falta no lo contienen.
    add_test(NAME replay.pass_line_rejects_time
        COMMAND ${Python3_EXECUTABLE} ${_pass_py} ${_fake_time})
    set_tests_properties(replay.pass_line_rejects_time PROPERTIES PASS_REGULAR_EXPRESSION "no cuadra")
    add_test(NAME replay.pass_line_rejects_steps
        COMMAND ${Python3_EXECUTABLE} ${_pass_py} ${_fake_steps})
    set_tests_properties(replay.pass_line_rejects_steps PROPERTIES PASS_REGULAR_EXPRESSION "no cuadra")
    add_test(NAME replay.pass_line_rejects_differing
        COMMAND ${Python3_EXECUTABLE} ${_pass_py} ${_fake_ok} ${_fake_other})
    set_tests_properties(replay.pass_line_rejects_differing PROPERTIES
        PASS_REGULAR_EXPRESSION "difiere entre info y run")

    set(_variant_py ${CMAKE_CURRENT_SOURCE_DIR}/make_rrec_variant.py)
    foreach(_mode changed-stage invalid-stage no-finish)
        add_test(NAME replay.variant_${_mode}
            COMMAND ${Python3_EXECUTABLE} ${_variant_py} ${_mode} ${_stage_rrec}
                    ${RALLY_TESTS_OUT}/stage_${_mode}.rrec)
        set_tests_properties(replay.variant_${_mode} PROPERTIES FIXTURES_SETUP variant_${_mode})
    endforeach()
    # El texto del tramo cambiado: aviso, no FALLO, y código 0 (el test falla si no).
    add_test(NAME replay.run_stage_changed
        COMMAND rally_replay run ${RALLY_TESTS_OUT}/stage_changed-stage.rrec)
    set_tests_properties(replay.run_stage_changed PROPERTIES
        FIXTURES_REQUIRED variant_changed-stage
        PASS_REGULAR_EXPRESSION "aviso: el tramo ha cambiado .*no se compara el hash"
        FAIL_REGULAR_EXPRESSION "FALLO|hash OK")
    add_test(NAME replay.info_stage_no_finish
        COMMAND rally_replay info ${RALLY_TESTS_OUT}/stage_no-finish.rrec)
    set_tests_properties(replay.info_stage_no_finish PROPERTIES
        FIXTURES_REQUIRED variant_no-finish
        PASS_REGULAR_EXPRESSION "\npasada: +sin meta, 1 recolocaciones, 0 atajos\n")
    add_test(NAME replay.info_stage_invalid
        COMMAND rally_replay info ${RALLY_TESTS_OUT}/stage_invalid-stage.rrec)
    set_tests_properties(replay.info_stage_invalid PROPERTIES
        FIXTURES_REQUIRED variant_invalid-stage
        PASS_REGULAR_EXPRESSION "\nmundo: +no válido [(]descripción del mundo: tramo no válido: "
        FAIL_REGULAR_EXPRESSION "\npasada:|\nhash del suelo:")
else()
    message(WARNING "Python 3.12 o posterior no encontrado: los tests replay.run_stage_csv*, "
        "replay.variant_*, replay.pass_line_*, replay.run_stage_changed y replay.info_stage_* no se añaden")
endif()
