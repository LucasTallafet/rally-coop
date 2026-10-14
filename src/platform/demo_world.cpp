#include "platform/demo_world.h"

#include "io/atomic_file.h"
#include "platform/camera_space.h"
#include "platform/demo_common.h"
#include "platform/demo_world_keys.h"
#include "platform/fixed_step_clock.h"
#include "platform/keys_raylib.h"
#include "sim/check.h"
#include "sim/demo_world.h"

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <string_view>
#include <utility>

namespace rally::platform {

using namespace demo;

namespace {

// O: el mundo se reconstruye a esta distancia del origen en x y z.
constexpr double kFarOffset = 20000.0;

constexpr Color kCarColor{230, 160, 60, 255};
constexpr Color kTireColor{38, 40, 44, 255};
constexpr Color kRimColor{200, 204, 210, 255};

// Configuraciones de prueba de la fase 2 (tarea 2.6), en el orden de F8.
constexpr std::array<std::string_view, 4> kCarConfigs = {
    "mid_awd_turbo.car", "front_awd_turbo_5cyl.car", "front_rwd_na.car",
    "front_awd_turbo_groupa.car"};

Color body_color(const sim::WorldDesc& desc, std::size_t index) {
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

Vec3d add(const Vec3d& a, const Vec3d& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3d scale(const Vec3d& a, double s) { return {a.x * s, a.y * s, a.z * s}; }

// Ángulo en (-π, π].
double wrap_angle(double a) {
    constexpr double kTwoPi = 6.283185307179586;
    a = std::fmod(a + 3.141592653589793, kTwoPi);
    return (a <= 0.0 ? a + kTwoPi : a) - 3.141592653589793;
}

// Rueda interpolada entre dos pasos (el giro, por el camino corto).
sim::SimWorld::WheelPose interpolate_wheel(const sim::SimWorld::WheelPose& a,
                                           const sim::SimWorld::WheelPose& b, double alpha) {
    sim::SimWorld::WheelPose w = b;
    const auto lerp = [alpha](float x, float y) { return static_cast<float>(x + (y - x) * alpha); };
    w.suspension_length = lerp(a.suspension_length, b.suspension_length);
    w.steer = lerp(a.steer, b.steer);
    w.rotation = static_cast<float>(a.rotation + wrap_angle(b.rotation - a.rotation) * alpha);
    return w;
}

// Coche en low-poly: la caja de la carrocería y cada rueda como un cilindro de 12 lados con
// una marca en la llanta que gira con ella (para ver el giro y el bloqueo).
void draw_car(const Pose& car_pose, const sim::CarData& car,
              const std::array<sim::SimWorld::WheelPose, sim::kWheelCount>& wheels,
              const Vec3d& origin) {
    sim::BoxDesc body;
    body.half_x = 0.5f * car.box_width;
    body.half_y = 0.5f * car.box_height;
    body.half_z = 0.5f * car.box_length;
    const Pose body_pose =
        attach(car_pose, Vec3d{0.0, car.ground_clearance + 0.5 * car.box_height, 0.0});
    draw_box(body_pose, body, kCarColor, origin);
    for (const sim::SimWorld::WheelPose& w : wheels) {
        // Centro de la rueda en ejes del coche y sus ejes tras el giro de la dirección (Jolt:
        // positivo a la izquierda, alrededor de +y).
        const Vec3d hub_local{w.attach_x, w.attach_y - w.suspension_length, w.attach_z};
        const double c = std::cos(w.steer);
        const double s = std::sin(w.steer);
        const Vec3d axle = rotate(car_pose.rotation, Vec3d{c, 0.0, -s});
        const Vec3d forward = rotate(car_pose.rotation, Vec3d{-s, 0.0, -c});
        const Vec3d up = rotate(car_pose.rotation, Vec3d{0.0, 1.0, 0.0});
        const Vec3d hub = attach(car_pose, hub_local).position;
        const Vec3d half_axle = scale(axle, 0.5 * w.width);
        const Vec3d inner = add(hub, scale(half_axle, -1.0));
        const Vec3d outer = add(hub, half_axle);
        DrawCylinderEx(to_vector(relative_to_camera(inner, origin)),
                       to_vector(relative_to_camera(outer, origin)), w.radius, w.radius, 12,
                       kTireColor);
        // Marca en la llanta, por fuera de la cara exterior: arriba al girar 0 y hacia
        // delante al rodar hacia delante.
        const Vec3d rim = add(add(outer, scale(half_axle, 0.05)),
                              add(scale(up, 0.7 * w.radius * std::cos(w.rotation)),
                                  scale(forward, 0.7 * w.radius * std::sin(w.rotation))));
        DrawSphere(to_vector(relative_to_camera(rim, origin)), 0.045f, kRimColor);
    }
}

} // namespace

bool read_golden_recording(const std::string& path, io::Recording& recording, std::string& error) {
    std::string bytes;
    if (!io::read_file(path, bytes, error)) {
        return false;
    }
    io::ParseResult parsed = io::parse(bytes);
    if (!parsed.ok) {
        error = parsed.error;
        return false;
    }
    sim::WorldDesc desc;
    if (!io::parse_world(parsed.recording.header.world, desc, error,
                         parsed.recording.header.format_version)) {
        return false;
    }
    if (!desc.car) {
        error = "la grabación no tiene coche";
        return false;
    }
    const sim::CarParseResult car =
        sim::parse_car(parsed.recording.header.car_text, sim::CarKeys::Sim);
    if (!car.ok) {
        error = "coche de la grabación: " + car.error;
        return false;
    }
    recording = std::move(parsed.recording);
    return true;
}

std::string default_car_path() {
    std::error_code error;
    std::filesystem::path folder = std::filesystem::path(GetApplicationDirectory());
    for (int up = 0; up < 8 && !folder.empty(); ++up) {
        const std::filesystem::path candidate = folder / "data" / "cars" / "mid_awd_turbo.car";
        if (std::filesystem::exists(candidate, error)) {
            const std::u8string text = candidate.u8string();
            return std::string(text.begin(), text.end());
        }
        const std::filesystem::path parent = folder.parent_path();
        if (parent == folder) {
            break;
        }
        folder = parent;
    }
    return {};
}

DemoWorld::DemoWorld(DemoContext& ctx, std::string car_path) : ctx_(ctx) {
    ctx.car_path = std::move(car_path);
    ctx.recording = &recording_;
    ctx.zone_draw = &zone_draw_;
    static std::string message; // vive hasta el abort
    message = "no se puede leer el coche '" + ctx.car_path +
              "' (por defecto se busca data/cars/mid_awd_turbo.car desde la carpeta del exe "
              "hacia arriba; o --car <archivo>)";
    std::string error;
    RALLY_CHECK(!ctx.car_path.empty() && io::read_file(ctx.car_path, car_text_, error),
                message.c_str());
    ctx.car = sim::load_car_or_abort(car_text_, ctx.car_path, sim::CarKeys::Full);
    load_settings(ctx);
}

DemoWorld::DemoWorld(DemoContext& ctx, const io::Recording& recording) : ctx_(ctx) {
    ctx.car_path = ctx.golden->recording;
    ctx.recording = &recording_;
    ctx.zone_draw = &zone_draw_;
    // Comprobados en read_golden_recording.
    std::string error;
    io::parse_world(recording.header.world, ctx.desc, error, recording.header.format_version);
    car_text_ = recording.header.car_text;
    ctx.car = sim::parse_car(car_text_, sim::CarKeys::Sim).car;
    ctx.desc.car_data = ctx.car;
    ctx.mode.zone = ctx.desc.test_zone;
    ctx.mode.blind_surface = ctx.desc.blind_surface;
    ctx.mode.road_surface = ctx.desc.road_surface;
    // Lo que se ve de los ajustes que van en la descripción (la caja en el indicador).
    ctx.settings.steer_assist = ctx.desc.aids.steer_assist;
    ctx.settings.sequential = ctx.desc.aids.gearbox == sim::GearboxMode::Sequential;
}

DemoWorld::~DemoWorld() {
    if (ctx_.settings_saver.dirty()) {
        save_settings(ctx_);
    }
    if (ctx_.world) {
        recording_.finish(*ctx_.world);
    }
}

void DemoWorld::load_settings(DemoContext& ctx) {
    settings_path_ = std::string(GetApplicationDirectory()) + std::string(kSettingsFileName);
    std::error_code missing;
    if (!std::filesystem::exists(utf8_path(settings_path_), missing)) {
        return; // se crea en el primer guardado
    }
    std::string text;
    std::string error;
    if (!io::read_file(settings_path_, text, error)) {
        ctx.overlay.set_message(Overlay::Message::Settings, "ajustes.txt: " + error, kError,
                                kSettingsMessageNs);
        return;
    }
    std::vector<SettingsWarning> warnings;
    ctx.settings = parse_settings(text, warnings);
    if (!warnings.empty()) {
        std::string message = "ajustes.txt:";
        for (const SettingsWarning& w : warnings) {
            message += " línea " + std::to_string(w.line) + ", " + w.message + ";";
        }
        message.pop_back();
        ctx.overlay.set_message(Overlay::Message::Settings, std::move(message), kError,
                                kSettingsMessageNs);
    }
}

void DemoWorld::save_settings(DemoContext& ctx) {
    std::string error;
    if (!io::write_file_atomic(utf8_path(settings_path_), format_settings(ctx.settings), error)) {
        ctx.overlay.set_message(Overlay::Message::Settings,
                                "ajustes.txt no se ha guardado: " + error, kError,
                                kSettingsMessageNs);
    }
    ctx.settings_saver.saved();
}

void DemoWorld::describe(DemoContext& ctx) const {
    const WorldMode& mode = ctx.mode;
    if (mode.zone) {
        // A ciegas, cada prueba sale en la pista ciega; si no, al principio de la recta.
        // Y: al principio de la recta o en la salida de la carretera de prueba.
        ctx.desc =
            mode.blind
                ? sim::zone_world(mode.blind_trial_surface, ctx.car, sim::ZoneStart::BlindTrack)
                : sim::zone_world(mode.blind_surface, ctx.car,
                                  mode.road_start ? sim::ZoneStart::Road : sim::ZoneStart::Straight,
                                  mode.road_surface);
    } else {
        ctx.desc = sim::demo_arena(mode.far_from_origin ? kFarOffset : 0.0, ctx.car);
    }
    // La ayuda de dirección y el modo de la caja cambian la simulación: van en la descripción
    // (y en la grabación).
    ctx.desc.aids.steer_assist = ctx.settings.steer_assist;
    ctx.desc.aids.gearbox =
        ctx.settings.sequential ? sim::GearboxMode::Sequential : sim::GearboxMode::Automatic;
}

void DemoWorld::reset(DemoContext& ctx) {
    if (ctx.world) {
        recording_.finish(*ctx.world);
    }
    ctx.world = std::make_unique<sim::SimWorld>();
    ctx.world->build(ctx.desc);
    ctx.world->seal();
    ctx.world->set_telemetry(true);
    if (ctx.world->zone() != nullptr) {
        zone_draw_.build(*ctx.world->zone(), ctx.mode.blind);
    }
    if (ctx.golden == nullptr) {
        recording_.start(ctx.desc, *ctx.world, car_text_, recording_settings_text(ctx.settings));
    }
    ctx.shift_up_presses = 0;
    ctx.shift_down_presses = 0;
    ctx.reset_presses = 0;
    read_poses(ctx);
    ctx.poses.previous = ctx.poses.current;
    ctx.poses.previous_car = ctx.poses.current_car;
    ctx.poses.previous_wheels = ctx.poses.current_wheels;
}

void DemoWorld::read_poses(DemoContext& ctx) {
    ScenePoses& p = ctx.poses;
    p.current.resize(static_cast<std::size_t>(ctx.world->body_count()));
    for (std::size_t i = 0; i < p.current.size(); ++i) {
        p.current[i] = to_pose(ctx.world->pose(static_cast<int>(i)));
    }
    p.current_car = to_pose(ctx.world->car_pose());
    for (int i = 0; i < sim::kWheelCount; ++i) {
        p.current_wheels[static_cast<std::size_t>(i)] = ctx.world->car_wheel(i);
    }
}

void DemoWorld::prepare_steps(DemoContext& ctx, int count) {
    if (!ctx.paused) {
        recording_.prepare(count);
    }
}

void DemoWorld::step(DemoContext& ctx, const sim::SimInput& input) {
    ctx.world->step(input);
    if (ctx.golden == nullptr) {
        recording_.record(input);
    }
    const std::uint16_t pressed = ctx.world->controls().pressed;
    ctx.shift_up_presses += (pressed & sim::kButtonShiftUp) != 0 ? 1 : 0;
    ctx.shift_down_presses += (pressed & sim::kButtonShiftDown) != 0 ? 1 : 0;
    ctx.reset_presses += (pressed & sim::kButtonReset) != 0 ? 1 : 0;
    ScenePoses& p = ctx.poses;
    p.previous.swap(p.current);
    p.previous_car = p.current_car;
    p.previous_wheels = p.current_wheels;
    read_poses(ctx);
}

void DemoWorld::handle_keys(DemoContext& ctx, FramePacer& /*pacer*/, PerfCounter& /*perf*/) {
    const std::uint8_t context = ctx.frame_key_context;
    if (action_pressed(action(WorldAction::ReloadCar), context)) {
        reload_car(ctx);
    }
    if (action_pressed(action(WorldAction::ResetWorld), context)) {
        ctx.rebuild = std::max(ctx.rebuild, Rebuild::Reset);
    }
    if (action_pressed(action(WorldAction::SteerAssist), context)) {
        // Cambia la simulación: mundo nuevo y otra grabación.
        ctx.settings.steer_assist = !ctx.settings.steer_assist;
        ctx.settings_saver.changed(now_ns());
        ctx.rebuild = Rebuild::Describe;
        ctx.overlay.set_message(Overlay::Message::Settings,
                                ctx.settings.steer_assist ? "F6: ayuda de dirección activada"
                                                          : "F6: ayuda de dirección desactivada",
                                kText, kShortMessageNs);
    }
    if (action_pressed(action(WorldAction::Gearbox), context)) {
        // Cambia la simulación: mundo nuevo y otra grabación.
        ctx.settings.sequential = !ctx.settings.sequential;
        ctx.settings_saver.changed(now_ns());
        ctx.rebuild = Rebuild::Describe;
        ctx.overlay.set_message(Overlay::Message::Settings,
                                ctx.settings.sequential ? "F2: caja secuencial (Q/E: bajar y subir)"
                                                        : "F2: caja automática",
                                kText, kShortMessageNs);
    }
    if (action_pressed(action(WorldAction::NextCar), context)) {
        next_car(ctx);
    }
    if (action_pressed(action(WorldAction::SteerDevice), context)) {
        // No es de la simulación: ni reconstruye el mundo ni cambia la grabación en curso.
        ctx.settings.steer_device = ctx.settings.steer_device == SteerDevice::Mouse
                                        ? SteerDevice::Keyboard
                                        : SteerDevice::Mouse;
        ctx.settings_saver.changed(now_ns());
        ctx.overlay.set_message(Overlay::Message::Settings,
                                ctx.settings.steer_device == SteerDevice::Mouse
                                    ? "F7: dirección con ratón (X o botón central: recentrar)"
                                    : "F7: dirección con teclado",
                                kText, kShortMessageNs);
    }
    // Guardado diferido de los ajustes: no en cada repetición de una tecla mantenida.
    if (ctx.settings_saver.due(now_ns())) {
        save_settings(ctx);
    }
}

void DemoWorld::reload_car(DemoContext& ctx) {
    if (load_car(ctx, ctx.car_path, "F5")) {
        ctx.overlay.set_message(Overlay::Message::Car,
                                ctx.mode.config_hidden()
                                    ? std::string("F5: coche recargado")
                                    : "F5: coche recargado de " + file_name(ctx.car_path) + " (" +
                                          std::to_string(static_cast<int>(ctx.car.mass)) + " kg)",
                                kText, kShortMessageNs);
    }
}

void DemoWorld::next_car(DemoContext& ctx) {
    const std::filesystem::path current = utf8_path(ctx.car_path);
    const std::u8string current_name = current.filename().u8string();
    std::size_t next = 0;
    for (std::size_t i = 0; i < kCarConfigs.size(); ++i) {
        if (std::string(current_name.begin(), current_name.end()) == kCarConfigs[i]) {
            next = (i + 1) % kCarConfigs.size();
        }
    }
    const std::u8string path =
        (current.parent_path() / std::filesystem::path(kCarConfigs[next])).u8string();
    if (load_car(ctx, std::string(path.begin(), path.end()), "F8")) {
        ctx.overlay.set_message(Overlay::Message::Car,
                                ctx.mode.config_hidden()
                                    ? std::string("F8: configuración siguiente")
                                    : "F8: configuración " + file_name(ctx.car_path),
                                kText, kShortMessageNs);
    }
}

bool DemoWorld::load_car(DemoContext& ctx, const std::string& path, const char* key) {
    std::string text;
    std::string error;
    if (!io::read_file(path, text, error)) {
        ctx.overlay.set_message(Overlay::Message::Car,
                                std::string(key) + ": no se puede leer " + path + ": " + error,
                                kError);
        return false;
    }
    const sim::CarParseResult parsed = sim::parse_car(text, sim::CarKeys::Full);
    if (!parsed.ok) {
        ctx.overlay.set_message(Overlay::Message::Car,
                                std::string(key) + ": " + file_name(path) + ", " + parsed.error +
                                    " (sigue el coche anterior)",
                                kError);
        return false;
    }
    // El coche nuevo sale parado donde lo dejaría recolocar (en el recinto, en su salida).
    const Vec3d at = ctx.poses.current_car.position;
    ctx.car_path = path;
    car_text_ = std::move(text);
    ctx.car = parsed.car;
    describe(ctx);
    if (const sim::TestZone* zone = ctx.world->zone(); zone != nullptr && !ctx.zone_bench) {
        const sim::RespawnPoint& r =
            zone->respawns()[static_cast<std::size_t>(zone->respawn_for(at.x, at.z))];
        ctx.desc.car_spawn.x = r.x;
        ctx.desc.car_spawn.y = zone->ground_height(r.x, r.z);
        ctx.desc.car_spawn.z = r.z;
        ctx.desc.car_spawn.yaw = static_cast<float>(r.yaw);
    }
    // Con la descripción ya hecha (la salida en el punto de recolocación): solo mundo nuevo.
    ctx.rebuild = std::max(ctx.rebuild, Rebuild::Reset);
    return true;
}

void DemoWorld::begin_frame(DemoContext& ctx, const DemoFrame& frame) {
    // En pausa se dibuja siempre el mismo estado: todos los fotogramas son idénticos.
    ctx.blend = ctx.interpolate && !ctx.paused;
    ScenePoses& p = ctx.poses;
    p.drawn.resize(p.current.size());
    for (std::size_t i = 0; i < p.current.size(); ++i) {
        p.drawn[i] =
            ctx.blend ? interpolate(p.previous[i], p.current[i], frame.steps.alpha) : p.current[i];
    }
    p.drawn_car =
        ctx.blend ? interpolate(p.previous_car, p.current_car, frame.steps.alpha) : p.current_car;
    p.drawn_wheels = p.current_wheels;
    if (ctx.blend) {
        for (std::size_t i = 0; i < p.drawn_wheels.size(); ++i) {
            p.drawn_wheels[i] =
                interpolate_wheel(p.previous_wheels[i], p.current_wheels[i], frame.steps.alpha);
        }
    }
    if (!recording_.error().empty()) {
        ctx.overlay.set_message(Overlay::Message::Recording,
                                "grabación: ERROR: " + recording_.error(), kError);
    }
}

void DemoWorld::draw_3d(DemoContext& ctx, const DemoFrame& /*frame*/) {
    const Vec3d& origin = ctx.frame_camera.origin;
    const ScenePoses& p = ctx.poses;
    if (ctx.mode.zone) {
        zone_draw_.draw(ctx.frame_camera.eye, origin);
        for (std::size_t i = 0; i < p.drawn.size(); ++i) {
            draw_box(p.drawn[i], ctx.desc.bodies[i], body_color(ctx.desc, i), origin);
        }
        draw_car(p.drawn_car, ctx.car, p.drawn_wheels, origin);
    } else {
        for (std::size_t i = 0; i < p.drawn.size(); ++i) {
            draw_box(p.drawn[i], ctx.desc.bodies[i], body_color(ctx.desc, i), origin);
            if (i == 0) {
                draw_floor_grid(ctx.desc.bodies[0], origin);
            }
        }
        draw_car(p.drawn_car, ctx.car, p.drawn_wheels, origin);
        draw_posts(ctx.desc.bodies[0], origin);
        // El bosque (demo_measure) va después, alrededor del recinto.
    }
}

} // namespace rally::platform
