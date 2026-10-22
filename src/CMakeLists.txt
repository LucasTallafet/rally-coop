add_subdirectory(sim)
# Lógica del juego (tarea 2.9): acciones; más adelante, sesión. Sin raylib ni SDL; la enlazan
# rally_io (repetición de las acciones), el juego y los tests.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/game/CMakeLists.txt)
    add_subdirectory(game)
endif()
add_subdirectory(platform)
add_subdirectory(io)
# Red (fase 3, lote 3): ENet, host y copiloto. Sin raylib ni SDL; la enlaza la plataforma.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/net/CMakeLists.txt)
    add_subdirectory(net)
endif()

add_executable(rally main.cpp)
target_link_libraries(rally PRIVATE rally_platform rally_io rally_sim rally_warnings)
# En Release, sin consola (subsistema WINDOWS) pero con main() como punto de entrada.
set_target_properties(rally PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
target_link_options(rally PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)

if(TARGET rally_game)
    target_link_libraries(rally PRIVATE rally_game)
endif()
