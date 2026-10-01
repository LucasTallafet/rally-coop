# Bucle sin dependencias: reloj de paso fijo e interpolación del estado de render.
# No enlaza raylib ni SDL, para que los tests lo usen sin ventana (cmake/CheckBuildRules.cmake).
add_library(rally_loop STATIC
    fixed_step_clock.cpp
    interpolation.cpp
)
target_include_directories(rally_loop PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rally_loop PRIVATE rally_warnings)

# Plataforma: ventana, render, entrada y audio sobre raylib (backend SDL).
# SDL se usa directamente solo para el intervalo de presentación y la frecuencia del monitor.
add_library(rally_platform STATIC
    fatal_error.cpp
    frame_pacer.cpp
    loop_demo.cpp
    window.cpp
)
target_include_directories(rally_platform PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rally_platform
    PUBLIC raylib rally_loop
    PRIVATE rally_warnings SDL3::SDL3 user32 winmm
)
