# Biblioteca de simulación: enlaza Jolt y NUNCA raylib ni SDL (lo comprueba
# cmake/CheckBuildRules.cmake). Las definiciones JPH_* llegan solo desde Jolt (PUBLIC).
add_library(rally_sim STATIC
    fp_guard.cpp
    jolt_runtime.cpp
    pcg32.cpp
)
target_include_directories(rally_sim PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rally_sim
    PUBLIC Jolt rally_fp_strict
    PRIVATE rally_warnings
)
