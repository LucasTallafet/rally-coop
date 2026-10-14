# Bucle sin dependencias: reloj de paso fijo, interpolación del estado de render, espacio de
# la cámara (render relativo), retención de botones en fotogramas sin pasos y forma de la
# entrada (zona muerta, curva, mezcla de teclado y mando, mando activo, vibración), lo que el
# conductor saca de cada paso (cabeza, vibración por superficie, sintetizador e interior; tarea
# 2.7), la parte del contador de rendimiento y del bosque de medida que se puede probar
# (estadísticas, reparto de árboles y malla), la historia y los canales de las gráficas de la
# telemetría, las tablas de teclas de cada componente (*_keys.cpp) y su registro
# (key_tables.cpp), el diseño de la pantalla (con la medida del texto de la fuente de raylib), el
# asiento y ajustes.txt. No enlaza raylib ni SDL, para que los tests lo usen sin ventana
# (cmake/CheckBuildRules.cmake); de rally_sim solo usa el PCG32 para el bosque.
add_library(rally_loop STATIC
    button_latch.cpp
    camera_space.cpp
    car_feedback.cpp
    car_synth.cpp
    cockpit.cpp
    demo_camera_keys.cpp
    demo_driver_feel_keys.cpp
    demo_measure_keys.cpp
    demo_screen_keys.cpp
    demo_world_keys.cpp
    demo_zone_modes_keys.cpp
    drive_keys.cpp
    fixed_step_clock.cpp
    forest_layout.cpp
    frame_history.cpp
    head_motion.cpp
    input_shaping.cpp
    interpolation.cpp
    key_tables.cpp
    keys.cpp
    mouse_steer.cpp
    perf_stats.cpp
    player_settings.cpp
    screen_layout.cpp
    seat.cpp
    slip.cpp
    surface_feel.cpp
    telemetry_history.cpp
    ui_text.cpp
    window_keys.cpp
)
target_include_directories(rally_loop PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rally_loop PRIVATE rally_sim rally_warnings)

# Plataforma: ventana, render, entrada, audio y grabación de la partida sobre raylib
# (backend SDL), y la demo: el compositor (drive_demo) y sus partes (demo_*.cpp).
# SDL se usa directamente para el intervalo de presentación, la frecuencia del monitor y los
# mandos (platform/gamepad).
add_library(rally_platform STATIC
    audio_output.cpp
    cockpit_draw.cpp
    demo_camera.cpp
    demo_driver_feel.cpp
    demo_measure.cpp
    demo_screen.cpp
    demo_world.cpp
    demo_zone_modes.cpp
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
    tree_instances.cpp
    zone_draw.cpp
)
target_include_directories(rally_platform PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rally_platform
    PUBLIC raylib rally_loop rally_io rally_sim
    PRIVATE rally_warnings SDL3::SDL3 user32 winmm
)
