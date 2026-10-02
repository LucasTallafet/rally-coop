# Bucle sin dependencias: reloj de paso fijo, interpolación del estado de render, espacio de
# la cámara (render relativo), retención de botones en fotogramas sin pasos y forma de la
# entrada (zona muerta, curva, mezcla de teclado y mando, mando activo, vibración), y la parte
# del contador de rendimiento y del bosque de medida que se puede probar (estadísticas, reparto
# de árboles y malla). No enlaza raylib ni SDL, para que los tests lo usen sin ventana
# (cmake/CheckBuildRules.cmake); de rally_sim solo usa el PCG32 para el bosque.
add_library(rally_loop STATIC
    button_latch.cpp
    camera_space.cpp
    fixed_step_clock.cpp
    forest_layout.cpp
    frame_history.cpp
    input_shaping.cpp
    interpolation.cpp
    perf_stats.cpp
)
target_include_directories(rally_loop PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rally_loop PRIVATE rally_sim rally_warnings)

# Plataforma: ventana, render, entrada y audio sobre raylib (backend SDL).
# SDL se usa directamente para el intervalo de presentación, la frecuencia del monitor y los
# mandos (platform/gamepad).
add_library(rally_platform STATIC
    box_demo.cpp
    forest_bench.cpp
    forest_scene.cpp
    frame_graph.cpp
    frame_log.cpp
    fatal_error.cpp
    frame_pacer.cpp
    gamepad.cpp
    gl_probe.cpp
    input.cpp
    perf_counter.cpp
    process_memory.cpp
    screenshot.cpp
    window.cpp
)
target_include_directories(rally_platform PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rally_platform
    PUBLIC raylib rally_loop rally_sim
    PRIVATE rally_warnings SDL3::SDL3 user32 winmm
)
