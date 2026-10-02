#pragma once

#include <cstdint>

namespace rally::platform {

enum class FrameTarget {
    MonitorRate, // un fotograma por refresco del monitor (por defecto)
    Fps30,       // modo de 30 fps para equipos justos
};

// Ritmo de fotogramas sin espera activa. Necesita la ventana de raylib ya abierta.
//
// - MonitorRate con sincronización vertical: intervalo de presentación 1.
// - Fps30 con sincronización vertical, si la frecuencia es múltiplo de 30 (60, 120, 240 Hz):
//   intervalo frecuencia / 30, y el ritmo lo marca la gráfica. Si no lo es (144, 165 Hz), o
//   el controlador no respeta el intervalo: intervalo 1 y un temporizador de Windows que
//   duerme hasta el plazo de cada fotograma; en 144 Hz los fotogramas duran 4 y 5 refrescos.
// - Sin sincronización vertical: intervalo 0; Fps30 sigue durmiendo con el temporizador.
class FramePacer {
public:
    FramePacer();
    ~FramePacer();
    FramePacer(const FramePacer&) = delete;
    FramePacer& operator=(const FramePacer&) = delete;

    // Al principio de cada fotograma: revisa la frecuencia del monitor, duerme si toca y
    // devuelve la duración real del fotograma anterior en nanosegundos (0 la primera vez).
    std::int64_t begin_frame();
    // Lo que ha durado la espera del temporizador en el último begin_frame (0 sin él), en ns.
    // Para el contador de rendimiento: cuenta como espera de presentación.
    std::int64_t last_wait_ns() const { return last_wait_ns_; }

    void set_target(FrameTarget target);
    FrameTarget target() const { return target_; }
    void set_vsync(bool vsync);
    bool vsync() const { return vsync_; }

    // Para el contador en pantalla.
    float refresh_rate() const { return refresh_rate_; }
    int swap_interval() const { return swap_interval_; }
    bool uses_timer() const { return use_timer_; }
    bool high_resolution_timer() const { return high_resolution_timer_; }

    // Periodo de un refresco del monitor, en unidades de FixedStepClock (ns × 240).
    std::int64_t refresh_period() const { return refresh_period_; }
    // Periodo al que se ajusta el dt (FixedStepClock::set_snap_period): el de un refresco
    // si hay sincronización vertical (también en el modo de 30: sus fotogramas duran un
    // número entero de refrescos); 0 sin ella.
    std::int64_t snap_period() const { return vsync_ ? refresh_period_ : 0; }
    // Duración prevista de un fotograma, en unidades del reloj: refresco × intervalo, o
    // 1/30 s con el temporizador; 0 si no se conoce (sin sincronización ni temporizador).
    std::int64_t frame_period() const;

private:
    void apply();
    void sleep_until(std::int64_t deadline_ns);
    void check_swap_interval(std::int64_t frame_ns);

    FrameTarget target_ = FrameTarget::MonitorRate;
    bool vsync_ = true;
    float refresh_rate_ = 0.0f;
    std::int64_t refresh_period_ = 0; // en unidades del reloj

    int swap_interval_ = 1;
    bool use_timer_ = false;
    std::int64_t deadline_ns_ = 0; // plazo del próximo fotograma con temporizador

    // Comprobación de que el controlador respeta un intervalo > 1: si los fotogramas
    // salen más cortos de lo esperado, se pasa al temporizador hasta el próximo cambio.
    bool interval_ignored_ = false;
    bool interval_checked_ = false;
    int interval_frames_ = 0;
    std::int64_t interval_ns_ = 0;

    void* timer_ = nullptr; // HANDLE del temporizador de Windows
    bool high_resolution_timer_ = false;
    bool raised_timer_resolution_ = false; // timeBeginPeriod(1) activo

    std::int64_t last_frame_start_ns_ = -1;
    std::int64_t last_wait_ns_ = 0;
};

} // namespace rally::platform
