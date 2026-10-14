#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace rally::platform {

// Lo que el sonido necesita del coche (tarea 2.7). El bucle principal la publica una vez por
// fotograma y el callback de audio (otro hilo) la lee sin bloquear con TripleBuffer. Los sucesos
// más cortos que un fotograma (cambio, soplido, impacto) van como contadores, como las
// pulsaciones en ButtonLatch: el sintetizador suena una vez por cada incremento.
struct WheelSound {
    bool contact = false;
    std::int8_t surface = -1; // sim::Surface, -1 en el aire
    float rho = 0.0f;         // deslizamiento normalizado (platform/slip)
    float slip_speed = 0.0f;  // m/s: |(slip_x, slip_y)|
};

struct AudioSnapshot {
    // Cambia al reconstruir el mundo: el sintetizador reinicia el suavizado y toma los contadores
    // como base, sin sucesos.
    std::uint32_t generation = 0;
    bool active = false; // hay coche (falso antes del primer fotograma)
    bool paused = false;
    bool blind = false;
    // Del coche (CarData).
    int cylinders = 4;
    float idle_rpm = 1000.0f;
    float limiter_rpm = 8000.0f;
    float limiter_ramp_rpm = 100.0f;
    float boost_max_bar = 0.0f;
    // Del último paso.
    float rpm = 0.0f;
    float engine_throttle = 0.0f; // acelerador efectivo (ralentí y corte del cambio incluidos)
    float torque_load = 0.0f;     // par del motor / par pleno a esas rpm (engine_torque_load)
    float throttle = 0.0f;        // pedal aplicado (el rebote del limitador)
    float boost_bar = 0.0f;
    float speed = 0.0f; // m/s
    std::array<WheelSound, 4> wheels{};
    // Sucesos, acumulados desde que se construyó el mundo.
    std::uint32_t shifts = 0;
    std::uint32_t blowoffs = 0;
    float blowoff_bar = 0.0f; // presión del último soplido
    std::uint32_t impacts = 0;
    float impact_peak = 0.0f; // m/s del último impacto
    // Para el test del triple búfer: el bucle no lo usa.
    std::uint64_t sequence = 0;
};

// Triple búfer de un productor y un consumidor: publish() escribe en su hueco y lo intercambia
// con el del medio; read() toma el del medio si hay uno nuevo. Los dos solo intercambian un
// índice atómico, así que nunca esperan ni ven un valor a medias, y no reservan memoria.
template <class T>
class TripleBuffer {
    static_assert(std::is_trivially_copyable_v<T>);

public:
    // Solo el productor.
    void publish(const T& value) {
        slots_[write_] = value;
        const std::uint8_t old =
            middle_.exchange(static_cast<std::uint8_t>(write_ | kFresh), std::memory_order_acq_rel);
        write_ = old & kIndex;
    }
    // Solo el consumidor. La referencia vale hasta su siguiente read().
    const T& read() {
        if ((middle_.load(std::memory_order_acquire) & kFresh) != 0) {
            const std::uint8_t old =
                middle_.exchange(static_cast<std::uint8_t>(read_), std::memory_order_acq_rel);
            read_ = old & kIndex;
        }
        return slots_[read_];
    }

private:
    static constexpr std::uint8_t kIndex = 3;
    static constexpr std::uint8_t kFresh = 4;
    std::array<T, 3> slots_{};
    std::uint8_t write_ = 0; // del productor
    std::uint8_t read_ = 1;  // del consumidor
    std::atomic<std::uint8_t> middle_{2};
};

} // namespace rally::platform
