#include "platform/box_demo.h"

#include "platform/fixed_step_clock.h"
#include "platform/frame_pacer.h"
#include "platform/screenshot.h"

#include <SDL3/SDL_video.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cstddef>

namespace rally::platform {

namespace {

static_assert(sim::kStepsPerSecond == kStepsPerSecond, "la simulación y el bucle usan otro paso");

constexpr std::int64_t kStatsPeriodNs = 500'000'000;
constexpr int kControlled = 5;

// Suelo de 30 × 30 m, cuatro muros de 2 m, la caja controlada (cae desde 2 m al empezar) y
// dos cajas sueltas para empujar.
sim::WorldDesc arena() {
    using sim::BodyMotion;
    sim::WorldDesc desc;
    desc.bodies = {
        {BodyMotion::Static, 0.0, -0.5, 0.0, 15.0f, 0.5f, 15.0f},
        {BodyMotion::Static, 0.0, 1.0, -15.5, 16.0f, 1.0f, 0.5f},
        {BodyMotion::Static, 0.0, 1.0, 15.5, 16.0f, 1.0f, 0.5f},
        {BodyMotion::Static, -15.5, 1.0, 0.0, 0.5f, 1.0f, 15.0f},
        {BodyMotion::Static, 15.5, 1.0, 0.0, 0.5f, 1.0f, 15.0f},
        {BodyMotion::Dynamic, 0.0, 2.0, 0.0, 0.5f, 0.5f, 0.5f},
        {BodyMotion::Dynamic, 4.0, 0.5, -3.0, 0.5f, 0.5f, 0.5f},
        {BodyMotion::Dynamic, -5.0, 0.75, 2.0, 0.75f, 0.75f, 0.75f},
    };
    desc.controlled = kControlled;
    return desc;
}

Color body_color(const sim::WorldDesc& desc, std::size_t index) {
    if (static_cast<int>(index) == desc.controlled) {
        return Color{230, 160, 60, 255};
    }
    if (desc.bodies[index].motion == sim::BodyMotion::Dynamic) {
        return Color{90, 140, 200, 255};
    }
    return index == 0 ? Color{104, 118, 92, 255} : Color{140, 136, 128, 255};
}

// Caja con sombreado plano por cara: cada cara se aclara según su normal en el mundo
// frente a una luz fija. Sin texturas ni iluminación de la GPU.
void draw_box(const Pose& pose, const sim::BoxDesc& box, Color color) {
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
    const Matrix transform = MatrixMultiply(QuaternionToMatrix(rotation),
                                            MatrixTranslate(static_cast<float>(pose.position.x),
                                                            static_cast<float>(pose.position.y),
                                                            static_cast<float>(pose.position.z)));
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

// Rejilla de 1 m sobre el suelo, un poco más oscura que él, para ver el movimiento.
void draw_floor_grid(const sim::BoxDesc& floor) {
    constexpr Color kLine{88, 101, 78, 255};
    const float height = static_cast<float>(floor.y) + floor.half_y + 0.01f;
    const int half_x = static_cast<int>(floor.half_x);
    const int half_z = static_cast<int>(floor.half_z);
    for (int x = -half_x; x <= half_x; ++x) {
        DrawLine3D(Vector3{static_cast<float>(x), height, -floor.half_z},
                   Vector3{static_cast<float>(x), height, floor.half_z}, kLine);
    }
    for (int z = -half_z; z <= half_z; ++z) {
        DrawLine3D(Vector3{-floor.half_x, height, static_cast<float>(z)},
                   Vector3{floor.half_x, height, static_cast<float>(z)}, kLine);
    }
}

Pose to_pose(const sim::BodyPose& body) {
    Pose pose;
    pose.position = {body.x, body.y, body.z};
    pose.rotation = {body.qx, body.qy, body.qz, body.qw};
    return pose;
}

// Nombre de la gráfica (GL_RENDERER). raylib no lo expone; glGetString se pide a SDL.
std::string query_renderer() {
    constexpr unsigned int kGlRenderer = 0x1F01;
    using GetString = const unsigned char*(__stdcall*)(unsigned int);
    const auto get_string = reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString"));
    const unsigned char* name = get_string != nullptr ? get_string(kGlRenderer) : nullptr;
    return name != nullptr ? reinterpret_cast<const char*>(name) : "desconocida";
}

} // namespace

BoxDemo::BoxDemo() : desc_(arena()), renderer_(query_renderer()) { reset(); }

void BoxDemo::reset() {
    world_ = std::make_unique<sim::SimWorld>();
    world_->build(desc_);
    world_->seal();
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
    if (IsKeyPressed(KEY_R)) {
        reset();
    }
}

void BoxDemo::step(const sim::SimInput& input) {
    if (paused_) {
        return;
    }
    world_->step(input);
    previous_.swap(current_);
    read_poses(current_);
}

void BoxDemo::update_stats(std::int64_t frame_ns) {
    stats_ns_ += frame_ns;
    ++stats_frames_;
    if (stats_ns_ >= kStatsPeriodNs) {
        fps_ = stats_frames_ * 1e9 / static_cast<double>(stats_ns_);
        average_ms_ = static_cast<double>(stats_ns_) / stats_frames_ / 1e6;
        stats_ns_ = 0;
        stats_frames_ = 0;
    }
}

void BoxDemo::draw(const FrameSteps& steps, std::int64_t frame_ns, const FramePacer& pacer) {
    update_stats(frame_ns);

    Camera3D camera{};
    camera.position = Vector3{0.0f, 26.0f, 30.0f};
    camera.target = Vector3{0.0f, 0.0f, 2.0f};
    camera.up = Vector3{0.0f, 1.0f, 0.0f};
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    BeginDrawing();
    ClearBackground(Color{40, 44, 52, 255});
    BeginMode3D(camera);
    for (std::size_t i = 0; i < current_.size(); ++i) {
        // En pausa se dibuja siempre el mismo estado: todos los fotogramas son idénticos.
        const Pose pose = interpolate_ && !paused_
                              ? interpolate(previous_[i], current_[i], steps.alpha)
                              : current_[i];
        draw_box(pose, desc_.bodies[i], body_color(desc_, i));
        if (i == 0) {
            draw_floor_grid(desc_.bodies[0]);
        }
    }
    EndMode3D();

    const char* target =
        pacer.target() == FrameTarget::MonitorRate ? "frecuencia del monitor" : "30 fps";
    const char* method = !pacer.uses_timer()             ? ""
                         : pacer.high_resolution_timer() ? " + temporizador de alta resolución"
                                                         : " + temporizador de 1 ms";
    const Color text = Color{220, 223, 228, 255};
    DrawText(TextFormat("%.1f fps   %.2f ms por fotograma (último %.2f)", fps_, average_ms_,
                        static_cast<double>(frame_ns) / 1e6),
             16, 16, 20, text);
    DrawText(TextFormat("pasos en el último fotograma: %d   alpha: %.2f", steps.count, steps.alpha),
             16, 42, 20, text);
    DrawText(
        TextFormat("ritmo: %s   sincronización vertical: %s", target, pacer.vsync() ? "sí" : "no"),
        16, 68, 20, text);
    int actual_interval = 0;
    const bool interval_known = SDL_GL_GetSwapInterval(&actual_interval);
    DrawText(TextFormat("monitor: %.2f Hz   intervalo pedido: %d%s   real (SDL): %s",
                        pacer.refresh_rate(), pacer.swap_interval(), method,
                        interval_known ? TextFormat("%d", actual_interval) : "desconocido"),
             16, 94, 20, text);
    DrawText(
        TextFormat("interpolación: %s   %s", interpolate_ ? "sí" : "no", paused_ ? "PAUSA" : ""),
        16, 120, 20, text);
    DrawText(TextFormat("gráfica: %s", renderer_.c_str()), 16, 146, 20, text);
    const Color help = Color{150, 156, 168, 255};
    DrawText("Flechas/WASD: mover   Espacio: saltar   R: reiniciar", 16, GetScreenHeight() - 62, 20,
             help);
    DrawText("F: monitor/30 fps   I: interpolación   V: sincronización vertical   P: pausa   "
             "C: captura   Esc: salir",
             16, GetScreenHeight() - 36, 20, help);

    if (capture_requested_) {
        save_screenshot();
        capture_requested_ = false;
    }
    EndDrawing();
}

} // namespace rally::platform
