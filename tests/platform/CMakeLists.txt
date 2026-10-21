# Carril de la plataforma: bucle, entrada, cámara, interior, sonido, vibración, pantalla, teclas
# y ajustes del jugador. button_latch y mouse_steer usan SimWorld, pero miden la entrada de la
# plataforma. Los mundos de prueba del coche vienen de tests/sim/common (solo se leen).
# golden/: capturas de rally.exe --golden (tools/ci/golden.ps1; necesitan ventana, no van aquí).
rally_add_test_executable(rally_tests_platform
    test_action_driver.cpp
    test_button_latch.cpp
    test_camera_space.cpp
    test_car_feedback.cpp
    test_car_synth.cpp
    test_cockpit.cpp
    test_fixed_step_clock.cpp
    test_forest_layout.cpp
    test_frame_history.cpp
    test_head_motion.cpp
    test_input_shaping.cpp
    test_interpolation.cpp
    test_keys.cpp
    test_mouse_steer.cpp
    test_perf_stats.cpp
    test_player_settings.cpp
    test_screen_layout.cpp
    test_seat.cpp
    test_surface_feel.cpp
    test_ui_text.cpp
)
target_include_directories(rally_tests_platform PRIVATE ${PROJECT_SOURCE_DIR}/tests/sim/common)
