#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace rally::sim {

// Capa de irregularidad del trazado (tarea irregularidad, lote 7; docs/02-arquitectura.md §6.1):
// funciones de s, deterministas y calculadas al cargar, que mueven la calzada sobre la normal del
// esqueleto (StageAxis) sin tocarlo. Siete canales: desplazamiento lateral del centro de la calzada
// d(s), onda larga del semiancho de cada lado ω(s) (ancho variable), onda corta del borde exterior
// de cada arcén ε(s) (bordes irregulares), peralte añadido β(s) (tanto por uno) y ondulación
// vertical z(s). Cada canal es σ(s)·G(s)·W(s):
// - W, una suma de kStageIrregularityWaves ondas a·sin(k·s + φ) sembradas con Pcg32(semilla,
//   canal): λ log-uniforme, una en cada parte igual de [log λmin, log λmax]; φ uniforme; a ∝ λ,
//   con Σ a = la amplitud del archivo (el pico nunca pasa de ella, y las ondas cortas, que son
//   las que curvan, llevan menos: el presupuesto de curvatura de los límites de abajo);
// - σ, la escala por s del archivo: entre dos puntos, un paso suave (3u² − 2u³), no lineal, para
//   que d', z' y β' no salten en los puntos; constante antes del primero y tras el último;
// - G, el apagado en curvas (d, ω y ε): paso suave de la distancia en s al conjunto donde la
//   curvatura del esqueleto pasa de kStageIrregularityGateCurvature, en
//   kStageIrregularityGateLength. ε se apaga también (parte A, premisa P2): en las horquillas de R
//   6–10, entre el arcén interior y el centro de la curva quedan menos de 2 m, y la onda corta del
//   borde dejaba el talud sin sitio («no cabe el talud entre pasadas»).
// Sin reservas al evaluar: todo lo que se usa son vectores fijos desde la carga.

inline constexpr int kStageIrregularityWaves = 4;
// Más allá de esta curvatura (R 30) manda el esqueleto: d, ω y ε valen 0 (lote-7.md §8.2).
inline constexpr double kStageIrregularityGateCurvature = 1.0 / 30.0;
// Longitud del paso suave del apagado (m). 45 y no 30: con 30, G'' y G'·W' sumaban hasta 0,006 m⁻¹
// a d'' con 0,45 m de amplitud (revisión del plan, cambio 6).
inline constexpr double kStageIrregularityGateLength = 45.0;
// Límites de partida (docs/05, 2026-11-12; prompt de irregularidad, «Diseño de partida»).
inline constexpr double kStageAxisOffsetMaxCurvature = 0.01; // |d''| (m⁻¹): como un radio de 100 m
inline constexpr double kStageHeightMaxCurvature = 0.0033;   // |z''| (m⁻¹): 0,3 g a 30 m/s
inline constexpr double kStageBankMaxRate = 0.015;           // |β'| (1/m): 1,5 %/m
// La validación muestrea cada canal a λmin/16 y multiplica lo medido por esto antes de compararlo
// con el límite: es lo que ese paso puede subestimar el máximo de una onda (1 − cos(π/16) = 1,9 %).
inline constexpr double kStageIrregularityProbeMargin = 1.02;
inline constexpr double kStageIrregularityMinWavelength = 1.0; // λmin del archivo (m)
inline constexpr double kStageMinShoulderWidth = 0.5;          // arcén real, como poco (m)

enum class StageChannel : int {
    Axis,       // d: t del centro de la calzada
    WidthLeft,  // ω izquierda: se suma al semiancho izquierdo de la calzada
    WidthRight, // ω derecha
    EdgeLeft,   // ε izquierda: se suma al ancho del arcén izquierdo
    EdgeRight,  // ε derecha
    Bank,       // β: se suma al peralte (tanto por uno)
    Height,     // z: se suma a la altura (m)
};
inline constexpr int kStageChannelCount = 7;
// Los cinco primeros canales mueven los bordes del núcleo (lo que se lee en el paso).
inline constexpr int kStageEdgeChannelCount = 5;

// El apagado en curvas se aplica a todo lo que mueve los bordes: d, ω y ε.
constexpr bool stage_channel_gated(StageChannel c) {
    return c != StageChannel::Bank && c != StageChannel::Height;
}

// Una componente del archivo: amplitud (m, o tanto por uno en el peralte) y longitudes de onda.
struct StageIrregularityComponent {
    double amplitude = 0.0;
    double wavelength_min = 0.0;
    double wavelength_max = 0.0;
};

// La sección [irregularity] del formato 2, tal como se lee.
struct StageIrregularitySpec {
    bool present = false;
    std::uint64_t seed = 0;
    StageIrregularityComponent axis;   // d
    StageIrregularityComponent width;  // ω, cada lado con sus ondas
    StageIrregularityComponent edge;   // ε, cada lado con sus ondas
    StageIrregularityComponent bank;   // β, en tanto por uno
    StageIrregularityComponent height; // z
    struct ScalePoint {
        double s = 0.0;
        double value = 0.0;
    };
    std::vector<ScalePoint> scale; // vacío: 1 en todo el tramo
};

struct StageWave {
    double amplitude = 0.0;
    double k = 0.0; // 2π/λ
    double phase = 0.0;
};

// Valor y sus dos derivadas en s.
struct StageJet {
    double v = 0.0;
    double d1 = 0.0;
    double d2 = 0.0;
};

// Cotas de |valor|, |primera| y |segunda derivada| de un canal en un tramo de s.
struct StageChannelBound {
    double v = 0.0;
    double d1 = 0.0;
    double d2 = 0.0;
};

class StageAxis;

class StageIrregularity {
public:
    // Ondas, escala y apagado desde la sección leída y las primitivas del esqueleto. Reserva
    // memoria: al cargar. Sin sección, todo vale 0 y no hay ondas.
    static StageIrregularity build(const StageIrregularitySpec& spec, const StageAxis& axis);

    // El valor de los `count` primeros canales en s (en el orden de StageChannel; los demás, 0).
    // Sin reservas.
    std::array<double, kStageChannelCount> values(double s, int count = kStageChannelCount) const;
    // Valor y derivadas de un canal en s. Sin reservas.
    StageJet jet(StageChannel channel, double s) const;
    // Cotas analíticas en [s0, s1]: |(σGW)''| ≤ |σG|''·Σa + 2·|σG|'·Σa·k + |σG|·Σa·k², y así.
    StageChannelBound bound(StageChannel channel, double s0, double s1) const;
    const std::vector<StageWave>& waves(StageChannel channel) const {
        return waves_[static_cast<std::size_t>(channel)];
    }
    // El λmin del archivo de un canal (0 sin ondas): el paso de la validación.
    double wavelength_min(StageChannel channel) const {
        return wavelength_min_[static_cast<std::size_t>(channel)];
    }
    bool has_waves() const;
    // Intervalos de s donde |κ| del esqueleto pasa del umbral del apagado (ordenados, sin solapes).
    const std::vector<std::array<double, 2>>& gate_intervals() const { return gate_; }

private:
    StageJet scale_jet(double s) const;
    StageJet gate_jet(double s) const;
    StageChannelBound scale_bound(double s0, double s1) const;
    StageChannelBound gate_bound(double s0, double s1) const;

    std::array<std::vector<StageWave>, kStageChannelCount> waves_;
    std::array<double, kStageChannelCount> wavelength_min_{};
    std::vector<StageIrregularitySpec::ScalePoint> scale_;
    std::vector<std::array<double, 2>> gate_;
};

} // namespace rally::sim
