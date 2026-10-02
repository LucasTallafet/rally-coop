#include "platform/box_demo.h"

#include "platform/camera_space.h"
#include "platform/fixed_step_clock.h"
#include "platform/frame_graph.h"
#include "platform/frame_pacer.h"
#include "platform/screenshot.h"

#include <SDL3/SDL_video.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace rally::platform {

namespace {

static_assert(sim::kStepsPerSecond == kStepsPerSecond, "la simulación y el bucle usan otro paso");

constexpr std::int64_t kStatsPeriodNs = 500'000'000;
constexpr int kControlled = 5;

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

// Suelo de 30 × 30 m, cuatro muros de 2 m, la caja controlada (cae desde 2 m al empezar) y
// dos cajas sueltas para empujar. Todo desplazado `offset` en x y z.
sim::WorldDesc arena(double offset) {
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
    for (sim::BoxDesc& box : desc.bodies) {
        box.x += offset;
        box.z += offset;
    }
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

BoxDemo::BoxDemo() : desc_(arena(0.0)), renderer_(query_renderer()) {
    rlSetClipPlanes(kNearPlane, kFarPlane);
    reset();
}

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
    if (IsKeyPressed(KEY_TAB)) {
        first_person_ = !first_person_;
    }
    if (IsKeyPressed(KEY_J)) {
        world_float_ = !world_float_;
    }
    if (IsKeyPressed(KEY_G)) {
        graph_visible_ = !graph_visible_;
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
    if (IsKeyPressed(KEY_O)) {
        far_from_origin_ = !far_from_origin_;
        desc_ = arena(far_from_origin_ ? kFarOffset : 0.0);
        reset();
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

void BoxDemo::draw(const FrameSteps& steps, std::int64_t frame_ns, const FramePacer& pacer,
                   const FixedStepClock& clock) {
    update_stats(frame_ns);
    history_.push(FrameRecord{frame_ns, steps.used, steps.count, steps.snapped});
    frame_log_.write(frame_ns, steps, clock.debt(), clock.snap_period(), pacer.frame_period());

    // En pausa se dibuja siempre el mismo estado: todos los fotogramas son idénticos.
    drawn_.resize(current_.size());
    for (std::size_t i = 0; i < current_.size(); ++i) {
        drawn_[i] = interpolate_ && !paused_ ? interpolate(previous_[i], current_[i], steps.alpha)
                                             : current_[i];
    }

    // Ojo y orientación de la cámara en el mundo, en doble.
    const Vec3d world_offset{desc_.bodies[0].x, 0.0, desc_.bodies[0].z};
    Vec3d eye;
    Vec3d forward;
    Vec3d up{0.0, 1.0, 0.0};
    if (first_person_) {
        const Pose head = attach(drawn_[kControlled], kEyeOffset);
        eye = head.position;
        forward = rotate(head.rotation, {0.0, 0.0, -1.0});
        up = rotate(head.rotation, {0.0, 1.0, 0.0});
    } else {
        eye = {world_offset.x + kFixedEye.x, kFixedEye.y, world_offset.z + kFixedEye.z};
        forward = {kFixedTarget.x - kFixedEye.x, kFixedTarget.y - kFixedEye.y,
                   kFixedTarget.z - kFixedEye.z};
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
    camera.fovy = first_person_ ? kFovYDegrees : kFixedFovYDegrees;
    camera.projection = CAMERA_PERSPECTIVE;

    BeginDrawing();
    ClearBackground(Color{40, 44, 52, 255});
    BeginMode3D(camera);
    for (std::size_t i = 0; i < drawn_.size(); ++i) {
        draw_box(drawn_[i], desc_.bodies[i], body_color(desc_, i), origin);
        if (i == 0) {
            draw_floor_grid(desc_.bodies[0], origin);
        }
    }
    draw_posts(desc_.bodies[0], origin);
    EndMode3D();

    // Velocidad horizontal de la caja controlada en el último paso.
    const Vec3d& now = current_[kControlled].position;
    const Vec3d& before = previous_[kControlled].position;
    const double speed = std::sqrt((now.x - before.x) * (now.x - before.x) +
                                   (now.z - before.z) * (now.z - before.z)) *
                         kStepsPerSecond;

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
    DrawText(TextFormat(
                 "interpolación: %s   pantalla: %s   registro: %s   %s", interpolate_ ? "sí" : "no",
                 IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) ? "completa sin bordes" : "ventana",
                 frame_log_.active() ? frame_log_.name() : "no", paused_ ? "PAUSA" : ""),
             16, 120, 20, text);
    DrawText(TextFormat("gráfica: %s", renderer_.c_str()), 16, 146, 20, text);
    DrawText(TextFormat("cámara: %s   mundo: %s   render: %s",
                        first_person_ ? "primera persona" : "fija",
                        far_from_origin_ ? "a 20 km del origen" : "en el origen",
                        world_float_ ? "FLOAT DEL MUNDO (depuración)" : "relativo a la cámara"),
             16, 172, 20, text);
    DrawText(TextFormat("ojo: %.3f, %.3f, %.3f m   velocidad: %.1f km/h", eye.x, eye.y, eye.z,
                        speed * 3.6),
             16, 198, 20, text);
    if (graph_visible_) {
        draw_frame_graph(history_, pacer.frame_period(), clock.debt(), snap_, 20, 236);
    }
    const Color help = Color{150, 156, 168, 255};
    DrawText("W/S: acelerar/atrás   A/D: girar   Espacio: saltar   R: reiniciar   "
             "O: origen/20 km",
             16, GetScreenHeight() - 114, 20, help);
    DrawText("Tab: cámara   J: render en float del mundo   F: monitor/30 fps   I: interpolación",
             16, GetScreenHeight() - 88, 20, help);
    DrawText("V: sincronización vertical   N: ajuste al refresco   G: gráfica   L: registro", 16,
             GetScreenHeight() - 62, 20, help);
    DrawText("F11 o Alt+Intro: pantalla completa   P: pausa   C: captura   Esc: salir", 16,
             GetScreenHeight() - 36, 20, help);

    if (capture_requested_) {
        save_screenshot();
        capture_requested_ = false;
    }
    EndDrawing();
}

} // namespace rally::platform
