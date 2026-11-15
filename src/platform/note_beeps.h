#pragma once

#include "platform/audio_snapshot.h"

#include <array>
#include <cstdint>

namespace rally::platform {

// Pitidos de las notas del copiloto (lote 7, tarea panel; docs/01 §5.4: «pitidos, de momento»):
// uno corto por el canal de su lado al entrar una nota en el hueco izquierdo del panel, uno por los
// dos canales con otro tono para un peligro sin curva y un tercero, descendente, para una nota
// borrada. Sintético y sin archivos, por el mismo camino que el sonido del coche: la instantánea
// lleva contadores de sucesos (platform/audio_snapshot) y suena una voz por cada incremento. Lo
// llama el callback de audio (otro hilo): sin reservas, sin cerrojos, estado de tamaño fijo. No
// depende del modo a ciegas (los pitidos no delatan la superficie); en pausa callan.
//
// Todos los valores son de partida (criterio de Lucas §1): se afinan jugando.
inline constexpr float kBeepNoteHz = 880.0f;
inline constexpr float kBeepNoteSeconds = 0.070f;
inline constexpr float kBeepHazardHz = 660.0f;
inline constexpr float kBeepHazardSeconds = 0.090f;
inline constexpr float kBeepEraseStartHz = 330.0f; // baja hasta kBeepEraseEndHz
inline constexpr float kBeepEraseEndHz = 220.0f;
inline constexpr float kBeepEraseSeconds = 0.140f;
inline constexpr float kBeepAttackSeconds = 0.004f;
inline constexpr float kBeepReleaseSeconds = 0.030f;
inline constexpr float kBeepGain = 0.22f;

class NoteBeeps {
public:
    NoteBeeps() { reset(); }

    // Estado a cero (como al construirlo).
    void reset();

    // SUMA los pitidos a `frames` muestras estéreo entrelazadas (L, R) de `out`, con la
    // instantánea `s` y el volumen general `volume` [0, 1]. Con un mundo nuevo (otra generación)
    // toma los contadores como base, sin sucesos.
    void render_add(float* out, int frames, const AudioSnapshot& s, float volume);

private:
    struct Voice {
        bool on = false;
        float phase = 0.0f; // ciclos
        float hz = 0.0f;
        float hz_slide = 0.0f; // Hz por muestra
        int pos = 0;           // muestras sonadas
        int length = 0;        // muestras totales
        void start(float start_hz, float end_hz, float seconds);
        float step(); // siguiente muestra [−1, 1] con su envolvente; apaga al acabar
    };

    bool started_ = false;
    std::uint32_t generation_ = 0;
    std::uint32_t left_ = 0;
    std::uint32_t right_ = 0;
    std::uint32_t hazard_ = 0;
    std::uint32_t erase_ = 0;
    std::array<Voice, 2> voices_{}; // izquierda y derecha
};

} // namespace rally::platform
