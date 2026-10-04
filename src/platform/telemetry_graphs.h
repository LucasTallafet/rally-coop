#pragma once

namespace rally::platform {

class TelemetryHistory;

// Gráficas de la telemetría (tecla T): los paneles de default_graph_panels(), uno debajo de
// otro arriba, con su borde derecho en `right` (a la izquierda del contador F1 si está
// visible), con los últimos 10 s (el último paso a la derecha). Cada columna de píxeles es una
// línea vertical del mínimo al máximo de sus pasos, unida a la anterior. Encima de cada panel,
// su nombre y el valor actual de cada canal. Dibuja entre BeginDrawing y EndDrawing.
void draw_telemetry_graphs(const TelemetryHistory& history, int right);

} // namespace rally::platform
