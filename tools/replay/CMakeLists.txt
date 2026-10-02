# rally_replay: programa de consola que solo enlaza la simulación y rally_io (sin raylib ni
# SDL; lo comprueba cmake/CheckBuildRules.cmake). Repite grabaciones, comprueba el hash y mide.
add_executable(rally_replay main.cpp)
target_link_libraries(rally_replay PRIVATE rally_io rally_sim rally_warnings)
