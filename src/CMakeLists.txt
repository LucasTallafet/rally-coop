add_subdirectory(sim)
add_subdirectory(platform)
add_subdirectory(io)

add_executable(rally main.cpp)
target_link_libraries(rally PRIVATE rally_platform rally_io rally_sim rally_warnings)
# En Release, sin consola (subsistema WINDOWS) pero con main() como punto de entrada.
set_target_properties(rally PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
target_link_options(rally PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
