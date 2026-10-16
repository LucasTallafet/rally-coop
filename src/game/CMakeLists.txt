# Lógica del juego (tarea 2.9): máquina de acciones; más adelante, sesión y copiloto. Sin raylib
# ni SDL. Es estado del juego, no de la simulación: lee SimWorld y nunca lo escribe. Cuenta
# pasos, nunca lee el reloj (cmake/CheckSimRules.cmake lo comprueba).
add_library(rally_game STATIC
    actions.cpp
)
target_include_directories(rally_game PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rally_game PUBLIC rally_sim PRIVATE rally_warnings)
