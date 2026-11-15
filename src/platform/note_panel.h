#pragma once

#include "game/notes.h"

#include <cstdint>

namespace rally::platform {

// Lógica del panel del salpicadero (lote 7, tarea panel; docs/01 §5.4): qué va en cada hueco de la
// fila curva · distancia · curva, cuándo se tacha una nota borrada y cuándo suena un pitido y por
// qué lado. Sin raylib ni reloj: el tiempo llega como argumento (`now_ns`). Solo lee la cola de
// notas del host y la `s` del coche; no cambia nada de la conducción ni de la simulación.

// Cuánto se enseña tachada una nota borrada (decisión 2026-11-12: «medio segundo»).
inline constexpr std::int64_t kStrikeNs = 500'000'000;

enum class PanelSlotKind : std::uint8_t {
    Empty,
    Corner, // nota de curva (número o H, sentido, modificadores y peligros)
    Hazard, // peligro sin curva: ocupa un hueco de nota con su icono
};

struct PanelSlot {
    PanelSlotKind kind = PanelSlotKind::Empty;
    game::Note note;
    bool struck = false; // borrada: se enseña tachada

    friend bool operator==(const PanelSlot&, const PanelSlot&) = default;
};

// Lo que se ve en un fotograma.
struct PanelView {
    PanelSlot left;
    PanelSlot right;
    // Hueco central: la distancia de la nota izquierda (vacío si no la tiene o está tachada).
    game::NoteDistance center;
    bool has_center = false;
};

// Sucesos de sonido acumulados desde el último reset(), como los contadores del audio: el
// sintetizador suena una vez por cada incremento.
struct PanelSounds {
    std::uint32_t beeps_left = 0;   // nota de curva a la izquierda
    std::uint32_t beeps_right = 0;  // nota de curva a la derecha
    std::uint32_t hazard_beeps = 0; // peligro sin curva: por los dos lados
    std::uint32_t erase_beeps = 0;  // nota borrada: por los dos lados, otro tono
    friend bool operator==(const PanelSounds&, const PanelSounds&) = default;
};

class NotePanel {
public:
    // Mundo nuevo: nada mostrado, contadores a 0. Hay que llamarlo cuando la cola se reinicia.
    void reset();

    // Una vez por fotograma. `car_s`: la `s` de SimWorld::stage_run() (el tramo); `now_ns`:
    // monótono.
    void update(const game::NoteQueue& queue, double car_s, std::int64_t now_ns);

    const PanelView& view() const { return view_; }
    const PanelSounds& sounds() const { return sounds_; }

private:
    struct Strike {
        bool active = false;
        game::Note note;
        std::int64_t until_ns = 0;
    };

    PanelView view_;
    PanelSounds sounds_;
    Strike strike_left_;
    Strike strike_right_;
    // Lo que había en los huecos en el fotograma anterior (ids de notas vivas; 0 = ninguna).
    std::uint32_t shown_left_ = 0;
    std::uint32_t shown_right_ = 0;
    std::uint32_t beeped_left_ = 0; // la última nota viva que entró en el hueco izquierdo
    std::uint32_t erased_seen_ = 0;
};

} // namespace rally::platform
