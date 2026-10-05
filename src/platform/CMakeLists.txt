# Bucle sin dependencias: reloj de paso fijo, interpolación del estado de render, espacio de
# la cámara (render relativo), retención de botones en fotogramas sin pasos y forma de la
# entrada (zona muerta, curva, mezcla de teclado y mando, mando activo, vibración), y la parte
# del contador de rendimiento y del bosque de medida que se puede probar (estadísticas, reparto
# de árboles y malla), la historia y los canales de las gráficas de la telemetría, la tabla de teclas, el diseño
# de la pantalla (con la medida del texto de la fuente de raylib), el asiento y ajustes.txt. No enlaza raylib ni SDL, para que los tests lo usen sin ventana
# (cmake/CheckBuildRules.cmake); de rally_sim solo usa el PCG32 para el bosque.
add_library(rally_loop STATIC
    button_latch.cpp
    camera_space.cpp
    fixed_step_clock.cpp
    forest_layout.cpp
    frame_history.cpp
    input_shaping.cpp
    interpolation.cpp
    keys.cpp
    mouse_steer.cpp
    perf_stats.cpp
    player_settings.cpp
    screen_layout.cpp
    seat.cpp
    telemetry_history.cpp
    ui_text.cpp
)
target_include_directories(rally_loop PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rally_loop PRIVATE rally_sim rally_warnings)

# Plataforma: ventana, render, entrada, audio y grabación de la partida sobre raylib
# (backend SDL).
# SDL se usa directamente para el intervalo de presentación, la frecuencia del monitor y los
# mandos (platform/gamepad).
add_library(rally_platform STATIC
    drive_demo.cpp
    forest_bench.cpp
    forest_scene.cpp
    frame_graph.cpp
    frame_log.cpp
    fatal_error.cpp
    frame_pacer.cpp
    gamepad.cpp
    gl_probe.cpp
    input.cpp
    keys_raylib.cpp
    overlay.cpp
    perf_counter.cpp
    process_memory.cpp
    recording_session.cpp
    screenshot.cpp
    telemetry_graphs.cpp
    window.cpp
    zone_draw.cpp
)
target_include_directories(rally_platform PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rally_platform
    PUBLIC raylib rally_loop rally_io rally_sim
    PRIVATE rally_warnings SDL3::SDL3 user32 winmm
)
