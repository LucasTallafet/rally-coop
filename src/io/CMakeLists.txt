# Entrada y salida sin raylib ni SDL: formato de las grabaciones (.rrec), su repetición y el
# guardado de emergencia (y en la 2.3, la telemetría). La enlazan el juego, rally_replay y
# los tests. Puede usar archivos; nada de aquí lee el reloj ni se llama dentro de step()
# salvo Recorder::append, que no reserva memoria.

# Commit del build: se recalcula en cada compilación, no solo al configurar.
set(_build_info_cpp ${CMAKE_CURRENT_BINARY_DIR}/build_info.cpp)
set(_git_info_cmd ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DOUTPUT=${_build_info_cpp} -P ${PROJECT_SOURCE_DIR}/cmake/GitInfo.cmake)
execute_process(COMMAND ${_git_info_cmd})
add_custom_target(rally_build_info
    COMMAND ${_git_info_cmd}
    BYPRODUCTS ${_build_info_cpp}
    COMMENT "Commit del build"
    VERBATIM)

add_library(rally_io STATIC
    crash_save.cpp
    recording.cpp
    replay.cpp
    ${_build_info_cpp}
)
add_dependencies(rally_io rally_build_info)
target_include_directories(rally_io PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rally_io PUBLIC rally_sim PRIVATE rally_warnings)
