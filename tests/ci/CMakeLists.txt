# Tests de las barreras del desarrollo por lotes (tarea P1b, docs/09-agentes.md §4 y §6): el hook
# de rutas, la tabla de dueños, propiedad.ps1 y los push (pushurl, pre-push y promover.ps1). Son
# del consultor, como las barreras: un agente no puede debilitarlos. Python y PowerShell, sin
# ventana ni red (el remoto es una carpeta temporal).

set(Python3_FIND_REGISTRY LAST)
find_package(Python3 COMPONENTS Interpreter)
set(_test_rutas ${CMAKE_CURRENT_SOURCE_DIR}/test_rutas.py)
if(Python3_Interpreter_FOUND)
    add_test(NAME hook_rutas.cases COMMAND ${Python3_EXECUTABLE} ${_test_rutas} hook)
    add_test(NAME hook_rutas.branch_mode COMMAND ${Python3_EXECUTABLE} ${_test_rutas} rama)
    add_test(NAME propiedad.owners
        COMMAND ${Python3_EXECUTABLE} ${_test_rutas} duenos ${PROJECT_SOURCE_DIR})
else()
    # Sin Python, el hook bloquea todas las escrituras: que se vea aquí con su nombre.
    foreach(_name hook_rutas.cases hook_rutas.branch_mode propiedad.owners)
        add_test(NAME ${_name} COMMAND ${CMAKE_COMMAND} -E false)
    endforeach()
    message(WARNING "Python 3 no encontrado: los tests hook_rutas.* y propiedad.owners fallarán")
endif()
# Además del código de salida, una línea FALLO hace fallar el test.
set_tests_properties(hook_rutas.cases hook_rutas.branch_mode propiedad.owners PROPERTIES
    FAIL_REGULAR_EXPRESSION "FALLO")

find_program(RALLY_POWERSHELL NAMES powershell REQUIRED)
foreach(_mode push promover propiedad)
    add_test(NAME ci_barreras.${_mode}
        COMMAND ${RALLY_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/test_barreras.ps1 ${_mode})
    set_tests_properties(ci_barreras.${_mode} PROPERTIES
        FAIL_REGULAR_EXPRESSION "FALLO"
        TIMEOUT 300)
endforeach()
