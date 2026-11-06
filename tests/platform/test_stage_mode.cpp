// Tarea tramo-modo (lote 5): el tramo en el juego y en el copiloto, sin ventana.

#include "car_worlds.h"
#include "io/recording.h"
#include "io/replay.h"
#include "net/protocol.h"
#include "platform/copilot_world.h"
#include "platform/recording_header.h"
#include "platform/screen_layout.h"
#include "platform/stage_hud.h"
#include "platform/stage_mode.h"
#include "platform/stage_overview.h"
#include "platform/ui_text.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_world.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

using namespace rally;
using namespace rally::platform;

namespace {

const std::string& lazadas_text() {
    static const std::string text = [] {
        std::string bytes;
        std::string error;
        REQUIRE_MESSAGE(io::read_file(RALLY_SOURCE_DIR "/data/stages/lazadas.stage", bytes, error),
                        error);
        return bytes;
    }();
    return text;
}

constexpr std::array kResolutions = {std::pair{1280, 720}, std::pair{1360, 768},
                                     std::pair{1920, 1080}};

sim::StageRunState fresh_run() {
    sim::StageRunState run{};
    run.start_step = -1;
    run.finish_step = -1;
    run.on_road = true;
    return run;
}

} // namespace

TEST_CASE("stage mode: K cycles zone, stage and arena, and stays put without a readable stage") {
    CHECK(cycle_world_kind(WorldKind::Zone, true) == WorldKind::Stage);
    CHECK(cycle_world_kind(WorldKind::Stage, true) == WorldKind::Arena);
    CHECK(cycle_world_kind(WorldKind::Arena, true) == WorldKind::Zone);
    // Sin tramo que cargar: desde la zona se queda; las otras dos no lo necesitan.
    CHECK(cycle_world_kind(WorldKind::Zone, false) == WorldKind::Zone);
    CHECK(cycle_world_kind(WorldKind::Stage, false) == WorldKind::Arena);
    CHECK(cycle_world_kind(WorldKind::Arena, false) == WorldKind::Zone);
    // Una vuelta entera vuelve al principio.
    WorldKind kind = WorldKind::Zone;
    for (int i = 0; i < 3; ++i) {
        kind = cycle_world_kind(kind, true);
    }
    CHECK(kind == WorldKind::Zone);
}

TEST_CASE("stage mode: a stage that does not load keeps the mode and says its line") {
    StageSelection selection;
    CHECK_FALSE(selection.loaded());
    StageFile good;
    good.path = "D:/juego/data/stages/lazadas.stage";
    good.text = lazadas_text();
    // Zona → tramo con un archivo bueno: cambia, quita el modo a ciegas y guarda el tramo.
    const WorldStep entered = step_world_kind(WorldKind::Zone, selection, good);
    CHECK(entered.changed);
    CHECK(entered.kind == WorldKind::Stage);
    CHECK(entered.clear_blind);
    CHECK(entered.message.empty());
    CHECK(selection.loaded());
    CHECK(selection.path == good.path);
    CHECK(selection.name == "Lazadas (test)");
    CHECK(selection.length == doctest::Approx(1150.0));

    // El tramo no se lee de nuevo para pasar a recinto ni de recinto a zona.
    const StageFile unused;
    const WorldStep arena = step_world_kind(WorldKind::Stage, selection, unused);
    CHECK(arena.changed);
    CHECK(arena.kind == WorldKind::Arena);
    CHECK_FALSE(arena.clear_blind);
    const WorldStep zone = step_world_kind(WorldKind::Arena, selection, unused);
    CHECK(zone.changed);
    CHECK(zone.kind == WorldKind::Zone);

    // Un texto roto en una línea con datos (la 31 es «straight 100»): se queda en la zona, con
    // su línea y el nombre del archivo, y la selección como estaba.
    StageFile broken = good;
    broken.path = "mi_tramo.stage";
    const std::size_t at = broken.text.find("straight 100");
    REQUIRE(at != std::string::npos);
    broken.text.replace(at, 12, "straight -100");
    const WorldStep refused = step_world_kind(WorldKind::Zone, selection, broken);
    CHECK_FALSE(refused.changed);
    CHECK(refused.kind == WorldKind::Zone);
    CHECK(refused.message.rfind("tramo: mi_tramo.stage, línea ", 0) == 0);
    CHECK(selection.text == lazadas_text());
    CHECK(selection.path == good.path);

    // Un archivo que no se lee: se queda y el mensaje lleva la ruta y el motivo.
    StageFile unreadable;
    unreadable.path = "C:/no/existe.stage";
    unreadable.error = "no existe";
    const WorldStep missing = step_world_kind(WorldKind::Zone, selection, unreadable);
    CHECK_FALSE(missing.changed);
    CHECK(missing.message == "tramo: no se puede leer C:/no/existe.stage: no existe");
    // Sin archivo (no se encontró lazadas.stage): tampoco cambia.
    const WorldStep none = step_world_kind(WorldKind::Zone, selection, StageFile{});
    CHECK_FALSE(none.changed);
    CHECK(none.message.rfind("tramo: no se encuentra data/stages/lazadas.stage", 0) == 0);
    // Un archivo vacío no vale.
    StageFile empty;
    empty.path = "vacio.stage";
    const WorldStep blank = step_world_kind(WorldKind::Zone, selection, empty);
    CHECK_FALSE(blank.changed);
    CHECK_FALSE(blank.message.empty());
    CHECK(selection.text == lazadas_text());
}

TEST_CASE("stage mode: the description puts the car 5 m before the line, stopped, without a zone") {
    const sim::JoltRuntime runtime;
    StageSelection selection;
    std::string error;
    REQUIRE_MESSAGE(selection.set_text(lazadas_text(), error), error);
    sim::DriverAids aids;
    aids.abs = false;
    const sim::WorldDesc desc = stage_world_desc(selection, tests::reference_car(), aids);
    CHECK(desc.stage_text == lazadas_text());
    CHECK_FALSE(desc.test_zone);
    CHECK(desc.car);
    CHECK_FALSE(desc.aids.abs);
    const sim::CarSpawn expected = sim::stage_spawn(selection.track, -5.0);
    CHECK(kGameStageSpawnS == -5.0);
    CHECK(desc.car_spawn.x == expected.x);
    CHECK(desc.car_spawn.y == expected.y);
    CHECK(desc.car_spawn.z == expected.z);
    CHECK(desc.car_spawn.yaw == expected.yaw);
    CHECK(desc.car_spawn.speed == 0.0f);

    sim::SimWorld world;
    world.build(desc);
    world.seal();
    REQUIRE(world.stage() != nullptr);
    CHECK(world.zone() == nullptr);
    CHECK(world.stage_ground_hash() != 0);
    REQUIRE(world.stage_run() != nullptr);
    CHECK(world.stage_run()->start_step == -1);
}

TEST_CASE("stage mode: a game session on the stage records a format 8 file that replays") {
    const sim::JoltRuntime runtime;
    StageSelection selection;
    std::string error;
    REQUIRE_MESSAGE(selection.set_text(lazadas_text(), error), error);
    const sim::WorldDesc desc = stage_world_desc(selection, tests::reference_car(), {});
    sim::SimWorld world;
    world.build(desc);
    world.seal();
    world.set_telemetry(true);

    // Lo que hace RecordingSession (cabecera y pie) con las entradas de un test: acelerador a
    // fondo y una recolocación en medio.
    const io::RecordingHeader header =
        make_recording_header(desc, world, tests::reference_car_text(), "", "");
    CHECK(header.stage_ground_hash == world.stage_ground_hash());
    CHECK(header.stage_ground_hash != 0);
    CHECK(header.zone_hash == 0);
    constexpr int kSteps = 1500;
    constexpr int kResetStep = 700;
    io::Recorder recorder;
    recorder.begin(header, 16);
    recorder.ensure_capacity(kSteps);
    for (int i = 0; i < kSteps; ++i) {
        sim::SimInput input;
        input.throttle = sim::kAxisMax;
        input.buttons = i == kResetStep ? sim::kButtonReset : 0;
        world.step(input);
        recorder.append(input);
    }
    const io::StageResult result = io::stage_result(world);
    CHECK(result.resets == 1);
    const io::Recording recording = finish_recording(recorder, world, 0);
    CHECK(recording.footer.stage == result);

    const io::ReplayResult replayed = io::replay(recording);
    REQUIRE_MESSAGE(replayed.ran, replayed.problem);
    CHECK(replayed.problem.empty());
    CHECK_FALSE(replayed.stage_mismatch);
    CHECK(replayed.hash_ok);
    CHECK(replayed.stage == result);
    CHECK(replayed.stage.resets == 1);

    // La grabación sobrevive a serializar y leer.
    const io::ParseResult parsed = io::parse(io::serialize(recording));
    REQUIRE_MESSAGE(parsed.ok, parsed.error);
    CHECK(parsed.recording.header.stage_ground_hash == header.stage_ground_hash);
    CHECK(parsed.recording.footer.stage == result);
}

TEST_CASE("stage hud: the clock truncates to the hundredth, like rally_replay info") {
    CHECK(clock_text(0) == "0:00,00");
    // 59,999 s son 14399,76 pasos: el último paso entero da 59,99 s y nunca 1:00,00.
    CHECK(clock_text(14399) == "0:59,99");
    CHECK(clock_text(14400) == "1:00,00");
    CHECK(clock_text(240) == "0:01,00");
    CHECK(clock_text(99 * 60 * 240 + 59 * 240 + 239) == "99:59,99");
    CHECK(clock_text(14640) == "1:01,00");   // 61 s
    CHECK(clock_text(144000) == "10:00,00"); // 10 min
    // La pasada de stage_lazadas.rrec: 21345 pasos = 88,9375 s.
    CHECK(clock_text(21345) == "1:28,93");
    CHECK(clock_text(-1) == "--");
    // Cada centésima llega a su paso: 0,01 s = 2,4 pasos.
    CHECK(clock_text(2) == "0:00,00");
    CHECK(clock_text(3) == "0:00,01");
}

TEST_CASE("stage hud: the clock runs from the start, stops at the finish and ignores the world") {
    sim::StageRunState run = fresh_run();
    run.steps = 5000;
    CHECK(run_clock_steps(run) == 0); // antes de la salida
    CHECK(stage_indicator_text("Lazadas (test)", run) == "tramo Lazadas (test) · 0:00,00");
    run.start_step = 1200;
    CHECK(run_clock_steps(run) == 3800);
    CHECK(stage_indicator_text("Lazadas (test)", run) == "tramo Lazadas (test) · 0:15,83");
    run.finish_step = 22545;
    run.steps = 30000;
    run.resets = 3;
    run.shortcuts = 1;
    CHECK(run_clock_steps(run) == 21345);
    CHECK(stage_indicator_text("Lazadas (test)", run) == "meta 1:28,93 · recol. 3 · atajos 1");
}

TEST_CASE("stage hud: a long name is cut with an ellipsis and no half character") {
    CHECK(short_stage_name("Lazadas") == "Lazadas");
    CHECK(short_stage_name("Lazadas (test)") == "Lazadas (test)");
    CHECK(short_stage_name("8888888888888888") == "8888888888888888"); // 16 cifras caben
    const std::string cut = short_stage_name("88888888888888888");
    CHECK(cut.size() > 3);
    CHECK(cut.compare(cut.size() - 3, 3, "...") == 0);
    // Con letras de dos bytes nunca queda media: sigue siendo UTF-8 válido de la fuente.
    const std::string accents = short_stage_name("ñññññññññññññññññññññññ");
    CHECK(accents.compare(accents.size() - 3, 3, "...") == 0);
    CHECK(in_default_font(accents));
    const int allowed = text_width(std::string(16, '8'), 10) + text_width("...", 10);
    CHECK(text_width(accents, 10) <= allowed);
}

TEST_CASE("stage hud: the F3 lines have the contract's fields") {
    sim::StageRunState run = fresh_run();
    run.s = 123.46;
    run.t = -1.26;
    run.reset_s = 100.0;
    run.resets = 3;
    run.shortcuts = 1;
    CHECK(stage_debug_line(run, 1150.0) == "tramo: s 123.5 / 1150 m, t -1.3 m, en carretera");
    CHECK(stage_debug_counts_line(run) == "recolocación en s = 100.0 m · recol. 3 · atajos 1");
    run.on_road = false;
    CHECK(stage_debug_line(run, 1150.0) == "tramo: s 123.5 / 1150 m, t -1.3 m, fuera");
}

TEST_CASE("stage hud: counter changes give one message each, never none") {
    StageEvents events;
    sim::StageRunState run = fresh_run();
    CHECK(events.observe(run).empty());
    // Dos recolocaciones en el mismo fotograma: un mensaje que dice 2.
    run.resets = 2;
    run.reset_s = 640.4;
    CHECK(events.observe(run).events == "2 recolocaciones, la última en s = 640 m");
    CHECK(events.observe(run).empty()); // sin cambios, nada
    run.resets = 3;
    CHECK(events.observe(run).events == "recolocación en s = 640 m");
    // Un atajo y la meta en el mismo fotograma: uno cada uno, cada cual en su hueco.
    run.shortcuts = 1;
    run.exit_s = 812.0;
    run.start_step = 100;
    run.finish_step = 21445;
    const StageMessages both = events.observe(run);
    CHECK(both.events == "atajo: recolocado en s = 812 m");
    CHECK(both.finish == "meta: 1:28,93 · recol. 3 · atajos 1");
    CHECK(events.observe(run).empty()); // la meta se dice una sola vez
    // Parciales (lazadas no tiene: el primero iría en s = 1000 > 1150 - 200; un tramo largo, sí).
    StageEvents splits;
    sim::StageRunState lap = fresh_run();
    lap.start_step = 240;
    lap.split_count = 1;
    lap.split_steps[0] = 240 + 14640;
    CHECK(splits.observe(lap).events == "parcial 1: 1:01,00");
    lap.split_count = 3;
    lap.split_steps[1] = 240 + 2 * 14640;
    lap.split_steps[2] = 240 + 3 * 14640;
    CHECK(splits.observe(lap).events == "parcial 2: 2:02,00 · parcial 3: 3:03,00");
    // Mundo nuevo: los contadores vuelven a cero.
    splits.reset();
    sim::StageRunState again = fresh_run();
    again.resets = 1;
    again.reset_s = 5.0;
    CHECK(splits.observe(again).events == "recolocación en s = 5 m");
    // Un contador que baja sin reset() (otro mundo): se empieza de nuevo, sin mensajes ni
    // fantasmas, y el siguiente suceso se cuenta desde cero.
    StageEvents stale;
    sim::StageRunState old_world = fresh_run();
    old_world.resets = 4;
    stale.observe(old_world);
    sim::StageRunState new_world = fresh_run();
    CHECK(stale.observe(new_world).empty());
    new_world.resets = 1;
    CHECK(stale.observe(new_world).events == "recolocación en s = 0 m");
}

TEST_CASE("stage hud: every text stays inside its layer at the three resolutions") {
    // Peor caso de la pasada: cifras de 8, 99 minutos, contadores de 3 cifras y el nombre más
    // largo (se corta con «...»).
    sim::StageRunState run = fresh_run();
    run.start_step = 0;
    run.steps = 99 * 60 * 240 + 59 * 240;
    run.s = 8888.8;
    run.t = -88.8;
    run.reset_s = 8888.8;
    run.resets = 99;
    run.shortcuts = 99;
    const std::string name(60, 'W');
    const std::string running = stage_indicator_text(name, run);
    run.finish_step = run.steps;
    const std::string finished = stage_indicator_text(name, run);
    const std::string debug = stage_debug_line(run, 88888.0);
    const std::string counts = stage_debug_counts_line(run);
    sim::StageRunState worst = run;
    worst.split_count = 1;
    worst.split_steps[0] = worst.steps;
    StageEvents events;
    const StageMessages messages = events.observe(worst);
    for (const auto& res : kResolutions) {
        const ScreenLayout layout = layout_screen(res.first, res.second);
        INFO(res.first, "x", res.second);
        // Indicador: la línea entera, con la velocidad, la marcha, las rpm, el turbo, el coche y el
        // volante.
        const std::string base =
            hud_base_text(88.8, "R>R", true, 8888.0, 8.88e5, ""); // el tramo no dice el coche
        for (const std::string& stage : {running, finished}) {
            const std::string line = base + "   " + stage + "   volante [---------O---------]";
            INFO(line);
            CHECK(text_width(line, layout.m.big_font) <=
                  layer_content_width(Layer::Indicator, layout));
            CHECK(in_default_font(line));
        }
        // Mensajes: se recortan con «...» al dibujar, pero el peor caso cabe entero.
        for (const std::string& message : {messages.events, messages.finish}) {
            INFO(message);
            CHECK(text_width(message, layout.m.font) <= layout.rect(Layer::Messages).w);
            CHECK(in_default_font(message));
        }
        // F3: la línea del tramo cabe en el contenido del panel.
        const int content = layout.rect(Layer::Debug).w - 2 * layout.m.padding;
        for (const std::string& line : {debug, counts}) {
            INFO(line);
            CHECK(debug_text_width(line, layout.m.font) <= content);
            CHECK(in_default_font(line));
        }
    }
    // La línea real no es más ancha que la muestra de peor caso de F3.
    for (const char* start : {"tramo: s", "recolocación en"}) {
        const std::string& real = start[0] == 't' ? debug : counts;
        int sample_width = 0;
        for (const std::string_view sample : layer_samples(Layer::Debug)) {
            if (sample.rfind(start, 0) == 0) {
                sample_width = debug_text_width(sample, 20);
            }
        }
        CHECK(sample_width > 0);
        CHECK(debug_text_width(real, 20) <= sample_width);
    }
}

TEST_CASE("stage mode: the copilot builds the stage from the host's world and compares the hash") {
    const sim::JoltRuntime runtime;
    std::string car_text;
    std::string error;
    REQUIRE(io::read_file(RALLY_SOURCE_DIR "/data/cars/mid_awd_turbo.car", car_text, error));
    const sim::CarData car =
        sim::load_car_or_abort(car_text, "mid_awd_turbo.car", sim::CarKeys::Full);
    StageSelection selection;
    REQUIRE_MESSAGE(selection.set_text(lazadas_text(), error), error);

    net::WorldInfo info;
    info.generation = 3;
    info.world_bytes = io::serialize_world(stage_world_desc(selection, car, {}));
    info.car_text = car_text;
    // Primero con un hash que no es el suyo: lo construye y dice «distinto».
    info.stage_ground_hash = 0x1234;
    CopilotWorld different = build_copilot_world(info);
    REQUIRE_MESSAGE(different.ok, different.error);
    CHECK(different.has_stage());
    CHECK_FALSE(different.stage_matches);
    CHECK(different.zone_matches);
    const std::uint64_t host_hash = different.world->stage_ground_hash();
    CHECK(host_hash != 0);
    CHECK(different.desc.stage_text == lazadas_text());
    // Con el hash que calcula el host (el mismo suelo): coincide.
    info.stage_ground_hash = host_hash;
    const CopilotWorld same = build_copilot_world(info);
    REQUIRE_MESSAGE(same.ok, same.error);
    CHECK(same.has_stage());
    CHECK(same.stage_matches);
    CHECK(same.world->stage_ground_hash() == host_hash);

    // Un mundo roto o un coche roto dicen cuál.
    net::WorldInfo bad = info;
    bad.world_bytes = "no es un mundo";
    const CopilotWorld no_world = build_copilot_world(bad);
    CHECK_FALSE(no_world.ok);
    CHECK(no_world.error.rfind("mundo del conductor no válido: ", 0) == 0);
    bad = info;
    bad.car_text = "no es un coche";
    const CopilotWorld no_car = build_copilot_world(bad);
    CHECK_FALSE(no_car.ok);
    CHECK(no_car.error.rfind("coche del conductor no válido: ", 0) == 0);
}

TEST_CASE("stage overview: the whole stage fits the screen and the far plane reaches it") {
    StageSelection selection;
    std::string error;
    REQUIRE_MESSAGE(selection.set_text(lazadas_text(), error), error);
    constexpr double kFov = 45.0;
    // Las proporciones de la ventana: 1280x720, 1360x768, 16:10 y 4:3.
    for (const double kAspect : {1280.0 / 720.0, 1360.0 / 768.0, 16.0 / 10.0, 4.0 / 3.0}) {
        INFO("aspecto ", kAspect);
        const StageOverview view = stage_overview(selection.track, kFov, kAspect);
        const double forward_length =
            std::sqrt(view.forward.x * view.forward.x + view.forward.y * view.forward.y +
                      view.forward.z * view.forward.z);
        const Vec3d fwd{view.forward.x / forward_length, view.forward.y / forward_length,
                        view.forward.z / forward_length};
        const Vec3d right_raw{fwd.y * 0.0 - fwd.z * 1.0, fwd.z * 0.0 - fwd.x * 0.0,
                              fwd.x * 1.0 - fwd.y * 0.0};
        const double right_length =
            std::sqrt(right_raw.x * right_raw.x + right_raw.z * right_raw.z);
        const Vec3d right{right_raw.x / right_length, 0.0, right_raw.z / right_length};
        const Vec3d up{right.y * fwd.z - right.z * fwd.y, right.z * fwd.x - right.x * fwd.z,
                       right.x * fwd.y - right.y * fwd.x};
        const double tan_v = std::tan(kFov * 0.5 * 3.14159265358979323846 / 180.0);
        const double tan_h = tan_v * kAspect;
        int checked = 0;
        const sim::StageTrack& track = selection.track;
        for (double s = track.s_begin(); s <= track.s_end(); s += 10.0, ++checked) {
            const sim::StagePose p = track.pose(s);
            const Vec3d rel{p.x - view.eye.x, p.y - view.eye.y, p.z - view.eye.z};
            const double depth = rel.x * fwd.x + rel.y * fwd.y + rel.z * fwd.z;
            INFO("s = ", s);
            REQUIRE(depth > 0.0);
            CHECK(std::fabs(rel.x * right.x + rel.z * right.z) / depth <= tan_h);
            CHECK(std::fabs(rel.x * up.x + rel.y * up.y + rel.z * up.z) / depth <= tan_v);
        }
        CHECK(checked > 100);
        // El plano lejano de la cámara del juego es de 2000 m (DemoCamera::kFarPlane).
        CHECK(view.far_distance < 2000.0);
        CHECK(view.draw_distance >= 600.0);
    }
}

TEST_CASE("stage mode: the reference capture's step has the run going") {
    // tramo_salida.png se saca en el paso 1200 de stage_lazadas.rrec: el indicador enseña el reloj
    // en marcha, ni antes de la salida ni tras la meta.
    const sim::JoltRuntime runtime;
    std::string bytes;
    std::string error;
    REQUIRE_MESSAGE(
        io::read_file(RALLY_TEST_DATA_DIR "/recordings/stage_lazadas.rrec", bytes, error), error);
    const io::ParseResult parsed = io::parse(bytes);
    REQUIRE_MESSAGE(parsed.ok, parsed.error);
    sim::SimWorld world;
    REQUIRE_MESSAGE(io::build_world(parsed.recording, world, error), error);
    constexpr std::uint64_t kCaptureStep = 1200;
    std::uint64_t done = 0;
    for (const io::InputRun& run : parsed.recording.runs) {
        for (std::uint32_t i = 0; i < run.count && done < kCaptureStep; ++i, ++done) {
            world.step(run.input);
        }
    }
    REQUIRE(done == kCaptureStep);
    const sim::StageRunState& state = *world.stage_run();
    CHECK(state.start_step >= 0);
    CHECK(state.start_step < state.steps);
    CHECK(state.finish_step < 0);
    CHECK(run_clock_steps(state) > 0);
}
