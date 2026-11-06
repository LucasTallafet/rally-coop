#pragma once

#include "platform/camera_space.h"
#include "platform/stage_draw.h"
#include "sim/stage_track.h"

#include <cstdint>
#include <memory>
#include <string>

namespace rally::platform {

// El tramo dibujado en el juego y en el copiloto (tarea tramo-modo, lote 5): un solo dibujo para
// los dos. Guarda su propia copia del StageTrack, porque el SimWorld se recrea en cada
// reconstrucción y StageDraw guarda referencias a él, y reconstruye las mallas solo si cambia el
// tramo (Retroceso, F2, F6... crean un mundo nuevo con el mismo tramo).
class StageScene {
public:
    // Prepara el dibujo de `track` (el de `text`) con el hash del suelo que da la física. true si
    // queda listo; false con el motivo (los dos hashes) y sin tramo dibujado. `loaded`: se han
    // construido las mallas ahora (false si ya estaban).
    bool prepare(const sim::StageTrack& track, const std::string& text,
                 std::uint64_t expected_ground_hash, std::string& error, bool& loaded);
    bool built() const { return draw_ != nullptr && draw_->built(); }
    // Dibuja el tramo si está listo (dentro de BeginMode3D); `distance` como en StageDraw::draw.
    void draw(const Vec3d& eye, const Vec3d& origin, double distance = kStageDrawDistance);

    const StageDraw* stage_draw() const { return draw_.get(); }

private:
    std::unique_ptr<sim::StageTrack> track_;
    std::unique_ptr<StageDraw> draw_; // se crea al preparar el primero: necesita la ventana
    std::string text_;
    std::uint64_t hash_ = 0;
    std::string failed_error_; // el último intento fallido con este texto y hash: no se repite
};

} // namespace rally::platform
