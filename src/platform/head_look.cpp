#include "platform/head_look.h"

#include <algorithm>
#include <cmath>

namespace rally::platform {

void HeadLook::update(const HeadLookInput& input) {
    if (frozen_) {
        return;
    }
    const double per_px = kLookMouseDegPerPx * kDegree;
    mouse_yaw_ = std::clamp(mouse_yaw_ - static_cast<double>(input.mouse_dx_px) * per_px,
                            -kLookYawMax, kLookYawMax);
    mouse_pitch_ = std::clamp(mouse_pitch_ - static_cast<double>(input.mouse_dy_px) * per_px,
                              -kLookPitchDown, kLookPitchUp);
    if (input.recenter) {
        mouse_yaw_ = 0.0;
        mouse_pitch_ = 0.0;
    }
    double target_yaw = mouse_yaw_;
    double target_pitch = mouse_pitch_;
    if (input.stick_x != 0.0f || input.stick_y != 0.0f) {
        const double x = std::clamp(static_cast<double>(input.stick_x), -1.0, 1.0);
        const double y = std::clamp(static_cast<double>(input.stick_y), -1.0, 1.0);
        target_yaw = -x * kLookYawMax; // el stick a la derecha mira a la derecha
        target_pitch = y * (y > 0.0 ? kLookPitchUp : kLookPitchDown);
    }
    if (input.dt_s > 0.0) {
        const double k = 1.0 - std::exp(-input.dt_s / kLookTau);
        yaw_ += (target_yaw - yaw_) * k;
        pitch_ += (target_pitch - pitch_) * k;
    }
}

namespace {

// Hacia dónde mira la cabeza con guiñada a la izquierda y cabeceo hacia arriba: primero se
// cabecea y después se gira alrededor de la vertical.
Vec3d look_direction(double yaw, double pitch) {
    const double cp = std::cos(pitch);
    return {-std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp};
}

} // namespace

Vec3d neck_eye_offset(double yaw, double pitch) {
    // El pivote está kNeckPivot detrás del ojo (+z) y el ojo, a kNeckPivot del pivote en la
    // dirección de la mirada: la posición nueva es pivote + kNeckPivot·mirada.
    const Vec3d look = look_direction(yaw, pitch);
    return {kNeckPivot * look.x, kNeckPivot * look.y, kNeckPivot + kNeckPivot * look.z};
}

HeadView head_view(double yaw, double pitch, const HeadOffset* f9) {
    HeadView view;
    view.eye_offset = neck_eye_offset(yaw, pitch);
    view.forward = look_direction(yaw, pitch);
    // Arriba de la cabeza: la vertical, cabeceada y girada como la mirada.
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    view.up = {sp * std::sin(yaw), cp, sp * std::cos(yaw)};
    if (f9 == nullptr) {
        return view;
    }
    view.eye_offset.x += f9->right;
    view.eye_offset.y += f9->up;
    view.eye_offset.z -= f9->forward;
    // La rotación de F9 lleva la base del coche (derecha, arriba, atrás) a (right0, up0, −look0):
    // cabeceo alrededor de x (> 0 mira arriba) y alabeo alrededor de la vista (> 0 la parte de
    // arriba de la cabeza a la derecha). Sin mirada es la cámara de F9 de siempre.
    const double fp = std::cos(f9->pitch);
    const double fs = std::sin(f9->pitch);
    const double rc = std::cos(f9->roll);
    const double rs = std::sin(f9->roll);
    const Vec3d look0{0.0, fs, -fp};
    const Vec3d up0{rs, fp * rc, fs * rc};
    const Vec3d right0{rc, -fp * rs, -fs * rs};
    const auto turn = [&](const Vec3d& v) {
        return Vec3d{v.x * right0.x + v.y * up0.x - v.z * look0.x,
                     v.x * right0.y + v.y * up0.y - v.z * look0.y,
                     v.x * right0.z + v.y * up0.z - v.z * look0.z};
    };
    view.forward = turn(view.forward);
    view.up = turn(view.up);
    return view;
}

} // namespace rally::platform
