#pragma once

#include "sim/stage_axis.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace rally::sim {

// Seguimiento del coche en el tramo (mensajes/lote-4.md §2.3 y §2.4; docs/02-arquitectura.md §5):
// `s` y `t` sobre el eje de un StageAxis con sus prolongaciones rectas, el índice espacial para
// adquirir la posición, la validación de la geometría al cargar y la máquina de la pasada
// (recolocación, atajos, parciales y meta). Sin Jolt: recibe posiciones en planta y devuelve
// decisiones. Todo se calcula al cargar; nada de lo que se llama en el paso reserva memoria.

// Constantes del tramo (§2.3). Provisionales hasta la fase 3, salvo el margen de atajo (`05`).
inline constexpr double kStageEntryLength = 20.0;    // prolongación recta antes de s = 0 (m)
inline constexpr double kStageRunoffLength = 150.0;  // prolongación recta tras la meta (m)
inline constexpr double kStageShoulderWidth = 1.0;   // arcén a cada lado (m)
inline constexpr double kStageStripHalfWidth = 60.0; // franja con colisión, en planta (m)
inline constexpr double kShortcutMargin = 30.0;      // atajo: ganancia > recorrido fuera + esto
inline constexpr double kSplitSpacing = 1000.0;      // un parcial en cada múltiplo de esto…
inline constexpr double kSplitFinishGap = 200.0;     // …salvo a menos de esto de la meta
inline constexpr double kRunStartDistance = 0.10;    // la pasada empieza al pasar esta s (m)
inline constexpr int kMaxSplits = 64;

struct StageProjection {
    double s = 0.0;        // en [s_begin(), s_end()]
    double t = 0.0;        // lateral en planta, + a la derecha del sentido de avance
    double distance = 0.0; // en planta, al punto del eje: |t| salvo más allá de los extremos
    bool on_road = false;  // pie perpendicular y |t| ≤ semiancho de ese lado + arcén
};

// StageAxis + prolongaciones + índice espacial + validación; todo al cargar.
class StageTrack {
public:
    // Lee el .stage (StageAxis::parse) y comprueba la geometría (§2.6 «Validación al cargar»).
    // Error «línea N: …» o, en la geometría, «s = …: …»; con error deja `out` como estaba.
    static StageParseResult build(std::string_view text, StageTrack& out);

    const StageAxis& axis() const { return axis_; }
    double s_begin() const { return -kStageEntryLength; }
    double s_end() const { return axis_.length() + kStageRunoffLength; }
    // Con prolongaciones; s acotada a [s_begin, s_end] (NaN da s_begin). Sin reservas.
    StagePose pose(double s) const;
    // Semiancho + arcén; side −1 izquierda, +1 derecha.
    double core_half_width(double s, int side) const;
    // La proyección más cercana de (x, z) con s en [s_lo, s_hi] (acotada al tramo): exacta en
    // rectas y arcos, Newton en clotoides. Sin reservas.
    StageProjection project(double x, double z, double s_lo, double s_hi) const;
    // La proyección más cercana en todo el tramo, con el índice (celdas de 10 m). Sin reservas.
    StageProjection acquire(double x, double z) const;
    // Si (x, z) está en la calzada o el arcén de alguna pasada, su proyección en ella. Con
    // `margin` > 0, también hasta `margin` fuera del arcén (el microrrelieve del tramo, que mezcla
    // en esa franja; on_road de `out` dice entonces si está dentro del margen). Sin reservas.
    bool on_any_road(double x, double z, StageProjection& out, double margin = 0.0) const;
    // Memoria de las piezas, las muestras y el índice (bytes).
    std::size_t index_bytes() const;

private:
    // Tramo del eje entre s0 y s1 con una sola fórmula: la prolongación de entrada, cada
    // primitiva de StageAxis y la de frenada.
    struct Piece {
        enum class Kind : std::uint8_t { Straight, Arc, Spiral };
        Kind kind = Kind::Straight;
        double s0 = 0.0;
        double s1 = 0.0;
        double x0 = 0.0, z0 = 0.0, heading0 = 0.0; // punto y rumbo de inicio
        double radius = 0.0;                       // arcos: R con signo
        double cx = 0.0, cz = 0.0;                 // arcos: centro
        double curvature0 = 0.0, curvature1 = 0.0; // curvatura lineal de s0 a s1
        std::uint32_t sample_first = 0;            // primera muestra (la de s0)
        std::uint32_t sample_count = 0;            // intervalos (muestras = sample_count + 1)
    };
    struct Interval {
        double s0 = 0.0;
        double s1 = 0.0;
    };
    struct Cell {
        std::int64_t key = 0; // (iz << 32) | (ix & 0xffffffff)
        std::uint32_t far_first = 0, far_count = 0;
        std::uint32_t near_first = 0, near_count = 0;
    };
    struct Candidate {
        double s = 0.0;
        double d2 = 0.0;
    };

    // Llama a f(Candidate) con cada mínimo local de la distancia en [lo, hi] (y los extremos de
    // las piezas que no tienen pie dentro), en orden de s.
    template <typename F>
    void for_each_candidate(double x, double z, double lo, double hi, F&& f) const;
    // Proyección de (x, z) en el punto s del eje: t, distancia y en carretera.
    StageProjection finish(double x, double z, double s, double margin = 0.0) const;
    const Cell* find_cell(double x, double z) const;
    // Tras parse: piezas, muestras, validación e índice. Error «s = …: …» o vacío.
    std::string prepare();

    StageAxis axis_;
    std::vector<Piece> pieces_;
    std::vector<double> sample_s_; // muestras de todo [s_begin, s_end] a ≤ 0,5 m
    std::vector<double> sample_x_;
    std::vector<double> sample_z_;
    std::vector<Cell> cells_;         // ordenadas por key
    std::vector<Interval> intervals_; // de las celdas, lejanos y cercanos
    double core_max_ = 0.0;           // el mayor semiancho + arcén del tramo
};

// Cómo se ha recolocado el coche (placed).
enum class StagePlace : std::uint8_t {
    Start,    // al construir: reset_s = s
    Button,   // botón de recolocar (en reset_s): resets + 1
    Shortcut, // tras un atajo (en exit_s): ya contado en step()
};

// Estado de la pasada. Trivial y sin relleno salvo 3 bytes tras on_road: se compara y se hashea
// campo a campo. Los campos de §2.4, ordenados por tamaño.
struct StageRunState {
    double s, t;           // del último paso
    double reset_s;        // punto de recolocación: el más avanzado alcanzado en carretera
    double exit_s;         // s al salir de la carretera (la del último paso en ella)
    double off_travel;     // recorrido en planta fuera de la carretera desde exit_s
    double last_x, last_z; // posición del último paso (o de placed)
    std::int64_t steps;    // pasos desde la construcción
    std::int64_t start_step, finish_step; // −1 si no han pasado
    std::int32_t resets, shortcuts;
    std::int32_t split_count; // parciales pasados (≤ kMaxSplits)
    bool on_road;
    std::int64_t split_steps[kMaxSplits]; // paso de cada parcial
};
static_assert(std::is_trivial_v<StageRunState> && std::is_standard_layout_v<StageRunState>);
static_assert(sizeof(StageRunState) == 7 * 8 + 3 * 8 + 3 * 4 + 4 + kMaxSplits * 8);

class Fnv1a64;
// Añade el estado de la pasada a un hash campo a campo, en el orden de la estructura y sin el
// relleno: los double por sus bits, los enteros en little-endian, on_road como un byte y las
// kMaxSplits entradas de split_steps. Es lo que SimWorld::state_hash() pone tras ControlState.
void hash_stage_run(Fnv1a64& hash, const StageRunState& state);

// La pasada por el tramo: sin Jolt, posiciones de entrada y decisiones de salida. Cuenta pasos:
// no lee ningún reloj. La penalización de tiempo no es de la simulación.
class StageRun {
public:
    explicit StageRun(const StageTrack& track);

    struct Step {
        bool shortcut = false; // atajo: recolocar en reset_s (= exit_s)
        double reset_s = 0.0;  // sin atajo: el punto de recolocación actual
    };
    // Cuenta un paso de física (`steps`). Es el único sitio que cuenta: SimWorld lo llama en
    // cada paso, antes de step() o, con el botón de recolocar, de placed(Button), así que
    // `steps` son los pasos desde la construcción y start_step, parciales y meta se miden con
    // él (respuesta del consultor a tramo-fisica, §3).
    void begin_step() { ++state_.steps; }
    // Tras cada paso de física (y su begin_step), con el origen del coche en planta (la altura
    // no cuenta). Tras un Step{true, …}, quien llama recoloca el coche en exit_s y llama a
    // placed(Shortcut, …) antes del siguiente step: si no, cada paso sobre la otra pasada
    // contaría otro atajo.
    Step step(double x, double z);
    // Tras recolocar (botón o atajo) o al construir: s, t = 0, en carretera, last_x/z = (x, z).
    // No cuenta pasos. Start solo antes del primer paso (RALLY_CHECK).
    void placed(StagePlace why, double s, double x, double z);
    const StageRunState& state() const { return state_; }

private:
    const StageTrack* track_;
    StageRunState state_;
};

} // namespace rally::sim
