#pragma once

#include "sim/telemetry.h"

#include <cstdint>
#include <span>
#include <vector>

namespace rally::platform {

// Historia de la telemetría para las gráficas en pantalla (tecla T): los últimos 10 s de
// pasos en un búfer circular reservado al construir. La partida mete cada paso aunque las
// gráficas estén ocultas, para que al pulsar T ya estén los últimos 10 s. Sin raylib: se
// prueba sin ventana.
inline constexpr int kTelemetryHistorySeconds = 10;
inline constexpr int kTelemetryHistorySteps = kTelemetryHistorySeconds * 240;

class TelemetryHistory {
public:
    explicit TelemetryHistory(int capacity = kTelemetryHistorySteps);

    void clear();
    // Sin reservas: si está lleno, sustituye el paso más antiguo.
    void push(const sim::CarTelemetry& telemetry);

    int size() const { return size_; }
    int capacity() const { return static_cast<int>(samples_.size()); }
    // Paso i, de 0 (el más antiguo) a size() - 1 (el último).
    const sim::CarTelemetry& at(int i) const;

    using Channel = float (*)(const sim::CarTelemetry&);
    struct Range {
        float min = 0.0f;
        float max = 0.0f;
        bool valid = false; // false si la columna aún no tiene pasos
    };
    // Mínimo y máximo de cada canal en cada una de `columns` columnas que reparten la
    // capacidad entera (10 s), con el último paso en la última columna. Cada columna incluye
    // además el último paso de la anterior, para que la curva quede unida. out[c * n + k] es
    // la columna c del canal k (n = channels.size()). Una sola pasada por los pasos: cada uno
    // se lee una vez para todos los canales.
    void column_ranges(std::span<const Channel> channels, int columns, std::span<Range> out) const;

private:
    std::vector<sim::CarTelemetry> samples_;
    int head_ = 0; // dónde va el siguiente
    int size_ = 0;
};

// Canales y paneles de las gráficas. Un canal nuevo (deslizamiento por rueda, RPM, presión
// del turbo) es una fila más en la tabla de su panel (telemetry_history.cpp); un panel nuevo,
// otra en la de paneles. El dibujo (telemetry_graphs) no cambia.
struct GraphChannel {
    const char* name;
    float (*value)(const sim::CarTelemetry&); // en la unidad del panel
    std::uint32_t rgba;                       // color 0xRRGGBBAA
};

struct GraphPanel {
    const char* title;
    const char* unit;
    float min; // rango vertical; lo que se sale se recorta
    float max;
    std::span<const GraphChannel> channels;
};

std::span<const GraphPanel> default_graph_panels();
// Canales que dibujan las gráficas como mucho, sumando todos los paneles (el dibujo reserva su
// sitio sin memoria dinámica).
inline constexpr int kMaxGraphChannels = 16;

// β en grados para las gráficas: 0 por debajo de kGraphBetaMinSpeed de velocidad horizontal,
// donde no está definido (parado salta entre ±180°).
inline constexpr float kGraphBetaMinSpeed = 3.0f; // m/s
float graph_beta_deg(const sim::CarTelemetry& telemetry);

// Interruptor de las gráficas (T). En el modo a ciegas T no hace nada y las gráficas no se
// ven: la prueba a ciegas es por el manejo, y los picos de β o de aceleración lateral darían
// una pista numérica de la superficie.
class GraphToggle {
public:
    void press(bool blind_mode) {
        if (!blind_mode) {
            on_ = !on_;
        }
    }
    bool visible(bool blind_mode) const { return on_ && !blind_mode; }

private:
    bool on_ = false;
};

} // namespace rally::platform
