#pragma once

#include "sim/telemetry.h"

#include <cstdint>
#include <span>
#include <vector>

namespace rally::platform {

// Historia de la telemetría para las gráficas en pantalla (tecla T): los últimos 10 s. La
// partida mete cada paso aunque las gráficas estén ocultas, para que al pulsar T ya estén los
// últimos 10 s. Sin raylib: se prueba sin ventana.
//
// No guarda los pasos: al meter cada uno evalúa todos los canales de todas las páginas y
// actualiza el mínimo y el máximo de su columna. Cada columna cubre un número fijo de pasos,
// alineado con el índice de paso, así que el dibujo de cada fotograma no recorre nada: solo
// lee las columnas. Todo se reserva al construir.
inline constexpr int kTelemetryHistorySeconds = 10;
inline constexpr int kGraphStepsPerColumn = 5;
inline constexpr int kGraphColumns = kTelemetryHistorySeconds * 240 / kGraphStepsPerColumn; // 480

class TelemetryHistory {
public:
    using Channel = float (*)(const sim::CarTelemetry&);
    struct Range {
        float min = 0.0f;
        float max = 0.0f;
        bool valid = false; // false si la columna aún no tiene pasos
    };

    // Por defecto, los canales de default_graph_pages() en 480 columnas de 5 pasos.
    TelemetryHistory();
    TelemetryHistory(std::span<const Channel> channels, int columns, int steps_per_column);

    void clear();
    // Sin reservas: evalúa los canales y actualiza la columna del paso.
    void push(const sim::CarTelemetry& telemetry);

    std::int64_t steps() const { return steps_; } // pasos metidos desde clear()
    int columns() const { return columns_; }
    int channel_count() const { return static_cast<int>(channels_.size()); }
    // Último valor del canal (0 sin pasos).
    float latest(int channel) const;
    // Columna c del canal: 0 la más antigua visible, columns() - 1 la del último paso (que
    // puede estar a medias). La columna de los pasos [n·s, (n+1)·s) incluye además el paso
    // n·s − 1, para que la curva quede unida a la anterior.
    Range column(int channel, int c) const;
    // Las columns() columnas del canal en orden (out[c] = column(channel, c)), sin una división
    // por columna: es lo que lee el dibujo en cada fotograma.
    void visible_columns(int channel, std::span<Range> out) const;

private:
    std::vector<Channel> channels_;
    int columns_;
    int steps_per_column_;
    std::vector<Range> ranges_; // [canal · columns_ + columna absoluta % columns_]
    std::vector<float> latest_;
    std::int64_t steps_ = 0;
};

// Junta las columnas de `in` en las out.size() de `out` (out.size() ≤ in.size()), para dibujar
// las gráficas en un panel más estrecho que la historia: la columna i cubre
// in[i·n/m, (i+1)·n/m), con el mínimo de los mínimos y el máximo de los máximos de las válidas.
// La envolvente sigue siendo exacta: un pico de un paso no se pierde.
void merge_columns(std::span<const TelemetryHistory::Range> in,
                   std::span<TelemetryHistory::Range> out);

// Canales, paneles y páginas de las gráficas. Un canal nuevo (deslizamiento por rueda, RPM,
// presión del turbo) es una fila más en la tabla de su panel (telemetry_history.cpp); un panel
// nuevo, otra en la de su página. El dibujo (telemetry_graphs) y la historia no cambian.
struct GraphChannel {
    const char* name;
    TelemetryHistory::Channel value; // en la unidad del panel
    std::uint32_t rgba;              // color 0xRRGGBBAA
};

struct GraphPanel {
    const char* title;
    const char* unit;
    float min; // rango vertical; lo que se sale se recorta
    float max;
    std::span<const GraphChannel> channels;
};

struct GraphPage {
    const char* title;
    std::span<const GraphPanel> panels; // vacía hasta que una tarea le pone canales
};

// 1 · Manejo, 2 · Tren motriz (2.5 y 2.6) y 3 · Suspensión (2.4).
std::span<const GraphPage> default_graph_pages();
// Todos los canales de las páginas por defecto, en orden de página, panel y canal: el índice
// de cada canal en la historia.
std::span<const TelemetryHistory::Channel> default_graph_channels();
// Índice en default_graph_channels() del primer canal de una página.
int first_graph_channel(int page);
// Canales que caben en total (el dibujo reserva su sitio sin memoria dinámica).
inline constexpr int kMaxGraphChannels = 32;

// β en grados para las gráficas: 0 por debajo de kGraphBetaMinSpeed de velocidad horizontal,
// donde no está definido (parado salta entre ±180°).
inline constexpr float kGraphBetaMinSpeed = 3.0f; // m/s
float graph_beta_deg(const sim::CarTelemetry& telemetry);

// Páginas de las gráficas (T): cada pulsación pasa a la siguiente página con paneles y,
// después de la última, las oculta. Las páginas sin paneles se saltan. En el modo a ciegas T no
// hace nada y las gráficas no se ven: la prueba a ciegas es por el manejo, y los picos de β o
// de aceleración lateral darían una pista numérica de la superficie.
class GraphPager {
public:
    void press(bool blind_mode, std::span<const GraphPage> pages);
    // Página visible (índice en pages) o -1 si están ocultas.
    int visible(bool blind_mode) const { return blind_mode ? -1 : page_; }

private:
    int page_ = -1;
};

} // namespace rally::platform
