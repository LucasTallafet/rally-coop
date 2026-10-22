#include "platform/demo_world.h"

#include "net/protocol.h"
#include "platform/camera_space.h"
#include "platform/demo_actions_keys.h"
#include "platform/demo_common.h"
#include "platform/demo_world_keys.h"
#include "platform/fixed_step_clock.h"
#include "platform/keys_raylib.h"
#include "platform/scene_draw.h"
#include "platform/wheel_spin.h"
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

// Configuraciones de prueba de la fase 2 (tarea 2.6), en el orden de F8.
constexpr std::array<std::string_view, 4> kCarConfigs = {
    "mid_awd_turbo.car", "front_awd_turbo_5cyl.car", "front_rwd_na.car",
    "front_awd_turbo_groupa.car"};

Pose to_pose(const sim::BodyPose& body) {
    Pose pose;
    pose.position = {body.x, body.y, body.z};
    pose.rotation = {body.qx, body.qy, body.qz, body.qw};
    return pose;
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

std::string find_data_file(const std::string& relative) {
    std::error_code error;
    std::filesystem::path folder = std::filesystem::path(GetApplicationDirectory());
    for (int up = 0; up < 8 && !folder.empty(); ++up) {
        const std::filesystem::path candidate = folder / utf8_path(relative);
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

std::string default_car_path() { return find_data_file("data/cars/mid_awd_turbo.car"); }

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
    // Acciones del copiloto (tarea 2.9): data/game/actions.txt, buscado como el coche. Si no está,
    // el juego sigue sin acciones (las teclas 7, 8 y 9 lo dicen); si tiene un error, aborta con su
    // línea.
    const std::string actions_path = find_data_file("data/game/actions.txt");
    std::string actions_text;
    if (!actions_path.empty() && io::read_file(actions_path, actions_text, error)) {
        actions_.load(actions_text);
    }
    ctx.actions = &actions_;
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
    ctx.settings.abs = ctx.desc.aids.abs;
    ctx.settings.traction = ctx.desc.aids.traction;
    ctx.settings.suspension = ctx.desc.aids.suspension;
}

DemoWorld::~DemoWorld() {
    if (ctx_.settings_saver.dirty()) {
        save_settings(ctx_);
    }
    if (ctx_.world) {
        recording_.finish(*ctx_.world, actions_.timeline_hash());
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
    if (!save_settings_file(settings_path_, ctx.settings, InstanceRole::Host, error)) {
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
    // Las ayudas (dirección, caja, ABS, control de tracción y suspensión) cambian la simulación:
    // van en la descripción (y en la grabación).
    ctx.desc.aids = driver_aids(ctx.settings);
}

void DemoWorld::reset(DemoContext& ctx) {
    if (ctx.world) {
        recording_.finish(*ctx.world, actions_.timeline_hash());
    }
    ctx.world = std::make_unique<sim::SimWorld>();
    ctx.world->build(ctx.desc);
    ctx.world->seal();
    ctx.world->set_telemetry(true);
    // La máquina de acciones empieza con el mundo y con su grabación (paso 0 de las dos).
    actions_.start(*ctx.world);
    if (ctx.world->zone() != nullptr) {
        zone_draw_.build(*ctx.world->zone(), ctx.mode.blind);
    }
    if (ctx.golden == nullptr) {
        recording_.start(ctx.desc, *ctx.world, car_text_, recording_settings_text(ctx.settings),
                         actions_.text());
    }
    ctx.shift_up_presses = 0;
    ctx.shift_down_presses = 0;
    ctx.reset_presses = 0;
    reset_presses_seen_ = 0;
    publish_world(ctx);
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
        actions_.advance(*ctx.world);
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
    // Sensibilidad del stick (tarea 2.8): no es de la simulación, ni reconstruye el mundo ni
    // cambia la grabación en curso (su cabecera guarda el valor con el que empezó).
    for (const int dir : {-1, 1}) {
        const ActionRef key =
            action(dir < 0 ? WorldAction::SensitivityDown : WorldAction::SensitivityUp);
        if (!action_pressed_repeat(key, context)) {
            continue;
        }
        ctx.settings.steer_sensitivity_pct =
            next_sensitivity_pct(ctx.settings.steer_sensitivity_pct, dir);
        ctx.settings_saver.changed(now_ns());
        ctx.overlay.set_message(Overlay::Message::Settings,
                                "4/5: sensibilidad del stick " +
                                    std::to_string(ctx.settings.steer_sensitivity_pct) + " %",
                                kText, kShortMessageNs);
    }
    handle_aid_keys(ctx, context);
    handle_action_keys(ctx, context);
    // Guardado diferido de los ajustes: no en cada repetición de una tecla mantenida.
    if (ctx.settings_saver.due(now_ns())) {
        save_settings(ctx);
    }
}

void DemoWorld::handle_aid_keys(DemoContext& ctx, std::uint8_t context) {
    // ABS, control de tracción y suspensión (tarea 2.8): cambian la simulación, así que como F6
    // y F2 reconstruyen el mundo y empiezan otra grabación.
    const auto changed = [&ctx](std::string message) {
        ctx.settings_saver.changed(now_ns());
        ctx.rebuild = Rebuild::Describe;
        ctx.overlay.set_message(Overlay::Message::Settings, std::move(message), kText,
                                kShortMessageNs);
    };
    if (action_pressed(action(WorldAction::Abs), context)) {
        ctx.settings.abs = !ctx.settings.abs;
        changed(ctx.settings.abs ? "1: ABS activado" : "1: ABS desactivado");
    }
    if (action_pressed(action(WorldAction::TractionControl), context)) {
        ctx.settings.traction = next_traction(ctx.settings.traction);
        changed("2: control de tracción " + std::string(traction_name(ctx.settings.traction)));
    }
    if (action_pressed(action(WorldAction::Suspension), context)) {
        ctx.settings.suspension = next_suspension(ctx.settings.suspension);
        changed("3: suspensión " + std::string(suspension_name(ctx.settings.suspension)));
    }
}

void DemoWorld::request_action(DemoContext& ctx, const char* key, std::string_view name) {
    const std::string prefix = std::string(key) + ": ";
    const int index = actions_.table().find(name);
    if (!actions_.enabled() || index < 0) {
        ctx.overlay.set_message(Overlay::Message::Action,
                                prefix + "sin acciones (data/game/actions.txt)", kError,
                                kShortMessageNs);
        return;
    }
    const game::ApplyResult result = actions_.submit(
        ctx.golden == nullptr ? recording_.recorder() : nullptr, game::Player::Copilot,
        game::EventKind::Request, static_cast<std::uint8_t>(index));
    const game::ActionDef& def = actions_.table().actions[static_cast<std::size_t>(index)];
    if (result.ok()) {
        ctx.overlay.set_message(Overlay::Message::Action,
                                prefix + def.name + ", " + duration_text(result.end - result.start),
                                kText, kShortMessageNs);
    } else {
        ctx.overlay.set_message(Overlay::Message::Action,
                                prefix + def.name + ": " + reject_text(result.reject, &def), kError,
                                kShortMessageNs);
    }
}

void DemoWorld::handle_action_keys(DemoContext& ctx, std::uint8_t context) {
    // Teclas de depuración que hacen de copiloto hasta que haya tablet (fase 3).
    if (action_pressed(action(ActionsAction::StowTablet), context)) {
        request_action(ctx, "7", "guardar_tablet");
    }
    if (action_pressed(action(ActionsAction::TakeTablet), context)) {
        request_action(ctx, "8", "sacar_tablet");
    }
    if (action_pressed(action(ActionsAction::CancelAction), context)) {
        const game::ActionProgress progress = actions_.copilot_progress();
        if (!progress.active) {
            ctx.overlay.set_message(Overlay::Message::Action,
                                    "9: no hay ninguna acción que cancelar", kText,
                                    kShortMessageNs);
        } else {
            // Si la cancelación se rechazara, la dice; si se acepta, el aviso llega con la
            // transición (after_steps).
            const game::ApplyResult result =
                actions_.submit(ctx.golden == nullptr ? recording_.recorder() : nullptr,
                                game::Player::Copilot, game::EventKind::Cancel, progress.action);
            if (!result.ok()) {
                ctx.overlay.set_message(Overlay::Message::Action,
                                        "9: " + reject_text(result.reject, nullptr), kError,
                                        kShortMessageNs);
            }
        }
    }
}

void DemoWorld::start_host(DemoContext& ctx) {
    host_ = std::make_unique<net::Host>();
    std::string error;
    if (!host_->start(net::kDefaultPort, error)) {
        // Otra instancia ya hace de host: el juego sigue igual, sin copiloto.
        host_.reset();
        ctx.overlay.set_message(Overlay::Message::Network,
                                "red: puerto " + std::to_string(net::kDefaultPort) +
                                    " ocupado; sin copiloto",
                                kError, kSettingsMessageNs);
        return;
    }
    ctx.network.host = true;
    publish_world(ctx);
}

void DemoWorld::publish_world(DemoContext& ctx) {
    // A ciegas no se manda nada: la ventana del copiloto enseñaría la superficie. El copiloto se
    // queda con el último mundo; al salir del modo, el mundo nuevo se publica aquí.
    if (host_ && !ctx.mode.blind) {
        host_->set_world(io::serialize_world(ctx.desc), car_text_, ctx.world->zone_hash());
    }
}

void DemoWorld::update_network(DemoContext& ctx) {
    NetworkInfo& info = ctx.network;
    const std::int64_t begin = now_ns();
    host_->service();
    const std::int64_t serviced = now_ns();
    const bool connected = host_->copilot_connected();
    if (connected != copilot_connected_) {
        copilot_connected_ = connected;
        ctx.overlay.set_message(Overlay::Message::Network,
                                connected ? "red: copiloto conectado"
                                          : "red: copiloto desconectado",
                                kText, kShortMessageNs);
    }
    // Un paso nuevo se manda solo si el mundo se ha publicado: la telemetría ya cuenta los pasos
    // desde el build y el botón de recolocar se ha visto en alguno de ellos desde el último envío.
    if (!ctx.mode.blind) {
        const bool reset = ctx.reset_presses != reset_presses_seen_;
        reset_presses_seen_ = ctx.reset_presses;
        host_->after_steps(*ctx.world, static_cast<std::uint32_t>(ctx.world->telemetry().step),
                           reset);
    }
    const std::int64_t end = now_ns();
    info.copilot = connected;
    info.blind_hold = connected && ctx.mode.blind;
    if (connected) {
        const net::LinkStats stats = host_->stats();
        info.rtt_ms = stats.rtt_s * 1000.0;
        info.sent = stats.sent;
        info.cpu.add(end, end - begin);
        info.service_cpu.add(end, serviced - begin);
        if (end - net_published_ns_ >= PerfStats::kPeriodNs) {
            info.cpu.publish(end);
            info.service_cpu.publish(end);
            net_published_ns_ = end;
        }
    }
}

void DemoWorld::after_steps(DemoContext& ctx, Gamepads& /*pads*/, float /*frame_impact*/,
                            std::int64_t /*frame_ns*/) {
    if (host_) {
        update_network(ctx);
    }
    // Los cambios de la máquina que se dicen en pantalla (cancelaciones y efecto sobre la
    // tablet), fuera del paso.
    std::array<game::Transition, 8> pending;
    for (std::size_t n = actions_.drain(pending); n > 0; n = actions_.drain(pending)) {
        for (std::size_t i = 0; i < n; ++i) {
            std::string text = transition_text(pending[i], actions_.table());
            if (!text.empty()) {
                const bool cancelled = pending[i].kind == game::TransitionKind::Cancelled;
                ctx.overlay.set_message(Overlay::Message::Action, std::move(text),
                                        cancelled ? kError : kText, kShortMessageNs);
            }
        }
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
