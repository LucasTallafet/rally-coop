#pragma once

namespace rally::platform {

class Overlay;
class TelemetryHistory;

// Gráficas de la telemetría (tecla T): los paneles de la página `page` de default_graph_pages(),
// uno debajo de otro, en su sitio del diseño de la pantalla (capa Graphs, a la izquierda del
// contador F1), con los últimos 10 s (el último paso a la derecha). Cada columna de píxeles es una
// línea vertical del mínimo al máximo de sus pasos (TelemetryHistory::column); si el panel es más
// estrecho que la historia, de la envolvente de las columnas que le tocan (merge_columns).
// Encima, el nombre de la página; encima de cada panel, su nombre y el valor actual de cada canal.
// Dibuja entre BeginDrawing y EndDrawing.
void draw_telemetry_graphs(const TelemetryHistory& history, int page, Overlay& overlay);

} // namespace rally::platform
