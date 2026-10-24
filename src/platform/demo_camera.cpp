#include "platform/demo_camera.h"

#include "platform/camera_space.h"
#include "platform/copilot_view.h"
#include "platform/demo_camera_keys.h"
#include "platform/demo_common.h"
#include "platform/keys_raylib.h"
#include "platform/seat.h"

#include <cmath>

namespace rally::platform {

using namespace demo;

namespace {

// Cámara en primera persona. El campo de visión es fijo: el diseño prohíbe que cambie con
// la velocidad. Los planos de recorte son provisionales: en la fase 6 se revisan con
// profundidad invertida (glClipControl) por la distancia de dibujado.
constexpr float kFovYDegrees = 55.0f;

// Cámara fija en diagonal, respecto al centro del recinto.
constexpr float kFixedFovYDegrees = 45.0f;
constexpr Vec3d kFixedEye{0.0, 26.0, 30.0};
constexpr Vec3d kFixedTarget{0.0, 0.0, 2.0};

// Cámara de persecución: detrás y por encima del coche (en sus ejes), mirando a 1 m sobre su
// origen. Vista de toda la zona.
constexpr Vec3d kChaseOffset{0.0, 2.2, 6.5};
constexpr Vec3d kOverviewEye{0.0, 900.0, 700.0};
constexpr Vec3d kOverviewTarget{0.0, 0.0, -60.0};

Vector3 to_vector(const Vec3f& v) { return Vector3{v.x, v.y, v.z}; }

} // namespace

void DemoCamera::handle_keys(DemoContext& ctx, FramePacer& /*pacer*/, PerfCounter& /*perf*/) {
    const std::uint8_t context = ctx.frame_key_context;
    if (action_pressed(action(CameraAction::Camera), context)) {
        ctx.view = next_camera_view(ctx.view);
    }
    if (action_pressed(action(CameraAction::HeadMotion), context)) {
        ctx.settings.head_motion = !ctx.settings.head_motion;
        ctx.settings_saver.changed(now_ns());
        ctx.overlay.set_message(Overlay::Message::Settings,
                                ctx.settings.head_motion
                                    ? "F9: movimiento de la cabeza activado"
                                    : "F9: movimiento de la cabeza desactivado",
                                kText, kShortMessageNs);
    }
    if (action_pressed(action(CameraAction::WorldFloat), context)) {
        ctx.world_float = !ctx.world_float;
    }
    // Asiento: se repite al mantener la tecla.
    struct SeatKey {
        CameraAction action;
        SeatAxis axis;
        int delta_cm;
    };
    static constexpr SeatKey kSeatKeys[] = {
        {CameraAction::SeatUp, SeatAxis::Up, 1},
        {CameraAction::SeatDown, SeatAxis::Up, -1},
        {CameraAction::SeatForward, SeatAxis::Forward, 1},
        {CameraAction::SeatBack, SeatAxis::Forward, -1},
        {CameraAction::SeatRight, SeatAxis::Right, 1},
        {CameraAction::SeatLeft, SeatAxis::Right, -1},
    };
    const std::int64_t now = now_ns();
    for (const SeatKey& key : kSeatKeys) {
        if (action_pressed_repeat(action(key.action))) {
            const SeatOffsetCm seat = nudge_seat(ctx.settings.seat, key.axis, key.delta_cm);
            if (!(seat == ctx.settings.seat)) {
                ctx.settings.seat = seat;
                ctx.settings_saver.changed(now);
            }
        }
    }
}

void DemoCamera::begin_frame(DemoContext& ctx, const DemoFrame& /*frame*/) { update(ctx); }

void DemoCamera::update(DemoContext& ctx) {
    // Ojo y orientación de la cámara en el mundo, en doble.
    FrameCamera& c = ctx.frame_camera;
    const Pose& car = ctx.poses.drawn_car;
    c.up = {0.0, 1.0, 0.0};
    if (ctx.camera_override) {
        // --bench-zone y --bench-road (demo_measure).
        c.eye = ctx.camera_override->first;
        c.forward = ctx.camera_override->second;
    } else if (ctx.view == 0) {
        // Ojo del conductor: el del archivo del coche más el asiento del jugador, en ejes del
        // coche (platform/seat). Dentro de la caja: sus caras quedan de espaldas y no se dibujan.
        // Con F9, la cabeza (platform/head_motion) lo desplaza y lo gira un poco, interpolada
        // con el mismo alpha que la pose del coche (demo_driver_feel).
        Vec3d local = driver_eye(ctx.car, ctx.settings.seat);
        Vec3d look{0.0, 0.0, -1.0};
        Vec3d head_up{0.0, 1.0, 0.0};
        if (ctx.copilot_seat) {
            // Copiloto (platform/copilot_view): el ojo reflejado, sin asiento del jugador ni
            // cabeza; solo el giro hacia el salpicadero mientras se mantiene la tecla.
            local = copilot_eye(ctx.car);
            look = {-std::sin(ctx.copilot_look_yaw), 0.0, -std::cos(ctx.copilot_look_yaw)};
        } else if (ctx.head) {
            const HeadOffset& h = *ctx.head;
            local = {local.x + h.right, local.y + h.up, local.z - h.forward};
            // Cabeceo alrededor de x (> 0 mira arriba) y alabeo alrededor de la vista (> 0 la
            // parte de arriba de la cabeza a la derecha).
            const double cp = std::cos(h.pitch);
            const double sp = std::sin(h.pitch);
            look = {0.0, sp, -cp};
            const Vec3d pitched_up{0.0, cp, sp};
            head_up = {std::sin(h.roll), pitched_up.y * std::cos(h.roll),
                       pitched_up.z * std::cos(h.roll)};
        }
        const Pose head = attach(car, local);
        c.eye = head.position;
        c.forward = rotate(head.rotation, look);
        c.up = rotate(head.rotation, head_up);
    } else if (ctx.view == 1) {
        // Persecución: detrás y por encima del coche, girando con él.
        c.eye = attach(car, kChaseOffset).position;
        c.forward = {car.position.x - c.eye.x, car.position.y + 1.0 - c.eye.y,
                     car.position.z - c.eye.z};
    } else if (!ctx.mode.zone) {
        const Vec3d world_offset{ctx.desc.bodies[0].x, 0.0, ctx.desc.bodies[0].z};
        c.eye = {world_offset.x + kFixedEye.x, kFixedEye.y, world_offset.z + kFixedEye.z};
        c.forward = {kFixedTarget.x - kFixedEye.x, kFixedTarget.y - kFixedEye.y,
                     kFixedTarget.z - kFixedEye.z};
    } else {
        c.eye = kOverviewEye;
        c.forward = {kOverviewTarget.x - c.eye.x, kOverviewTarget.y - c.eye.y,
                     kOverviewTarget.z - c.eye.z};
    }

    // Render relativo: todo se dibuja respecto al ojo y la cámara de raylib queda en el
    // origen, solo con la rotación. Depuración (J): todo en float del mundo, con la cámara
    // en float(ojo), para ver por contraste el temblor lejos del origen.
    c.origin = ctx.world_float ? Vec3d{} : c.eye;
    const Vec3f camera_position = relative_to_camera(c.eye, c.origin);
    Camera3D camera{};
    camera.position = to_vector(camera_position);
    camera.target = Vector3{camera_position.x + static_cast<float>(c.forward.x),
                            camera_position.y + static_cast<float>(c.forward.y),
                            camera_position.z + static_cast<float>(c.forward.z)};
    camera.up =
        Vector3{static_cast<float>(c.up.x), static_cast<float>(c.up.y), static_cast<float>(c.up.z)};
    camera.fovy = ctx.view != 2 || ctx.zone_bench ? kFovYDegrees : kFixedFovYDegrees;
    camera.projection = CAMERA_PERSPECTIVE;
    c.camera = camera;
}

} // namespace rally::platform
