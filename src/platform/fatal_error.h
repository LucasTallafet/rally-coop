#pragma once

namespace rally::platform {

// Muestra un error fatal al jugador: lo escribe en stderr y abre un cuadro de
// mensaje de Windows, porque en Release el exe no tiene consola. Bloquea hasta
// que se cierra el cuadro. El mensaje va en UTF-8.
void show_fatal_error(const char* message);

} // namespace rally::platform
