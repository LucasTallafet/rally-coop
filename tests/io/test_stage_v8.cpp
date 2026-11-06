#include "car_worlds.h"
#include "io/recording.h"
#include "io/replay.h"
#include "io/telemetry_csv.h"
#include "new_counter.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"
#include "sim/stage_track.h"
#include "stage_driver.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Formato 8 del .rrec (lote 5, grabacion-v8): el texto del tramo en el mundo, el hash del suelo
// en la cabecera y la pasada en el pie; las seis columnas del tramo del CSV.

using rally::io::InputRun;
using rally::io::Recorder;
using rally::io::Recording;
using rally::io::StageResult;
using rally::sim::SimInput;
using rally::sim::SimWorld;
using rally::sim::StageRunState;
using rally::sim::StageTrack;
using rally::sim::WorldDesc;

namespace {

constexpr const char* kStageRecording = RALLY_TEST_DATA_DIR "/recordings/stage_lazadas.rrec";
// Paso del mundo en que el generador pulsa recolocar (a mitad de la pasada, por carretera).
constexpr int kResetStep = 9000;

const std::string& lazadas_text() {
    static const std::string text = [] {
        std::string bytes;
        std::string error;
        REQUIRE_MESSAGE(
            rally::io::read_file(RALLY_SOURCE_DIR "/data/stages/lazadas.stage", bytes, error),
            error);
        return bytes;
    }();
    return text;
}

StageTrack lazadas_track() {
    StageTrack track;
    const rally::sim::StageParseResult parsed = StageTrack::build(lazadas_text(), track);
    REQUIRE_MESSAGE(parsed.ok, parsed.error);
    return track;
}

// El mundo de la grabación de test: lazadas, el 205 y la salida 5 m antes de la línea.
WorldDesc lazadas_desc() {
    WorldDesc desc;
    desc.stage_text = lazadas_text();
    desc.car = true;
    desc.car_data = rally::tests::reference_car();
    desc.car_spawn = rally::sim::stage_spawn(lazadas_track(), -5.0);
    return desc;
}

std::string read_bytes(const std::string& path) {
    std::string bytes;
    std::string error;
    REQUIRE_MESSAGE(rally::io::read_file(path, bytes, error), error);
    return bytes;
}

Recording parse_file(const std::string& path) {
    const rally::io::ParseResult parsed = rally::io::parse(read_bytes(path));
    REQUIRE_MESSAGE(parsed.ok, parsed.error);
    return parsed.recording;
}

// Una grabación de formato 8 hecha a mano, sin simulación, con valores distintos en cada campo
// nuevo.
Recording handmade() {
    Recording r;
    r.header.commit = "test";
    r.header.world = rally::io::serialize_world(lazadas_desc());
    r.header.car_text = rally::tests::reference_car_text();
    r.header.settings = "x = 1\n";
    r.header.stage_ground_hash = 0x0123'4567'89ab'cdefull;
    r.runs = {InputRun{100, SimInput{}}, InputRun{50, SimInput{1000, 2000, 0, 1}}};
    r.footer.complete = true;
    r.footer.total_steps = 150;
    r.footer.final_hash = 0x1111;
    r.footer.timed_steps = -1;
    r.footer.stage = StageResult{123456789012, 3, 5};
    return r;
}

// Las primeras `steps` entradas de una grabación, con el pie de lo que se repite.
Recording first_steps(const Recording& source, std::uint64_t steps) {
    Recording out = source;
    out.runs.clear();
    std::uint64_t left = steps;
    for (const InputRun& run : source.runs) {
        if (left == 0) {
            break;
        }
        InputRun copy = run;
        copy.count = static_cast<std::uint32_t>(std::min<std::uint64_t>(copy.count, left));
        left -= copy.count;
        out.runs.push_back(copy);
    }
    out.footer.total_steps = steps;
    return out;
}

// Cambia un byte de la geometría del tramo (no del nombre: no entra en el hash del suelo) dentro
// de la descripción del mundo de la cabecera. El texto sigue siendo un tramo válido.
void change_stage_byte(Recording& recording) {
    const std::size_t at = recording.header.world.find("start_x = -262.5");
    REQUIRE(at != std::string::npos);
    recording.header.world[at + std::string("start_x = -262.").size()] = '6';
}

std::uint64_t le(const std::string& bytes, std::size_t at, int size) {
    std::uint64_t v = 0;
    for (int i = 0; i < size; ++i) {
        v |= static_cast<std::uint64_t>(
                 static_cast<std::uint8_t>(bytes[at + static_cast<std::size_t>(i)]))
             << (8 * i);
    }
    return v;
}

double to_double(const std::string& text) {
    double value = 0.0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

// Lo que ve una repetición paso a paso: la pasada (los double de StageRunState), la curvatura del
// eje en la s del paso, las seis columnas del tramo del CSV y las reservas del bucle de la
// repetición (las de entre una llamada y la siguiente, sin contar las del propio observador).
class PassObserver final : public rally::io::StepObserver {
public:
    explicit PassObserver(std::size_t steps) {
        runs.reserve(steps);
        curvature.reserve(steps);
        csv.reserve(steps);
    }
    void after_step(const SimWorld& world) override {
        const std::uint64_t now = rally::tests::global_new_count();
        if (started) {
            allocations += now - last_end;
        }
        started = true;
        row_.clear();
        rally::io::append_telemetry_row(world.telemetry(), row_, world.stage_run());
        std::array<std::string, 6> fields;
        std::size_t end = row_.size() - 1; // sin '\n'
        for (int i = 5; i >= 0; --i) {
            const std::size_t comma = row_.rfind(',', end - 1);
            fields[static_cast<std::size_t>(i)] = row_.substr(comma + 1, end - comma - 1);
            end = comma;
        }
        csv.push_back(std::move(fields));
        const StageRunState* run = world.stage_run();
        runs.push_back(run != nullptr ? *run : StageRunState{});
        curvature.push_back(run != nullptr ? world.stage()->pose(run->s).curvature : 0.0);
        last_end = rally::tests::global_new_count();
    }
    std::vector<StageRunState> runs;
    std::vector<double> curvature;
    std::vector<std::array<std::string, 6>> csv;
    std::uint64_t allocations = 0;

private:
    std::string row_;
    bool started = false;
    std::uint64_t last_end = 0;
};

} // namespace

TEST_CASE("stage_v8: a world with a stage round-trips its text byte for byte") {
    const WorldDesc desc = lazadas_desc();
    const std::string bytes = rally::io::serialize_world(desc);
    WorldDesc back;
    std::string error;
    REQUIRE_MESSAGE(rally::io::parse_world(bytes, back, error), error);
    CHECK(back.stage_text == desc.stage_text);
    CHECK(back.stage_variant == rally::sim::StageGroundVariant::Design);
    CHECK(back.stage_micro_relief);
    CHECK(rally::io::serialize_world(back) == bytes);

    // Sin tramo: longitud 0 y texto vacío.
    WorldDesc plain = desc;
    plain.stage_text.clear();
    const std::string plain_bytes = rally::io::serialize_world(plain);
    CHECK(bytes.size() == plain_bytes.size() + desc.stage_text.size());
    CHECK(le(plain_bytes, plain_bytes.size() - 4, 4) == 0);
    REQUIRE(rally::io::parse_world(plain_bytes, back, error));
    CHECK(back.stage_text.empty());
}

TEST_CASE(
    "stage_v8: a whole recording round-trips and the new fields sit where the contract says") {
    const Recording r = handmade();
    const std::string bytes = rally::io::serialize(r);
    CHECK(r.header.format_version == 8);
    CHECK(bytes.size() ==
          rally::io::header_bytes(r.header) + 12 * r.runs.size() + 4 + rally::io::kFooterBytes);
    // Cabecera: el hash del suelo, detrás del texto de las acciones y delante del número de
    // tramos.
    const std::size_t hash_at = rally::io::header_bytes(r.header) - 4 - 8;
    CHECK(le(bytes, hash_at, 8) == r.header.stage_ground_hash);
    CHECK(le(bytes, hash_at + 8, 4) == r.runs.size());
    // Pie: StageResult justo antes de «REND».
    const std::size_t end = bytes.size();
    CHECK(bytes.substr(end - 4) == "REND");
    CHECK(le(bytes, end - 4 - 16, 8) == 123456789012ull);
    CHECK(le(bytes, end - 4 - 8, 4) == 3);
    CHECK(le(bytes, end - 4 - 4, 4) == 5);
    // Y el texto del tramo, al final del mundo: u32 longitud + bytes.
    const std::string& world = r.header.world;
    CHECK(le(world, world.size() - lazadas_text().size() - 4, 4) == lazadas_text().size());
    CHECK(world.substr(world.size() - lazadas_text().size()) == lazadas_text());

    const rally::io::ParseResult parsed = rally::io::parse(bytes);
    REQUIRE_MESSAGE(parsed.ok, parsed.error);
    CHECK(parsed.recording.header.stage_ground_hash == r.header.stage_ground_hash);
    CHECK(parsed.recording.header.world == r.header.world);
    CHECK(parsed.recording.footer.stage == r.footer.stage);
    CHECK(rally::io::serialize(parsed.recording) == bytes);
}

TEST_CASE("stage_v8: truncating the file at any byte is an error and never reads outside") {
    const std::string bytes = rally::io::serialize(handmade());
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        const rally::io::ParseResult parsed = rally::io::parse(std::string_view(bytes.data(), n));
        CHECK_FALSE(parsed.ok);
        CHECK_FALSE(parsed.error.empty());
    }
}

TEST_CASE("stage_v8: the world description rejects bad stage lengths and bad stage texts") {
    const WorldDesc desc = lazadas_desc();
    const std::string bytes = rally::io::serialize_world(desc);
    WorldDesc out;
    std::string error;
    // Truncada en cada byte: error, nunca ok.
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        CHECK_FALSE(rally::io::parse_world(std::string_view(bytes.data(), n), out, error));
    }
    // Longitud del texto mayor que lo que queda: error con el byte.
    std::string longer = bytes;
    const std::size_t length_at = bytes.size() - desc.stage_text.size() - 4;
    longer[length_at + 3] = static_cast<char>(0x7f);
    CHECK_FALSE(rally::io::parse_world(longer, out, error));
    CHECK(error.find("byte") != std::string::npos);
    // Menor: sobran bytes.
    std::string shorter = bytes;
    shorter[length_at] = static_cast<char>(shorter[length_at] - 1);
    CHECK_FALSE(rally::io::parse_world(shorter, out, error));
    CHECK(error.find("byte") != std::string::npos);
    // Un texto que no es un tramo: error, no aborto.
    WorldDesc garbage = desc;
    garbage.stage_text = "esto no es un tramo\n";
    CHECK_FALSE(rally::io::parse_world(rally::io::serialize_world(garbage), out, error));
    CHECK(error.find("tramo") != std::string::npos);
    // Tramo y zona de pruebas a la vez.
    WorldDesc both = desc;
    both.test_zone = true;
    CHECK_FALSE(rally::io::parse_world(rally::io::serialize_world(both), out, error));
    CHECK(error.find("zona") != std::string::npos);
}

// Una sola repetición entera de lazadas (la de Debug es lenta) para los criterios 3, 6 y 7.
TEST_CASE("stage_v8: the stored lazadas recording replays with hash OK, its stage result, "
          "the CSV columns and no allocations") {
    const rally::sim::JoltRuntime runtime;
    const Recording recording = parse_file(kStageRecording);
    CHECK(recording.header.format_version == 8);
    CHECK(recording.header.sim_version == rally::sim::kSimVersion);
    CHECK(recording.header.stage_ground_hash != 0);
    CHECK(recording.footer.stage.steps > 0);
    CHECK(recording.footer.stage.resets == 1);
    CHECK(recording.footer.stage.shortcuts == 0);
    WorldDesc desc;
    std::string error;
    REQUIRE_MESSAGE(rally::io::parse_world(recording.header.world, desc, error), error);
    CHECK(desc.stage_text == lazadas_text());

    PassObserver observer(recording.footer.total_steps);
    const rally::io::ReplayResult replayed = rally::io::replay(recording, &observer);
    CHECK_MESSAGE(replayed.hash_ok, replayed.problem);
    CHECK_FALSE(replayed.stage_mismatch);
    CHECK(replayed.stage == recording.footer.stage);
    std::printf("RALLY_SUMMARY stage_lazadas bytes=%zu steps=%llu pass_steps=%lld resets=%u\n",
                read_bytes(kStageRecording).size(),
                static_cast<unsigned long long>(recording.footer.total_steps),
                static_cast<long long>(recording.footer.stage.steps),
                recording.footer.stage.resets);

    // 7: ni el bucle de la repetición con tramo reserva memoria (sin contar la construcción).
    CHECK(observer.allocations == 0);

    // 6: las columnas.
    const auto columns = rally::io::telemetry_columns();
    REQUIRE(columns.size() == 139);
    const char* const names[6] = {"stage",         "stage_s",      "stage_t",
                                  "stage_on_road", "stage_resets", "stage_shortcuts"};
    for (std::size_t i = 0; i < 6; ++i) {
        CHECK(std::string(columns[133 + i].name) == names[i]);
    }
    REQUIRE(observer.csv.size() == recording.footer.total_steps);
    int on_road_rows = 0;
    int bad_steps = 0;
    int resets_seen = 0;
    for (std::size_t i = 0; i < observer.csv.size(); ++i) {
        const auto& f = observer.csv[i];
        const StageRunState& run = observer.runs[i];
        REQUIRE(f[0] == "1");
        // El CSV es la pasada, con 4 decimales.
        CHECK(std::abs(to_double(f[1]) - run.s) <= 5.1e-5);
        CHECK(std::abs(to_double(f[2]) - run.t) <= 5.1e-5);
        CHECK(f[3] == (run.on_road ? "1" : "0"));
        CHECK(f[4] == std::to_string(run.resets));
        CHECK(f[5] == std::to_string(run.shortcuts));
        on_road_rows += run.on_road ? 1 : 0;
        if (i == 0) {
            continue;
        }
        const StageRunState& before = observer.runs[i - 1];
        if (run.resets + run.shortcuts != before.resets + before.shortcuts) {
            // Recolocación: este paso es el del botón y s es el punto de recolocación anterior.
            ++resets_seen;
            CHECK(i == static_cast<std::size_t>(kResetStep));
            CHECK(run.resets == before.resets + 1);
            CHECK(std::abs(run.s - before.reset_s) < 0.01);
            continue;
        }
        // Sin saltos: |Δs| no pasa de lo recorrido en planta sobre (1 − |κ||t|) más 1 mm (la
        // cota de tramo-seguimiento).
        const double d = std::hypot(run.last_x - before.last_x, run.last_z - before.last_z);
        const double k = std::abs(observer.curvature[i - 1]) * std::abs(before.t);
        const double bound = d / (1.0 - k) + 1e-3;
        if (!(std::abs(run.s - before.s) <= bound)) {
            ++bad_steps;
        }
    }
    CHECK(bad_steps == 0);
    CHECK(resets_seen == 1);
    CHECK(on_road_rows > static_cast<int>(observer.csv.size() / 2));
    CHECK(observer.runs.back().s > 1000.0); // pasada la meta de lazadas (1150 m)
    CHECK(observer.csv.back()[4] == "1");
    CHECK(observer.csv.back()[5] == "0");
}

TEST_CASE("stage_v8: the zone writes the six stage columns as 0") {
    const rally::sim::JoltRuntime runtime;
    const Recording zone =
        first_steps(parse_file(RALLY_TEST_DATA_DIR "/recordings/zone_lap.rrec"), 240);
    PassObserver observer(240);
    REQUIRE(rally::io::replay(zone, &observer).ran);
    REQUIRE(observer.csv.size() == 240);
    for (const auto& f : observer.csv) {
        for (const std::string& text : f) {
            REQUIRE(to_double(text) == 0.0);
        }
    }
}

TEST_CASE("stage_v8: another stage text warns that the stage changed and does not compare") {
    const rally::sim::JoltRuntime runtime;
    const Recording stored = parse_file(kStageRecording);
    Recording changed = first_steps(stored, 240);
    change_stage_byte(changed);
    const rally::io::ReplayResult result = rally::io::replay(changed);
    CHECK(result.ran);
    CHECK(result.stage_mismatch);
    CHECK_FALSE(result.hash_ok);
    // Con 240 pasos el hash final y los pasos tampoco cuadran: que salga el texto del tramo
    // demuestra que se miró antes y que no se compararon.
    CHECK(result.problem.find("el tramo ha cambiado") != std::string::npos);

    // Con el tramo igual, una pasada distinta en el pie es el problema.
    Recording short_run = first_steps(stored, 240);
    const rally::io::ReplayResult base = rally::io::replay(short_run);
    REQUIRE(base.ran);
    CHECK_FALSE(base.stage_mismatch);
    short_run.footer.final_hash = base.hash;
    short_run.footer.timed_steps = base.timed_steps;
    short_run.footer.road = base.road;
    short_run.footer.stage = base.stage;
    const rally::io::ReplayResult same = rally::io::replay(short_run);
    CHECK_MESSAGE(same.hash_ok, same.problem);
    short_run.footer.stage.steps += 1;
    const rally::io::ReplayResult wrong = rally::io::replay(short_run);
    CHECK_FALSE(wrong.hash_ok);
    CHECK(wrong.problem.find("pasada por el tramo distinta") != std::string::npos);
}

// Los resultados de hoy, medidos con el build de integracion 1aa3bc4 (rally_replay run): el
// formato 8 no cambia lo que dan las grabaciones de los formatos anteriores.
TEST_CASE("stage_v8: the recordings of formats 1 to 7 give what they gave before") {
    const rally::sim::JoltRuntime runtime;
    struct Expected {
        const char* path;
        bool hash_ok;
        std::int64_t timed_steps;
        std::int64_t road_steps;
        std::uint32_t road_resets;
        std::uint32_t road_runs;
    };
    constexpr const char* kSessions = RALLY_SOURCE_DIR "/docs/manejo/sesiones/2026-10-14/";
    constexpr const char* kData = RALLY_TEST_DATA_DIR "/recordings/";
    const std::vector<Expected> expected = {
        {"20261014-165144.rrec", true, -1, -1, 0, 0},
        {"20261014-165320.rrec", true, -1, -1, 0, 0},
        {"20261014-165323.rrec", true, -1, -1, 0, 0},
        {"20261014-165348.rrec", true, -1, -1, 0, 0},
        {"20261014-165534.rrec", true, -1, -1, 0, 0},
        {"20261014-165623.rrec", true, -1, -1, 0, 0},
        {"actions_v7.rrec", true, -1, -1, 0, 0},
        {"bench_lap.rrec", true, -1, -1, 0, 0},
        {"road_off.rrec", true, -1, 16834, 0, 1},
        {"road_run.rrec", true, -1, 16580, 0, 1},
        {"zone_lap.rrec", true, 25262, -1, 0, 0},
        {"zone_lap_other_zone.rrec", false, 25262, -1, 0, 0},
    };
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const Expected& e = expected[i];
        const std::string path = std::string(i < 6 ? kSessions : kData) + e.path;
        INFO(path);
        const std::string bytes = read_bytes(path);
        const rally::io::ParseResult parsed = rally::io::parse(bytes);
        REQUIRE_MESSAGE(parsed.ok, parsed.error);
        CHECK(parsed.recording.header.format_version < 8);
        CHECK(parsed.recording.header.stage_ground_hash == 0);
        CHECK(parsed.recording.footer.stage == StageResult{});
        CHECK(rally::io::serialize(parsed.recording) == bytes);
        const rally::io::ReplayResult replayed = rally::io::replay(parsed.recording);
        CHECK(replayed.ran);
        CHECK(replayed.hash_ok == e.hash_ok);
        CHECK(replayed.zone_mismatch == !e.hash_ok);
        CHECK_FALSE(replayed.stage_mismatch);
        CHECK(replayed.stage == StageResult{});
        CHECK(replayed.timed_steps == e.timed_steps);
        CHECK(replayed.road.last_run == e.road_steps);
        CHECK(replayed.road.last_resets == e.road_resets);
        CHECK(replayed.road.runs == e.road_runs);
    }
}

TEST_CASE("stage_v8: recording and stepping a stage world allocate nothing") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(lazadas_desc());
    world.seal();
    Recorder recorder;
    rally::io::RecordingHeader header;
    header.world = rally::io::serialize_world(lazadas_desc());
    header.stage_ground_hash = world.stage_ground_hash();
    recorder.begin(std::move(header), 64);
    SimInput input;
    input.throttle = rally::sim::kAxisMax / 2;
    std::uint64_t allocations = 0;
    for (int frame = 0; frame < 50; ++frame) {
        recorder.ensure_capacity(4);
        const std::uint64_t before = rally::tests::global_new_count();
        for (int i = 0; i < 4; ++i) {
            input.steer = static_cast<std::int16_t>((frame + i) % 2 == 0 ? 500 : -500);
            world.step(input);
            recorder.append(input);
        }
        allocations += rally::tests::global_new_count() - before;
    }
    CHECK(allocations == 0);
    const Recording done =
        recorder.finish(world.state_hash(), rally::io::timed_steps(world),
                        rally::io::road_result(world), 0, rally::io::stage_result(world));
    CHECK(done.footer.stage.steps == -1);
    CHECK(done.header.stage_ground_hash != 0);
}

// Genera tests/data/recordings/stage_lazadas.rrec con RALLY_WRITE_STAGE_REC=<ruta> (en
// Release); sin la variable no hace nada. El conductor de test (rmath) por lazadas a 60 km/h
// desde 5 m antes de la línea, con el 205 de reference.car y las ayudas por defecto, con una
// recolocación con el botón en el paso kResetStep y hasta 1 s pasada la meta. Antes de escribir
// comprueba la meta, los atajos, las recolocaciones y que la repetición da hash OK.
TEST_CASE("stage_v8: write the lazadas recording when RALLY_WRITE_STAGE_REC is set") {
    char* path = nullptr;
    std::size_t path_size = 0;
    if (_dupenv_s(&path, &path_size, "RALLY_WRITE_STAGE_REC") != 0 || path == nullptr) {
        MESSAGE("RALLY_WRITE_STAGE_REC sin definir: no se genera stage_lazadas.rrec");
        return;
    }
    const std::string target = path;
    std::free(path);
    const rally::sim::JoltRuntime runtime;
    const StageTrack track = lazadas_track();
    const WorldDesc desc = lazadas_desc();
    SimWorld world;
    world.build(desc);
    world.seal();
    rally::io::RecordingHeader header;
    header.commit = "test";
    header.world = rally::io::serialize_world(desc);
    header.car_text = rally::tests::reference_car_text();
    header.zone_hash = world.zone_hash();
    header.stage_ground_hash = world.stage_ground_hash();
    Recorder recorder;
    recorder.begin(std::move(header), 64);
    rally::tests::StageDriver driver(track, 60.0 / 3.6, 2.5);
    int after_finish = -1;
    for (int i = 0; i < 300 * rally::sim::kStepsPerSecond; ++i) {
        SimInput in = driver.input(world.car_pose(), world.car_velocity());
        if (i == kResetStep) {
            in.buttons = rally::sim::kButtonReset;
        }
        recorder.ensure_capacity(1);
        world.step(in);
        recorder.append(in);
        if (world.stage_run()->finish_step >= 0 && after_finish < 0) {
            after_finish = 0;
            driver.set_speed(0.0);
        }
        if (after_finish >= 0 && ++after_finish > rally::sim::kStepsPerSecond) {
            break;
        }
    }
    REQUIRE(after_finish > 0);
    const Recording recording =
        recorder.finish(world.state_hash(), rally::io::timed_steps(world),
                        rally::io::road_result(world), 0, rally::io::stage_result(world));
    REQUIRE(recording.footer.stage.steps > 0);
    REQUIRE(recording.footer.stage.resets == 1);
    REQUIRE(recording.footer.stage.shortcuts == 0);
    const rally::io::ReplayResult replayed = rally::io::replay(recording);
    REQUIRE_MESSAGE(replayed.hash_ok, replayed.problem);
    std::string error;
    CHECK_MESSAGE(rally::io::write_file(target, rally::io::serialize(recording), error), error);
    MESSAGE("stage_lazadas.rrec: ", recording.footer.total_steps, " pasos, pasada ",
            recording.footer.stage.steps, " pasos (",
            static_cast<double>(recording.footer.stage.steps) / rally::sim::kStepsPerSecond,
            " s), ", recording.footer.stage.resets, " recolocaciones, ",
            recording.footer.stage.shortcuts, " atajos");
}
