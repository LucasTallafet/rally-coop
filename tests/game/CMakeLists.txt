# Carril de juego (src/game): máquina de acciones y, más adelante, sesión y copiloto.
rally_add_test_executable(rally_tests_game test_actions.cpp)
target_link_libraries(rally_tests_game PRIVATE rally_game)
target_compile_definitions(rally_tests_game PRIVATE
    RALLY_GAME_DATA_DIR="${PROJECT_SOURCE_DIR}/data/game")
target_include_directories(rally_tests_game PRIVATE ${PROJECT_SOURCE_DIR}/tests/sim/common)
