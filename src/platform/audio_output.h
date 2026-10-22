#pragma once

#include "platform/audio_snapshot.h"
#include "platform/car_synth.h"

#include <cstdint>

namespace rally::platform {

// Salida del sonido del coche (tarea 2.7) con el audio de raylib (miniaudio): un AudioStream mono
// en float a 48 kHz con callback. En raylib 6.0 el callback de un stream se llama desde el
// callback del dispositivo (otro hilo), con las muestras que pide la mezcla en ese momento: la
// latencia es la del periodo del dispositivo (10 ms por defecto en raylib), no la del búfer del
// stream. El callback no recibe un puntero de usuario: el sintetizador y la instantánea son
// estáticos de este módulo. Solo puede haber una AudioOutput.
//
// El bucle principal publica la instantánea una vez por fotograma (TripleBuffer: el callback no
// espera nunca) y cambia el volumen con una ganancia atómica del sintetizador, sin llamar a raylib
// (raylib toma su cerrojo de audio en cada llamada).

// Medidas del callback desde que se abrió el audio (o desde reset_stats).
struct AudioStats {
    bool ready = false;
    std::int64_t calls = 0;
    std::int64_t frames = 0;
    double cpu_fraction = 0.0; // tiempo dentro del callback / tiempo transcurrido
    double max_call_ms = 0.0;  // la llamada más larga
    double max_gap_ms = 0.0;   // el mayor intervalo entre dos llamadas seguidas
    std::int64_t gaps = 0;     // intervalos > kAudioGapFactor × periodo: cortes probables
    double elapsed_s = 0.0;
};

inline constexpr int kAudioSampleRate = 48000;
inline constexpr double kAudioDevicePeriodMs = 10.0; // el de raylib por defecto
inline constexpr double kAudioGapFactor = 1.5;

class AudioOutput {
public:
    // Abre el dispositivo y el stream (necesita la ventana abierta). Si no hay dispositivo de
    // audio, el juego sigue sin sonido (ready() falso).
    AudioOutput();
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    bool ready() const { return ready_; }
    void publish(const AudioSnapshot& snapshot);
    void set_volume(float volume);
    // Modelo del sonido del deslizamiento (tecla 6, A/B del lote 3).
    void set_skid_model(SkidModel model);
    SkidModel skid_model() const;
    AudioStats stats() const;
    void reset_stats();

private:
    bool ready_ = false;
};

} // namespace rally::platform
