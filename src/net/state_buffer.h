#pragma once

// Búfer de estados del copiloto (interpolación por número de paso) y red simulada. Sin ENet,
// sin reloj: el tiempo llega como argumento. push, sample y stats no reservan memoria.

#include "net/car_state.h"
#include "sim/pcg32.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace rally::net {

struct Pose {
    double x = 0, y = 0, z = 0;
    double qx = 0, qy = 0, qz = 0, qw = 1;
};

// Lo demás del estado a la hora del objetivo (salpicadero y volante del copiloto): la dirección
// interpolada, el resto del estado más cercano. Con un recolocado en medio, todo del estado que
// se está mostrando (no se mezcla ni se adelanta).
struct SampledState {
    float steer = 0;
    float boost_bar = 0;
    std::int8_t gear = 0;
    float rpm = 0;
    std::array<std::int8_t, 4> surface{{-1, -1, -1, -1}};
    std::array<float, 4> compression{};
};

struct BufferStats {
    std::uint64_t received = 0; // estados aceptados
    std::uint64_t missing = 0;  // pasos de cadencia (paso / 8) que el host no mandó o se perdieron
    std::uint64_t late = 0;     // llegados con un paso menor que el máximo visto
    // Edad del último estado respecto al paso estimado del host (s). Sano: < 0,1 s.
    double delay_s = 0.0;
    bool extrapolating = false;  // el objetivo está más allá del último estado
    std::uint32_t host_step = 0; // paso estimado del host ahora
};

// Anillo de tamaño fijo, ordenado por paso. Sin reset(generación) descarta todo: 0 es una
// generación real tras la vuelta de 255.
class StateBuffer {
public:
    static constexpr int kCapacity = 128;

    // Mundo nuevo: vacía el búfer y solo acepta estados de esa generación.
    void reset(std::uint8_t generation);
    // Descarta duplicados, de otra generación y anteriores al más viejo con el anillo lleno.
    void push(const CarState& state, double arrival_s);
    // false si aún no hay ningún estado.
    bool sample(double now_s, Pose& out) const;
    bool sample(double now_s, Pose& out, SampledState& extra) const;
    BufferStats stats(double now_s) const;
    // Paso del estado más nuevo (0 si no hay).
    std::uint32_t newest_step() const;
    int size() const { return count_; }

    // Paso objetivo (con decimales) para `now_s`; solo para tests y diagnóstico.
    double target_step(double now_s) const;

private:
    struct Entry {
        CarState state;
        double arrival = 0.0;
    };
    double host_step_estimate(double now_s) const;

    std::array<Entry, kCapacity> ring_{};
    int count_ = 0;
    bool active_ = false;
    std::uint8_t generation_ = 0;
    std::uint64_t received_ = 0;
    std::uint64_t missing_ = 0;
    std::uint64_t late_ = 0;
    std::int64_t max_index_ = -1;
    std::uint32_t max_step_ = 0;
};

// Red simulada (tecla Z del copiloto): retiene cada estado hasta llegada + delay ± jitter y
// descarta una fracción `loss`. Solo afecta al canal no fiable.
struct Impairment {
    double delay_s = 0, jitter_s = 0, loss = 0;
};

class LinkSimulator {
public:
    static constexpr int kCapacity = 256;

    void set(const Impairment& impairment, std::uint64_t seed);
    void clear();
    // Recibe un estado en `now_s`: lo descarta (pérdida o anillo lleno) o lo retiene.
    void submit(const CarState& state, double now_s);
    // Saca el retenido con menor hora de salida si ya ha llegado; false si no hay ninguno.
    bool pop_ready(double now_s, CarState& out, double& release_s);
    std::uint64_t dropped() const { return dropped_; }

private:
    struct Slot {
        bool used = false;
        double release = 0.0;
        CarState state;
    };
    std::array<Slot, kCapacity> slots_{};
    Impairment impairment_{};
    sim::Pcg32 rng_{1, 1};
    std::uint64_t dropped_ = 0;
};

} // namespace rally::net
