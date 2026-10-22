# Carril de la red (src/net): estado del coche, búfer del copiloto, red simulada y ENet de
# verdad por 127.0.0.1 en un solo proceso. Sin ventana ni mando. `enet` solo para el cliente
# crudo del test del protocolo 2 (el encabezado de ENet trae windows.h).
rally_add_test_executable(rally_tests_net test_car_state.cpp test_state_buffer.cpp
    test_link.cpp test_host_world.cpp)
target_link_libraries(rally_tests_net PRIVATE rally_net enet)
target_compile_definitions(rally_tests_net PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(rally_tests_net PRIVATE ${PROJECT_SOURCE_DIR}/tests/sim/common)
