# Carril de la red (src/net): estado del coche, búfer del copiloto, red simulada y ENet de
# verdad por 127.0.0.1 en un solo proceso. Sin ventana ni mando. `enet` solo para los clientes
# crudos de los tests del protocolo 2 y del bit de recolocado (el encabezado de ENet trae
# windows.h).
rally_add_test_executable(rally_tests_net test_car_state.cpp test_state_buffer.cpp
    test_link.cpp test_host_world.cpp test_reset_bit.cpp test_reset_counter.cpp)
target_link_libraries(rally_tests_net PRIVATE rally_net enet)
target_compile_definitions(rally_tests_net PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(rally_tests_net PRIVATE ${PROJECT_SOURCE_DIR}/tests/sim/common)
