# Red (fase 3, lote 3): estado del coche por ENet, host y copiloto. Sin raylib ni SDL; la enlaza
# la plataforma. ENet (y con él winsock2.h y windows.h) solo entra en los .cpp: ningún
# encabezado público de src/net lo incluye, porque chocan con raylib y con std::min/max.
# No lee el reloj: el tiempo llega como argumento (now_s). Lo comprueba cmake/CheckSimRules.cmake.
add_library(rally_net STATIC
    car_state.cpp
    copilot.cpp
    enet_runtime.cpp
    host.cpp
    protocol.cpp
    state_buffer.cpp
)
target_include_directories(rally_net PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rally_net PUBLIC rally_sim PRIVATE enet rally_warnings)
target_compile_definitions(rally_net PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
