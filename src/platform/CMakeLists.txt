# Plataforma: ventana, render, entrada y audio sobre raylib (backend SDL).
add_library(rally_platform STATIC
    fatal_error.cpp
    window.cpp
)
target_include_directories(rally_platform PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(rally_platform
    PUBLIC raylib
    PRIVATE rally_warnings user32
)
