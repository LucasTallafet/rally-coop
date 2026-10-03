# Biblioteca de simulación: enlaza Jolt y NUNCA raylib ni SDL (lo comprueba
# cmake/CheckBuildRules.cmake). Las definiciones JPH_* llegan solo desde Jolt (PUBLIC).
add_library(rally_sim STATIC
    check.cpp
    demo_world.cpp
    fp_guard.cpp
    input_smoothing.cpp
    jolt_runtime.cpp
    lap_timer.cpp
    pcg32.cpp
    sim_world.cpp
    test_zone.cpp
)
target_include_directories(rally_sim PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rally_sim
    PUBLIC Jolt rally_fp_strict
    PRIVATE rally_warnings
)
