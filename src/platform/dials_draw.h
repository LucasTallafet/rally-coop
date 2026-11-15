#pragma once

#include "net/notes_wire.h"
#include "platform/copilot_dials.h"
#include "platform/copilot_dials_layout.h"
#include "platform/overlay.h"

#include <cstdint>

namespace rally::platform {

// El dibujo de los diales del copiloto (lote 7): una capa fina sobre platform/copilot_dials_layout,
// que calcula el sitio de todo sin raylib. Las notas no se dibujan en la tablet (docs/05): aquí
// solo se ve el dial, lo que el copiloto está eligiendo, la última orden que el host aplicó y
// cuántas notas quedan por delante.
class DialsDraw {
public:
    // `notes`: el estado que manda el host (la última nota y las anclas); `car_s`: la `s` del host.
    void draw(Overlay& overlay, const DialsGeometry& geometry, const DialsView& view,
              const net::NotesState& notes, double car_s);

    std::int64_t last_draw_ns() const { return draw_ns_; }

private:
    std::int64_t draw_ns_ = 0;
};

} // namespace rally::platform
