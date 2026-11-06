#include "platform/stage_scene.h"

#include <utility>

namespace rally::platform {

bool StageScene::prepare(const sim::StageTrack& track, const std::string& text,
                         std::uint64_t expected_ground_hash, std::string& error, bool& loaded) {
    loaded = false;
    // Un intento fallido no se repite en cada reconstrucción (Retroceso, F2...): el mismo tramo
    // con el mismo suelo vuelve a fallar igual.
    if (!failed_error_.empty() && text == text_ && expected_ground_hash == hash_) {
        error = failed_error_;
        return false;
    }
    if (built() && !text_.empty() && text == text_ && expected_ground_hash == hash_) {
        return true;
    }
    auto copy = std::make_unique<sim::StageTrack>(track);
    if (draw_ == nullptr) {
        draw_ = std::make_unique<StageDraw>();
    }
    // build() suelta lo anterior antes de guardar referencias a la copia nueva.
    const bool ok = draw_->build(*copy, expected_ground_hash, error);
    track_ = std::move(copy);
    text_ = text;
    hash_ = expected_ground_hash;
    failed_error_ = ok ? std::string() : error;
    if (!ok) {
        return false;
    }
    loaded = true;
    return true;
}

void StageScene::draw(const Vec3d& eye, const Vec3d& origin, double distance) {
    if (built() && !text_.empty()) {
        draw_->draw(eye, origin, distance);
    }
}

} // namespace rally::platform
