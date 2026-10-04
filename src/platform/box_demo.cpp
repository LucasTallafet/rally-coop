#include "platform/box_demo.h"

#include "platform/camera_space.h"
#include "platform/fixed_step_clock.h"
#include "platform/forest_layout.h"
#include "platform/frame_pacer.h"
#include "platform/input.h"
#include "platform/input_shaping.h"
#include "platform/perf_counter.h"
#include "platform/screenshot.h"
#include "platform/telemetry_graphs.h"
#include "sim/demo_world.h"

#include <SDL3/SDL_video.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace rally::platform {

namespace {

static_assert(sim::kStepsPerSecond == kStepsPerSecond, "la simulación y el bucle usan otro paso");

// Cámara en primera persona. El campo de visión es fijo: el diseño prohíbe que cambie con
// la velocidad. Los planos de recorte son provisionales: en la fase 6 se revisan con
// profundidad invertida (glClipControl) por la distancia de dibujado.
constexpr float kFovYDegrees = 55.0f;
constexpr double kNearPlane = 0.05;
constexpr double kFarPlane = 2000.0;
// Ojo respecto al centro de la caja: dentro de ella, a 0,9 m del suelo. Sus caras quedan de
// espaldas y no se dibujan.
constexpr Vec3d kEyeOffset{0.0, 0.4, 0.0};

// Cámara fija en diagonal, respecto al centro del recinto.
constexpr float kFixedFovYDegrees = 45.0f;
constexpr Vec3d kFixedEye{0.0, 26.0, 30.0};
constexpr Vec3d kFixedTarget{0.0, 0.0, 2.0};

// O: el mundo se reconstruye a esta distancia del origen en x y z.
constexpr double kFarOffset = 20000.0;

// Zona: cámara detrás y por encima de la caja (en sus ejes) y vista de toda la zona.
constexpr Vec3d kChaseOffset{0.0, 7.0, 14.0};
constexpr Vec3d kOverviewEye{0.0, 900.0, 700.0};
constexpr Vec3d kOverviewTarget{0.0, 0.0, -60.0};
// --bench-zone: velocidad de la cámara por la pista ciega y altura del ojo.
constexpr double kZoneBenchSpeed = 100.0 / 3.6;
constexpr double kZoneBenchEyeHeight = 1.1;

Color body_color(const sim::WorldDesc& desc, std::size_t index) {
    if (static_cast<int>(index) == desc.controlled) {
        return Color{230, 160, 60, 255};
    }
    if (desc.bodies[index].motion == sim::BodyMotion::Dynamic) {
        return Color{90, 140, 200, 255};
    }
    return index == 0 ? Color{104, 118, 92, 255} : Color{140, 136, 128, 255};
}

Vector3 to_vector(const Vec3f& v) { return Vector3{v.x, v.y, v.z}; }

// Caja con sombreado plano por cara: cada cara se aclara según su normal en el mundo
// frente a una luz fija. Sin texturas ni iluminación de la GPU. Se dibuja en
// (posición − origin) pasado a float: con origin en la cámara, render relativo.
void draw_box(const Pose& pose, const sim::BoxDesc& box, Color color, const Vec3d& origin) {
    struct Face {
        Vector3 normal;
        Vector3 u; // u × v = normal: las esquinas en orden (-,-) (+,-) (+,+) (-,+) quedan
        Vector3 v; // en sentido antihorario vistas desde fuera
    };
    static constexpr Face kFaces[] = {
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},
    };
    static const Vector3 kLight = Vector3Normalize(Vector3{0.35f, 1.0f, 0.55f});
    static constexpr float kCorners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};

    const Quaternion rotation{
        static_cast<float>(pose.rotation.x), static_cast<float>(pose.rotation.y),
        static_cast<float>(pose.rotation.z), static_cast<float>(pose.rotation.w)};
    const Vec3f position = relative_to_camera(pose.position, origin);
    const Matrix transform = MatrixMultiply(QuaternionToMatrix(rotation),
                                            MatrixTranslate(position.x, position.y, position.z));
    const Vector3 half{box.half_x, box.half_y, box.half_z};

    rlPushMatrix();
    rlMultMatrixf(MatrixToFloatV(transform).v);
    rlBegin(RL_QUADS);
    for (const Face& face : kFaces) {
        const Vector3 world_normal = Vector3RotateByQuaternion(face.normal, rotation);
        const float shade = 0.5f + 0.5f * std::max(0.0f, Vector3DotProduct(world_normal, kLight));
        rlColor4ub(static_cast<unsigned char>(color.r * shade),
                   static_cast<unsigned char>(color.g * shade),
                   static_cast<unsigned char>(color.b * shade), color.a);
        for (const auto& corner : kCorners) {
            const Vector3 local =
                Vector3Add(face.normal, Vector3Add(Vector3Scale(face.u, corner[0]),
                                                   Vector3Scale(face.v, corner[1])));
            const Vector3 vertex = Vector3Multiply(local, half);
            rlVertex3f(vertex.x, vertex.y, vertex.z);
        }
    }
    rlEnd();
    rlPopMatrix();
}

// Rejilla de 1 m sobre el suelo, un poco más oscura que él, para ver el movimiento. Los
// extremos de cada línea se calculan en doble y se dibujan relativos a origin.
void draw_floor_grid(const sim::BoxDesc& floor, const Vec3d& origin) {
    constexpr Color kLine{88, 101, 78, 255};
    const double height = floor.y + floor.half_y + 0.01;
    const int half_x = static_cast<int>(floor.half_x);
    const int half_z = static_cast<int>(floor.half_z);
    for (int i = -half_x; i <= half_x; ++i) {
        const double x = floor.x + i;
        DrawLine3D(to_vector(relative_to_camera({x, height, floor.z - floor.half_z}, origin)),
                   to_vector(relative_to_camera({x, height, floor.z + floor.half_z}, origin)),
                   kLine);
    }
    for (int i = -half_z; i <= half_z; ++i) {
        const double z = floor.z + i;
        DrawLine3D(to_vector(relative_to_camera({floor.x - floor.half_x, height, z}, origin)),
                   to_vector(relative_to_camera({floor.x + floor.half_x, height, z}, origin)),
                   kLine);
    }
}

// Postes de 2,5 m (sobresalen de los muros) cada 10 m por dentro del recinto, junto a los
// muros. Solo se dibujan: no son cuerpos de la simulación.
void draw_posts(const sim::BoxDesc& floor, const Vec3d& origin) {
    constexpr Color kPost{236, 232, 220, 255};
    constexpr double kInside = 14.8;
    constexpr double kAlong[] = {-kInside, -5.0, 5.0, kInside};
    sim::BoxDesc post;
    post.half_x = 0.15f;
    post.half_y = 1.25f;
    post.half_z = 0.15f;
    const double y = floor.y + floor.half_y + post.half_y;
    auto draw_at = [&](double x, double z) {
        Pose pose;
        pose.position = {floor.x + x, y, floor.z + z};
        draw_box(pose, post, kPost, origin);
    };
    for (const double along : kAlong) {
        draw_at(along, -kInside);
        draw_at(along, kInside);
    }
    for (const double along : {-5.0, 5.0}) {
        draw_at(-kInside, along);
        draw_at(kInside, along);
    }
}

// Barra de un eje en [-1, 1]: el valor aplicado relleno desde el centro y el objetivo como
// una marca blanca.
constexpr int kAxisBarWidth = 200;
constexpr int kAxisBarHeight = 16;

void draw_axis_bar(int x, int y, float target, float applied) {
    const int center = x + kAxisBarWidth / 2;
    const auto offset = [](float value) {
        return static_cast<int>(std::clamp(value, -1.0f, 1.0f) * (kAxisBarWidth / 2));
    };
    DrawRectangle(x, y, kAxisBarWidth, kAxisBarHeight, Color{60, 64, 72, 255});
    const int fill = offset(applied);
    DrawRectangle(std::min(center, center + fill), y, std::abs(fill), kAxisBarHeight,
                  Color{230, 160, 60, 255});
    DrawRectangle(center + offset(target) - 1, y - 3, 3, kAxisBarHeight + 6, RAYWHITE);
    DrawRectangle(center, y, 1, kAxisBarHeight, Color{150, 156, 168, 255});
}

Pose to_pose(const sim::BodyPose& body) {
    Pose pose;
    pose.position = {body.x, body.y, body.z};
    pose.rotation = {body.qx, body.qy, body.qz, body.qw};
    return pose;
}

} // namespace

BoxDemo::BoxDemo() {
    rlSetClipPlanes(kNearPlane, kFarPlane);
    describe_world();
    reset();
}

void BoxDemo::describe_world() {
    if (zone_mode_) {
        // A ciegas, cada prueba sale en la pista ciega; si no, al principio de la recta.
        desc_ = blind_mode_ ? sim::zone_world(blind_series_[static_cast<std::size_t>(blind_trial_)],
                                              sim::ZoneStart::BlindTrack)
                            : sim::zone_world(blind_surface_);
    } else {
        desc_ = sim::demo_arena(far_from_origin_ ? kFarOffset : 0.0);
    }
}

void BoxDemo::use_arena() {
    zone_mode_ = false;
    blind_mode_ = false;
    view_ = 0;
    describe_world();
    reset();
}

BoxDemo::~BoxDemo() {
    recording_.finish(*world_);
    if (target_.id != 0) {
        UnloadRenderTexture(target_);
    }
}

void BoxDemo::set_render_size(int width, int height) {
    if (target_.id != 0) {
        UnloadRenderTexture(target_);
    }
    target_ = LoadRenderTexture(width, height);
    SetTextureFilter(target_.texture, TEXTURE_FILTER_BILINEAR);
}

int BoxDemo::render_width() const {
    return target_.id != 0 ? target_.texture.width : GetScreenWidth();
}

int BoxDemo::render_height() const {
    return target_.id != 0 ? target_.texture.height : GetScreenHeight();
}

void BoxDemo::reset() {
    if (world_) {
        recording_.finish(*world_);
    }
    world_ = std::make_unique<sim::SimWorld>();
    world_->build(desc_);
    world_->seal();
    world_->set_telemetry(true);
    telemetry_history_.clear();
    if (world_->zone() != nullptr) {
        zone_draw_.build(*world_->zone(), blind_mode_);
    }
    recording_.start(desc_, *world_);
    shift_up_presses_ = 0;
    shift_down_presses_ = 0;
    reset_presses_ = 0;
    read_poses(current_);
    previous_ = current_;
}

void BoxDemo::read_poses(std::vector<Pose>& poses) const {
    poses.resize(static_cast<std::size_t>(world_->body_count()));
    for (std::size_t i = 0; i < poses.size(); ++i) {
        poses[i] = to_pose(world_->pose(static_cast<int>(i)));
    }
}

void BoxDemo::handle_keys(FramePacer& pacer) {
    if (IsKeyPressed(KEY_F)) {
        pacer.set_target(pacer.target() == FrameTarget::MonitorRate ? FrameTarget::Fps30
                                                                    : FrameTarget::MonitorRate);
    }
    if (IsKeyPressed(KEY_I)) {
        interpolate_ = !interpolate_;
    }
    if (IsKeyPressed(KEY_V)) {
        pacer.set_vsync(!pacer.vsync());
    }
    if (IsKeyPressed(KEY_P)) {
        paused_ = !paused_;
    }
    if (IsKeyPressed(KEY_C)) {
        capture_requested_ = true;
    }
    if (IsKeyPressed(KEY_TAB)) {
        view_ = (view_ + 1) % (zone_mode_ ? 3 : 2);
    }
    if (IsKeyPressed(KEY_H)) {
        overlay_visible_ = !overlay_visible_;
    }
    if (IsKeyPressed(KEY_T)) {
        graphs_.press(blind_mode_, default_graph_pages());
    }
    if (IsKeyPressed(KEY_K)) {
        zone_mode_ = !zone_mode_;
        view_ = 0;
        describe_world();
        reset();
    }
    if (zone_mode_ && IsKeyPressed(KEY_M)) {
        // Al entrar empieza la serie desde la primera prueba; al salir, la pista vuelve a
        // la superficie elegida con G.
        blind_mode_ = !blind_mode_;
        if (blind_mode_) {
            blind_series_ = sim::blind_series(blind_seed_);
            blind_trial_ = 0;
        }
        describe_world();
        reset();
    }
    if (zone_mode_ && IsKeyPressed(KEY_G)) {
        if (blind_mode_) {
            blind_trial_ = std::min(blind_trial_ + 1, sim::kBlindTrials - 1);
        } else {
            blind_surface_ = static_cast<sim::Surface>((static_cast<int>(blind_surface_) + 1) %
                                                       sim::kSurfaceCount);
        }
        describe_world();
        reset();
    }
    if (IsKeyPressed(KEY_J)) {
        world_float_ = !world_float_;
    }
    if (!zone_mode_ && IsKeyPressed(KEY_B)) {
        forest_mode_ = next_forest_mode(forest_mode_);
    }
    if (IsKeyPressed(KEY_N)) {
        snap_ = !snap_;
    }
    if (IsKeyPressed(KEY_L)) {
        if (frame_log_.active()) {
            frame_log_.stop();
        } else {
            frame_log_.start();
        }
    }
    // La zona está hecha para el origen: O solo mueve el recinto.
    if (!zone_mode_ && IsKeyPressed(KEY_O)) {
        far_from_origin_ = !far_from_origin_;
        describe_world();
        reset();
    }
    if (IsKeyPressed(KEY_BACKSPACE)) {
        reset();
    }
}

void BoxDemo::prepare_steps(int count) {
    if (!paused_) {
        recording_.prepare(count);
    }
}

float BoxDemo::step(const sim::SimInput& input) {
    if (paused_) {
        return 0.0f;
    }
    world_->step(input);
    recording_.record(input);
    telemetry_history_.push(world_->telemetry());
    const std::uint16_t pressed = world_->controls().pressed;
    shift_up_presses_ += (pressed & sim::kButtonShiftUp) != 0 ? 1 : 0;
    shift_down_presses_ += (pressed & sim::kButtonShiftDown) != 0 ? 1 : 0;
    reset_presses_ += (pressed & sim::kButtonReset) != 0 ? 1 : 0;
    previous_.swap(current_);
    read_poses(current_);
    return world_->last_impact();
}

void BoxDemo::draw(const FrameSteps& steps, std::int64_t frame_ns, const FramePacer& pacer,
                   const FixedStepClock& clock, const DriverInput& driver,
                   const sim::SimInput& input, float frame_impact, PerfCounter& perf) {
    if (frame_impact > kImpactThreshold) {
        last_rumble_impact_ = frame_impact;
    }
    frame_log_.write(frame_ns, steps, clock.debt(), clock.snap_period(), pacer.frame_period());

    // En pausa se dibuja siempre el mismo estado: todos los fotogramas son idénticos.
    drawn_.resize(current_.size());
    for (std::size_t i = 0; i < current_.size(); ++i) {
        drawn_[i] = interpolate_ && !paused_ ? interpolate(previous_[i], current_[i], steps.alpha)
                                             : current_[i];
    }

    // Ojo y orientación de la cámara en el mundo, en doble.
    const auto controlled = static_cast<std::size_t>(desc_.controlled);
    Vec3d eye;
    Vec3d forward;
    Vec3d up{0.0, 1.0, 0.0};
    if (zone_bench_) {
        // --bench-zone: el ojo recorre el eje de la pista ciega a 100 km/h.
        advance_zone_bench(frame_ns);
        const sim::ZoneArea& track = world_->zone()->area(sim::AreaId::BlindTrack);
        const double length = sim::TestZone::axis_length(track);
        const double s =
            std::fmod(static_cast<double>(zone_bench_ns_) * 1e-9 * kZoneBenchSpeed, length);
        const sim::AxisPoint p = sim::TestZone::axis_point(track, s);
        eye = {p.x, world_->zone()->ground_height(p.x, p.z) + kZoneBenchEyeHeight, p.z};
        forward = {std::sin(p.psi), -0.05, -std::cos(p.psi)};
    } else if (view_ == 0) {
        const Pose head = attach(drawn_[controlled], kEyeOffset);
        eye = head.position;
        forward = rotate(head.rotation, {0.0, 0.0, -1.0});
        up = rotate(head.rotation, {0.0, 1.0, 0.0});
    } else if (!zone_mode_) {
        const Vec3d world_offset{desc_.bodies[0].x, 0.0, desc_.bodies[0].z};
        eye = {world_offset.x + kFixedEye.x, kFixedEye.y, world_offset.z + kFixedEye.z};
        forward = {kFixedTarget.x - kFixedEye.x, kFixedTarget.y - kFixedEye.y,
                   kFixedTarget.z - kFixedEye.z};
    } else if (view_ == 1) {
        // Detrás y por encima de la caja, girando con ella.
        const Pose& box = drawn_[controlled];
        eye = attach(box, kChaseOffset).position;
        forward = {box.position.x - eye.x, box.position.y + 1.0 - eye.y, box.position.z - eye.z};
    } else {
        eye = kOverviewEye;
        forward = {kOverviewTarget.x - eye.x, kOverviewTarget.y - eye.y, kOverviewTarget.z - eye.z};
    }

    // Render relativo: todo se dibuja respecto al ojo y la cámara de raylib queda en el
    // origen, solo con la rotación. Depuración (J): todo en float del mundo, con la cámara
    // en float(ojo), para ver por contraste el temblor lejos del origen.
    const Vec3d origin = world_float_ ? Vec3d{} : eye;
    const Vec3f camera_position = relative_to_camera(eye, origin);
    Camera3D camera{};
    camera.position = to_vector(camera_position);
    camera.target = Vector3{camera_position.x + static_cast<float>(forward.x),
                            camera_position.y + static_cast<float>(forward.y),
                            camera_position.z + static_cast<float>(forward.z)};
    camera.up =
        Vector3{static_cast<float>(up.x), static_cast<float>(up.y), static_cast<float>(up.z)};
    camera.fovy = view_ == 0 || zone_bench_ ? kFovYDegrees : kFixedFovYDegrees;
    camera.projection = CAMERA_PERSPECTIVE;

    constexpr Color kSky{40, 44, 52, 255};
    BeginDrawing();
    perf.begin_render();
    if (target_.id != 0) {
        BeginTextureMode(target_);
    }
    ClearBackground(kSky);
    BeginMode3D(camera);
    if (zone_mode_) {
        zone_draw_.draw(eye, origin);
        for (std::size_t i = 0; i < drawn_.size(); ++i) {
            draw_box(drawn_[i], desc_.bodies[i], body_color(desc_, i), origin);
        }
    } else {
        for (std::size_t i = 0; i < drawn_.size(); ++i) {
            draw_box(drawn_[i], desc_.bodies[i], body_color(desc_, i), origin);
            if (i == 0) {
                draw_floor_grid(desc_.bodies[0], origin);
            }
        }
        draw_posts(desc_.bodies[0], origin);
        // El bosque rodea el recinto: su centro es el del suelo.
        forest_.draw(forest_mode_,
                     relative_to_camera(Vec3d{desc_.bodies[0].x, 0.0, desc_.bodies[0].z}, origin));
    }
    EndMode3D();
    if (target_.id != 0) {
        EndTextureMode();
        // Las RenderTexture de OpenGL quedan invertidas en y: el rectángulo de origen, también.
        const auto width = static_cast<float>(target_.texture.width);
        const auto height = static_cast<float>(target_.texture.height);
        DrawTexturePro(target_.texture, Rectangle{0.0f, 0.0f, width, -height},
                       Rectangle{0.0f, 0.0f, static_cast<float>(GetScreenWidth()),
                                 static_cast<float>(GetScreenHeight())},
                       Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }

    // Velocidad horizontal de la caja controlada en el último paso.
    const Vec3d& now = current_[controlled].position;
    const Vec3d& before = previous_[controlled].position;
    const double speed = std::sqrt((now.x - before.x) * (now.x - before.x) +
                                   (now.z - before.z) * (now.z - before.z)) *
                         kStepsPerSecond;

    // H oculta el texto para mirar la escena; en la zona quedan las vueltas (y la prueba a
    // ciegas).
    if (!overlay_visible_) {
        if (zone_mode_) {
            draw_zone_overlay(16);
        }
    } else {
        const char* target =
            pacer.target() == FrameTarget::MonitorRate ? "frecuencia del monitor" : "30 fps";
        const char* method = !pacer.uses_timer()             ? ""
                             : pacer.high_resolution_timer() ? " + temporizador de alta resolución"
                                                             : " + temporizador de 1 ms";
        const Color text = Color{220, 223, 228, 255};
        DrawText(TextFormat("último fotograma: %.2f ms   bosque (B): %s%s",
                            static_cast<double>(frame_ns) / 1e6, forest_mode_name(forest_mode_),
                            forest_mode_ == ForestMode::Off
                                ? ""
                                : TextFormat(", %d árboles de %d triángulos", kForestTrees,
                                             kTreeTriangles)),
                 16, 16, 20, text);
        DrawText(
            TextFormat("pasos en el último fotograma: %d   alpha: %.2f", steps.count, steps.alpha),
            16, 42, 20, text);
        DrawText(TextFormat("ritmo: %s   sincronización vertical: %s", target,
                            pacer.vsync() ? "sí" : "no"),
                 16, 68, 20, text);
        int actual_interval = 0;
        const bool interval_known = SDL_GL_GetSwapInterval(&actual_interval);
        DrawText(TextFormat("monitor: %.2f Hz   intervalo pedido: %d%s   real (SDL): %s",
                            pacer.refresh_rate(), pacer.swap_interval(), method,
                            interval_known ? TextFormat("%d", actual_interval) : "desconocido"),
                 16, 94, 20, text);
        DrawText(TextFormat("interpolación: %s   pantalla: %s   registro: %s   %s",
                            interpolate_ ? "sí" : "no",
                            IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) ? "completa sin bordes"
                                                                         : "ventana",
                            frame_log_.active() ? frame_log_.name() : "no", paused_ ? "PAUSA" : ""),
                 16, 120, 20, text);
        DrawText(TextFormat("render: %d×%d%s", render_width(), render_height(),
                            target_.id != 0 ? " en RenderTexture, escalado a la ventana" : ""),
                 16, 146, 20, text);
        const char* view_name = zone_bench_   ? "medida del suelo"
                                : view_ == 0  ? "primera persona"
                                : !zone_mode_ ? "fija"
                                : view_ == 1  ? "detrás y por encima"
                                              : "toda la zona";
        DrawText(TextFormat("cámara: %s   mundo: %s   render: %s", view_name,
                            zone_mode_         ? "zona de pruebas"
                            : far_from_origin_ ? "recinto a 20 km del origen"
                                               : "recinto en el origen",
                            world_float_ ? "FLOAT DEL MUNDO (depuración)" : "relativo a la cámara"),
                 16, 172, 20, text);
        DrawText(TextFormat("ojo: %.3f, %.3f, %.3f m   velocidad: %.1f km/h", eye.x, eye.y, eye.z,
                            speed * 3.6),
                 16, 198, 20, text);
        const Gamepads& pads = driver.gamepads();
        if (pads.active_name() != nullptr) {
            DrawText(TextFormat("mando: %s (%s)   gatillos: RT %s, LT %s   conectados: %d",
                                pads.active_name(), pads.active_type(),
                                pads.active_throttle_digital() ? "digital" : "analógico",
                                pads.active_brake_digital() ? "digital" : "analógico",
                                pads.connected()),
                     16, 224, 20, text);
        } else {
            DrawText(TextFormat("mando: ninguno activo   conectados: %d", pads.connected()), 16,
                     224, 20, text);
        }
        const sim::ControlState& controls = world_->controls();
        constexpr float kAxisScale = 1.0f / sim::kAxisMax;
        draw_axis_bar(16, 256, input.steer * kAxisScale, controls.steer.applied * kAxisScale);
        draw_axis_bar(16 + kAxisBarWidth + 16, 256, input.throttle * kAxisScale,
                      controls.throttle.applied * kAxisScale);
        draw_axis_bar(16 + 2 * (kAxisBarWidth + 16), 256, input.brake * kAxisScale,
                      controls.brake.applied * kAxisScale);
        const auto mode = [&input](std::uint16_t bit) {
            return (input.buttons & bit) != 0 ? "digital" : "analógico";
        };
        // Barras: dirección, acelerador y freno; blanco el objetivo y naranja el aplicado.
        DrawText(TextFormat("dirección %s, acelerador %s, freno %s", mode(sim::kButtonSteerDigital),
                            mode(sim::kButtonThrottleDigital), mode(sim::kButtonBrakeDigital)),
                 16 + 3 * (kAxisBarWidth + 16), 252, 20, text);
        DrawText(
            TextFormat("freno de mano: %3.0f %%   pulsaciones: subir %d, bajar %d, recolocar %d",
                       100.0f * controls.handbrake * kAxisScale, shift_up_presses_,
                       shift_down_presses_, reset_presses_),
            16, 282, 20, text);
        DrawText(TextFormat("impacto: %.2f m/s   último que vibró: %.2f m/s (umbral %.1f)",
                            frame_impact, last_rumble_impact_, kImpactThreshold),
                 16, 308, 20, text);
        DrawText(TextFormat("grabación: %s   pasos: %llu%s", recording_.name().c_str(),
                            static_cast<unsigned long long>(recording_.steps()),
                            recording_.error().empty()
                                ? ""
                                : TextFormat("   ERROR: %s", recording_.error().c_str())),
                 16, 334, 20, text);
        if (zone_mode_) {
            draw_zone_overlay(360);
        }
        const Color help = Color{150, 156, 168, 255};
        DrawText("W/S o gatillos: acelerar/frenar   A/D o stick: girar   Espacio o abajo: freno de "
                 "mano   Q/E o hombros: marchas   R o arriba: recolocar",
                 16, GetScreenHeight() - 140, 20, help);
        DrawText("X: saltar (temporal)   Retroceso: reiniciar el mundo   K: zona/recinto   "
                 "G: superficie de la pista ciega (a ciegas: siguiente prueba)   M: modo a ciegas",
                 16, GetScreenHeight() - 114, 20, help);
        DrawText(
            "Tab: cámara   J: render en float del mundo   F: monitor/30 fps   I: interpolación", 16,
            GetScreenHeight() - 88, 20, help);
        DrawText("V: sincronización vertical   N: ajuste al refresco   L: registro   "
                 "B: bosque y O: origen/20 km (en el recinto)",
                 16, GetScreenHeight() - 62, 20, help);
        DrawText(TextFormat("F1: rendimiento   F11 o Alt+Intro: pantalla completa   P: pausa   "
                            "C: captura   H: texto%s   Esc: salir",
                            blind_mode_ ? "" : "   T: gráficas"),
                 16, GetScreenHeight() - 36, 20, help);
    }

    if (graphs_.visible(blind_mode_) >= 0) {
        draw_telemetry_graphs(telemetry_history_, graphs_.visible(blind_mode_),
                              GetScreenWidth() - (perf.visible() ? PerfCounter::kPanelWidth : 0));
    }
    perf.draw(pacer, clock, snap_, render_width(), render_height());
    if (capture_requested_) {
        save_screenshot();
        capture_requested_ = false;
    }
    perf.end_render();
    EndDrawing();
    perf.end_present();
}

void BoxDemo::draw_zone_overlay(int y) const {
    const Color text = Color{220, 223, 228, 255};
    const Color strong = Color{250, 200, 90, 255};
    const sim::TestZone& zone = *world_->zone();
    struct LapText {
        char text[24];
    };
    // Tiempo en pasos como segundos, o "--" si no hay.
    const auto lap_text = [](std::int64_t steps) {
        LapText out{};
        if (steps < 0) {
            std::snprintf(out.text, sizeof(out.text), "--");
        } else {
            std::snprintf(out.text, sizeof(out.text), "%.2f s",
                          static_cast<double>(steps) / kStepsPerSecond);
        }
        return out;
    };
    const sim::LapState& lap = *world_->lap();
    // A ciegas no se dice la superficie ni la configuración: solo la prueba en curso.
    if (blind_mode_) {
        DrawText(TextFormat("MODO A CIEGAS   Prueba %d/%d", blind_trial_ + 1, sim::kBlindTrials),
                 16, y, 24, strong);
    } else {
        const Pose& box = current_[static_cast<std::size_t>(desc_.controlled)];
        const int area = zone.area_at(box.position.x, box.position.z);
        const char* where = area < 0 ? "fondo" : zone.areas()[static_cast<std::size_t>(area)].name;
        DrawText(TextFormat("zona: %s (%s)   pista ciega (G): %s", where,
                            sim::surface_name(zone.surface_at(box.position.x, box.position.z)),
                            sim::surface_name(zone.blind_surface())),
                 16, y, 20, text);
    }
    // Vueltas de la pista ciega, contadas en pasos de 1/240 s por la simulación.
    DrawText(TextFormat("vuelta: %s   última: %s   mejor: %s   vueltas: %d",
                        lap_text(lap.current()).text, lap_text(lap.last_lap).text,
                        lap_text(lap.best_lap).text, lap.laps),
             16, y + 28, 24, strong);
    DrawText(TextFormat("dibujo del suelo (CPU): %.2f ms   media %.2f ms   máximo %.2f ms "
                        "(últimos %d fotogramas)",
                        zone_draw_.last_cpu_ms(), zone_draw_.mean_cpu_ms(), zone_draw_.max_cpu_ms(),
                        ZoneDraw::kStatsFrames),
             16, y + 58, 20, text);
}

void BoxDemo::start_zone_bench() {
    if (!zone_mode_ || blind_mode_) {
        zone_mode_ = true;
        blind_mode_ = false;
        describe_world();
        reset();
    }
    zone_bench_ = true;
    zone_bench_ns_ = 0;
    zone_bench_measuring_ = false;
    zone_bench_ground_ms_.reserve(static_cast<std::size_t>(kZoneBenchMeasureNs / 1'000'000));
}

void BoxDemo::advance_zone_bench(std::int64_t frame_ns) {
    if (zone_bench_done_) {
        return;
    }
    zone_bench_ns_ += frame_ns;
    if (!zone_bench_measuring_ && zone_bench_ns_ >= kZoneBenchWarmupNs) {
        zone_bench_measuring_ = true;
        zone_draw_.reset_totals();
        return;
    }
    if (zone_bench_measuring_) {
        const double ms = static_cast<double>(frame_ns) / 1e6;
        zone_bench_frame_ms_total_ += ms;
        zone_bench_frame_ms_max_ = std::max(zone_bench_frame_ms_max_, ms);
        ++zone_bench_frames_;
        // CPU del dibujo del suelo del fotograma anterior (este aún no se ha dibujado).
        if (zone_bench_ground_ms_.size() < zone_bench_ground_ms_.capacity()) {
            zone_bench_ground_ms_.push_back(
                {zone_draw_.last_cpu_ms(), ms, zone_draw_.last_breakdown()});
        }
        if (zone_bench_ns_ >= kZoneBenchWarmupNs + kZoneBenchMeasureNs) {
            write_zone_bench_report();
            zone_bench_done_ = true;
        }
    }
}

void BoxDemo::write_zone_bench_report() const {
    int number = 1;
    while (
        FileExists(TextFormat("%s/rendimiento_zona_%03d.txt", GetApplicationDirectory(), number))) {
        ++number;
    }
    std::FILE* file = nullptr;
    if (fopen_s(&file,
                TextFormat("%s/rendimiento_zona_%03d.txt", GetApplicationDirectory(), number),
                "w") != 0 ||
        file == nullptr) {
        TraceLog(LOG_WARNING, "no se pudo escribir el informe de la zona");
        return;
    }
    const double frames = static_cast<double>(std::max<std::int64_t>(zone_bench_frames_, 1));
    std::fprintf(file, "Medida del dibujo del suelo de la zona de pruebas (--bench-zone)\n");
    std::fprintf(file, "Cámara por el eje de la pista ciega a 100 km/h, ojo a %.1f m\n",
                 kZoneBenchEyeHeight);
    std::fprintf(file, "Render: %d x %d, pantalla: %s\n", render_width(), render_height(),
                 IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) ? "completa sin bordes" : "ventana");
    std::fprintf(file, "Fotogramas medidos: %lld en %.1f s (tras %.1f s de calentamiento)\n",
                 static_cast<long long>(zone_bench_frames_),
                 static_cast<double>(kZoneBenchMeasureNs) / 1e9,
                 static_cast<double>(kZoneBenchWarmupNs) / 1e9);
    std::fprintf(file, "Fotograma: media %.3f ms, máximo %.3f ms\n",
                 zone_bench_frame_ms_total_ / frames, zone_bench_frame_ms_max_);
    std::fprintf(file, "CPU del dibujo del suelo: media %.3f ms, máximo %.3f ms (límite 1 ms)\n",
                 zone_draw_.total_mean_ms(), zone_draw_.total_max_ms());
    std::vector<double> sorted;
    sorted.reserve(zone_bench_ground_ms_.size());
    double cells = 0.0;
    double near_ms = 0.0;
    for (const ZoneBenchFrame& f : zone_bench_ground_ms_) {
        sorted.push_back(f.ground_ms);
        cells += f.breakdown.cells;
        near_ms += f.breakdown.near_ms;
    }
    std::sort(sorted.begin(), sorted.end());
    if (!sorted.empty()) {
        const std::size_t p99 = std::min(sorted.size() - 1, sorted.size() * 99 / 100);
        const auto over =
            std::count_if(sorted.begin(), sorted.end(), [](double ms) { return ms > 1.0; });
        const auto count = static_cast<double>(sorted.size());
        std::fprintf(file,
                     "CPU del dibujo del suelo: p99 %.3f ms; %lld de %zu fotogramas por encima "
                     "de 1 ms\n",
                     sorted[p99], static_cast<long long>(over), sorted.size());
        std::fprintf(file, "Rejilla cercana por fotograma: media %.1f celdas nuevas, %.3f ms\n",
                     cells / count, near_ms / count);
    }
    // Desglose de los fotogramas por encima de 1 ms (y del más lento).
    std::fprintf(file, "\nFotogramas lentos (suelo ms | rejilla ms | subidas ms | celdas | "
                       "subidas | fotograma ms):\n");
    double worst = -1.0;
    std::size_t worst_index = 0;
    for (std::size_t i = 0; i < zone_bench_ground_ms_.size(); ++i) {
        const ZoneBenchFrame& f = zone_bench_ground_ms_[i];
        if (f.ground_ms > worst) {
            worst = f.ground_ms;
            worst_index = i;
        }
    }
    for (std::size_t i = 0; i < zone_bench_ground_ms_.size(); ++i) {
        const ZoneBenchFrame& f = zone_bench_ground_ms_[i];
        if (f.ground_ms > 1.0 || i == worst_index) {
            std::fprintf(file, "  #%zu: %.3f | %.3f | %.3f | %d | %d | %.3f\n", i, f.ground_ms,
                         f.breakdown.near_ms, f.breakdown.upload_ms, f.breakdown.cells,
                         f.breakdown.uploads, f.frame_ms);
        }
    }
    std::fclose(file);
}

} // namespace rally::platform
