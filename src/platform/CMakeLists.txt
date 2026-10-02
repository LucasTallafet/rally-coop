# Bucle sin dependencias: reloj de paso fijo, interpolación del estado de render, espacio de
# la cámara (render relativo) y retención de botones en fotogramas sin pasos.
# No enlaza raylib ni SDL, para que los tests lo usen sin ventana (cmake/CheckBuildRules.cmake).
add_library(rally_loop STATIC
    button_latch.cpp
    camera_space.cpp
    fixed_step_clock.cpp
    frame_history.cpp
    interpolation.cpp
)
target_include_directories(rally_loop PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rally_loop PRIVATE rally_warnings)

# Plataforma: ventana, render, entrada y audio sobre raylib (backend SDL).
# SDL se usa directamente solo para el intervalo de presentación y la frecuencia del monitor.
add_library(rally_platform STATIC
    box_demo.cpp
    frame_graph.cpp
    frame_log.cpp
    fatal_error.cpp
    frame_pacer.cpp
    input.cpp
    screenshot.cpp
    window.cpp
)
target_include_directories(rally_platform PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rally_platform
    PUBLIC raylib rally_loop rally_sim
    PRIVATE rally_warnings SDL3::SDL3 user32 winmm
)
