#pragma once

#include "platform/copilot_args.h"

namespace rally::platform {

// rally.exe --copiloto (lote 3, §2.5–§2.8): la segunda instancia. Abre una ventana de 640 × 360,
// siempre encima y en la esquina inferior derecha del monitor, se conecta al host (por defecto
// 127.0.0.1:29470), construye el mundo que le manda (el mismo código que el host, sin llamar nunca
// a step) y dibuja el coche del conductor en la pose interpolada de net::StateBuffer::sample, con
// la cámara del asiento derecho (la de arranque; con el interior: el volante gira con la dirección
// recibida y la pantalla marca la marcha, las rpm y la presión), la de persecución o la de toda la
// zona (Tab). B mantenida gira la cabeza al salpicadero. No simula, no graba, no suena y no
// escribe ajustes.txt. Teclas: Z (red simulada), B, Tab, F1, F3, H, F11 y Esc. Sin conexión
// reintenta cada segundo y se reconecta solo cuando vuelve el host. Devuelve el código de salida.
int run_copilot(const CopilotArgs& args);

} // namespace rally::platform
