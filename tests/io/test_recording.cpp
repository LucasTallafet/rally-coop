#include "car_worlds.h"
#include "io/crash_save.h"
#include "io/recording.h"
#include "io/replay.h"
#include "new_counter.h"
#include "platform/fixed_step_clock.h"
#include "sim/demo_world.h"
#include "sim/jolt_runtime.h"
#include "sim/pcg32.h"
#include "sim/sim_input.h"
#include "sim/sim_version.h"
#include "sim/sim_world.h"

#include <doctest/doctest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

using rally::io::InputRun;
using rally::io::Recorder;
using rally::io::Recording;
using rally::io::RecordingHeader;
using rally::sim::kAxisMax;
using rally::sim::SimInput;
using rally::sim::SimWorld;

namespace {

// Entrada aleatoria de un fotograma, con todos los campos y botones.
SimInput random_input(rally::sim::Pcg32& rng) {
    SimInput input;
    input.steer =
        static_cast<std::int16_t>(static_cast<int>(rng.bounded(2u * kAxisMax + 1u)) - kAxisMax);
    input.throttle = static_cast<std::int16_t>(rng.bounded(kAxisMax + 1u));
    input.brake = static_cast<std::int16_t>(rng.bounded(3) == 0 ? rng.bounded(kAxisMax + 1u) : 0);
    input.buttons = static_cast<std::uint16_t>(rng.bounded(256));
    return input;
}

// `seconds` de entradas como las del bucle: cambian cada 4 pasos (60 fps), y en una de cada
// cuatro veces se repite la del fotograma anterior.
std::vector<SimInput> random_steps(int seconds, std::uint64_t seed) {
    rally::sim::Pcg32 rng(seed, 11u);
    std::vector<SimInput> steps;
    const std::size_t total = static_cast<std::size_t>(seconds) * rally::sim::kStepsPerSecond;
    steps.reserve(total);
    SimInput input;
    while (steps.size() < total) {
        if (rng.bounded(4) != 0) {
            input = random_input(rng);
        }
        for (int i = 0; i < 4 && steps.size() < total; ++i) {
            steps.push_back(input);
        }
    }
    return steps;
}

RecordingHeader test_header(const rally::sim::WorldDesc& desc) {
    RecordingHeader header;
    header.commit = "0123456789ab-dirty";
    header.world = rally::io::serialize_world(desc);
    header.car_text = rally::tests::reference_car_text();
    header.settings = "";
    return header;
}

Recording record(const std::vector<SimInput>& steps) {
    Recorder recorder;
    recorder.begin(test_header(rally::sim::demo_arena(0.0, rally::tests::reference_car())),
                   steps.size());
    for (const SimInput& input : steps) {
        recorder.append(input);
    }
    return recorder.finish(0x1234'5678'9abc'def0ull, 9876);
}

std::vector<SimInput> expand(const Recording& recording) {
    std::vector<SimInput> steps;
    for (const InputRun& run : recording.runs) {
        steps.insert(steps.end(), run.count, run.input);
    }
    return steps;
}

// Repite `steps` en el mundo de la demo y devuelve la grabación ya terminada con su hash.
Recording simulate_and_record(const std::vector<SimInput>& steps) {
    const rally::sim::WorldDesc desc = rally::sim::demo_arena(0.0, rally::tests::reference_car());
    SimWorld world;
    world.build(desc);
    world.seal();
    Recorder recorder;
    recorder.begin(test_header(desc), steps.size());
    for (const SimInput& input : steps) {
        world.step(input);
        recorder.append(input);
    }
    return recorder.finish(world.state_hash(), -1);
}

} // namespace

TEST_CASE("sim_input: eight bytes and no padding") {
    CHECK(sizeof(SimInput) == 8);
    CHECK(std::has_unique_object_representations_v<SimInput>);
    // Bits distintos, uno por botón.
    const std::uint16_t bits[] = {
        rally::sim::kButtonHandbrake,    rally::sim::kButtonShiftUp,
        rally::sim::kButtonShiftDown,    rally::sim::kButtonReset,
        rally::sim::kButtonSteerDigital, rally::sim::kButtonThrottleDigital,
        rally::sim::kButtonBrakeDigital, rally::sim::kButtonReserved7,
    };
    std::uint16_t all = 0;
    for (const std::uint16_t bit : bits) {
        CHECK((all & bit) == 0);
        all |= bit;
    }
    CHECK(all == 0xff);
}

TEST_CASE("recording: 60 s of PCG32 inputs survive a round trip exactly") {
    const std::vector<SimInput> steps = random_steps(60, 0x7ec0'0000'0000'0001ull);
    const Recording original = record(steps);
    const std::string bytes = rally::io::serialize(original);
    const rally::io::ParseResult parsed = rally::io::parse(bytes);
    REQUIRE_MESSAGE(parsed.ok, parsed.error);
    const Recording& back = parsed.recording;

    CHECK(expand(back) == steps);
    CHECK(back.runs.size() == original.runs.size());
    CHECK(back.header.format_version == rally::io::kRecordingFormatVersion);
    CHECK(back.header.sim_version == rally::sim::kSimVersion);
    CHECK(back.header.commit == original.header.commit);
    CHECK(back.header.world == original.header.world);
    CHECK(back.header.car_text == original.header.car_text);
    CHECK(back.header.settings == original.header.settings);
    CHECK(back.footer.complete);
    CHECK(back.footer.total_steps == steps.size());
    CHECK(back.footer.final_hash == 0x1234'5678'9abc'def0ull);
    CHECK(back.footer.timed_steps == 9876);
    // Volver a serializar da los mismos bytes.
    CHECK(rally::io::serialize(back) == bytes);
}

TEST_CASE("recording: the file is exactly header plus 12 bytes per run plus footer") {
    const std::vector<SimInput> steps = random_steps(60, 0x7ec0'0000'0000'0002ull);
    const Recording recording = record(steps);
    const std::string bytes = rally::io::serialize(recording);
    const RecordingHeader& h = recording.header;
    // Firma, dos versiones, cuatro cadenas con su longitud, el hash de la zona (formato 2), el
    // texto de las acciones (formato 7) y el número de tramos.
    const std::size_t header = 8 + 4 + 4 + (4 + h.commit.size()) + (4 + h.world.size()) +
                               (4 + h.car_text.size()) + (4 + h.settings.size()) + 8 +
                               (4 + h.actions_text.size()) + 4;
    CHECK(rally::io::header_bytes(h) == header);
    // Detrás de los tramos, el número de eventos de acción (formato 7).
    CHECK(bytes.size() == header + 12 * recording.runs.size() + 4 + rally::io::kFooterBytes);

    // RLE: entradas que cambian cada 4 pasos dan un tramo cada 4 pasos; si se repite la del
    // fotograma anterior, el tramo se alarga.
    std::size_t changes = 1;
    for (std::size_t i = 1; i < steps.size(); ++i) {
        changes += steps[i] == steps[i - 1] ? 0 : 1;
    }
    CHECK(recording.runs.size() == changes);
    const double ratio =
        static_cast<double>(steps.size() * sizeof(SimInput)) / static_cast<double>(bytes.size());
    MESSAGE("60 s, entrada que cambia en 3 de cada 4 fotogramas: ", recording.runs.size(),
            " tramos, ", bytes.size(), " bytes, ", ratio, "x frente a 8 B por paso");
}

TEST_CASE("recording: a truncated file or a bad signature is an error, never an overread") {
    const Recording recording = record(random_steps(2, 0x7ec0'0000'0000'0003ull));
    const std::string bytes = rally::io::serialize(recording);
    REQUIRE(rally::io::parse(bytes).ok);

    // Cortado en cada byte: siempre error. Se copia a un búfer exacto para que una lectura
    // fuera de rango la detecte el depurador de la CRT en Debug.
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        const std::vector<char> cut(bytes.begin(), bytes.begin() + static_cast<long long>(size));
        const rally::io::ParseResult parsed =
            rally::io::parse(std::string_view(cut.data(), cut.size()));
        CHECK_FALSE(parsed.ok);
        if (parsed.ok) {
            break;
        }
    }

    std::string bad = bytes;
    bad[0] = 'X';
    const rally::io::ParseResult bad_magic = rally::io::parse(bad);
    CHECK_FALSE(bad_magic.ok);
    CHECK(bad_magic.error.find("firma") != std::string::npos);

    // Un byte de más al final, un número de tramos enorme y una longitud de cadena enorme:
    // error, sin intentar reservar lo que dicen.
    CHECK_FALSE(rally::io::parse(bytes + "x").ok);
    std::string huge_runs = bytes;
    const std::size_t runs_at = rally::io::header_bytes(recording.header) - 4;
    huge_runs[runs_at + 3] = '\x7f';
    CHECK_FALSE(rally::io::parse(huge_runs).ok);
    std::string huge_string = bytes;
    huge_string[16 + 3] = '\x7f'; // longitud del commit
    CHECK_FALSE(rally::io::parse(huge_string).ok);
    // Un pie que no cuadra con los tramos.
    std::string wrong_steps = bytes;
    wrong_steps[bytes.size() - rally::io::kFooterBytes + 4] ^= 1;
    CHECK_FALSE(rally::io::parse(wrong_steps).ok);
}

TEST_CASE("recording: the world description round-trips bit for bit") {
    const rally::sim::WorldDesc desc =
        rally::sim::demo_arena(20000.123, rally::tests::reference_car());
    rally::sim::WorldDesc back;
    std::string error;
    REQUIRE(rally::io::parse_world(rally::io::serialize_world(desc), back, error));
    REQUIRE(back.bodies.size() == desc.bodies.size());
    CHECK(back.car == desc.car);
    CHECK(std::bit_cast<std::uint64_t>(back.car_spawn.x) ==
          std::bit_cast<std::uint64_t>(desc.car_spawn.x));
    CHECK(std::bit_cast<std::uint64_t>(back.car_spawn.y) ==
          std::bit_cast<std::uint64_t>(desc.car_spawn.y));
    CHECK(std::bit_cast<std::uint64_t>(back.car_spawn.z) ==
          std::bit_cast<std::uint64_t>(desc.car_spawn.z));
    CHECK(back.car_spawn.yaw == desc.car_spawn.yaw);
    CHECK(back.car_spawn.speed == desc.car_spawn.speed);
    for (std::size_t i = 0; i < desc.bodies.size(); ++i) {
        const rally::sim::BoxDesc& a = desc.bodies[i];
        const rally::sim::BoxDesc& b = back.bodies[i];
        CHECK(a.motion == b.motion);
        CHECK(std::bit_cast<std::uint64_t>(a.x) == std::bit_cast<std::uint64_t>(b.x));
        CHECK(std::bit_cast<std::uint64_t>(a.y) == std::bit_cast<std::uint64_t>(b.y));
        CHECK(std::bit_cast<std::uint64_t>(a.z) == std::bit_cast<std::uint64_t>(b.z));
        CHECK(a.half_x == b.half_x);
        CHECK(a.half_y == b.half_y);
        CHECK(a.half_z == b.half_z);
        CHECK(a.pitch == b.pitch);
        CHECK(a.surface == b.surface);
        CHECK(a.micro == b.micro);
    }
    // Una caja girada, con superficie y h, también.
    rally::sim::WorldDesc tilted = desc;
    tilted.bodies[0].pitch = 0.25f;
    tilted.bodies[0].surface = rally::sim::Surface::Snow;
    tilted.bodies[0].micro = true;
    tilted.car_spawn.speed = 12.5f;
    REQUIRE(rally::io::parse_world(rally::io::serialize_world(tilted), back, error));
    CHECK(back.bodies[0].pitch == 0.25f);
    CHECK(back.bodies[0].surface == rally::sim::Surface::Snow);
    CHECK(back.bodies[0].micro);
    CHECK(back.car_spawn.speed == 12.5f);
    CHECK_FALSE(rally::io::parse_world("abc", back, error));
}

TEST_CASE("recording: append allocates nothing once capacity is ensured") {
    Recorder recorder;
    recorder.begin(test_header(rally::sim::demo_arena(0.0, rally::tests::reference_car())), 4);
    rally::sim::Pcg32 rng(0x7ec0'0000'0000'0004ull, 1u);
    int allocations = 0;
    for (int frame = 0; frame < 2000; ++frame) {
        // Como la partida: hueco para los pasos del fotograma antes de simularlos.
        recorder.ensure_capacity(8);
        const SimInput input = random_input(rng);
        const std::uint64_t before = rally::tests::global_new_count();
        for (int i = 0; i < 8; ++i) {
            recorder.append(input);
        }
        allocations += static_cast<int>(rally::tests::global_new_count() - before);
    }
    CHECK(allocations == 0);
    CHECK(recorder.steps() == 16000);
}

TEST_CASE("recording: a game-like loop and its replay give the same hash and steps") {
    const rally::sim::JoltRuntime runtime;
    const rally::sim::WorldDesc desc = rally::sim::demo_arena(0.0, rally::tests::reference_car());
    SimWorld world;
    world.build(desc);
    world.seal();
    Recorder recorder;
    recorder.begin(test_header(desc), 16);

    // Bucle de la partida: fotogramas de 16,7 ms con ±1,5 ms de ruido y 33 ms perdidos de
    // vez en cuando, ajuste al refresco de 60 Hz, entrada leída una vez por fotograma.
    rally::platform::FixedStepClock clock;
    clock.set_snap_period(rally::platform::refresh_period_units(60, 1));
    rally::sim::Pcg32 rng(0x7ec0'0000'0000'0005ull, 5u);
    int frames_with_other_steps = 0;
    SimInput input;
    for (int frame = 0; frame < 60 * 30; ++frame) {
        std::int64_t frame_ns =
            16'666'667 + static_cast<std::int64_t>(rng.bounded(3'000'001)) - 1'500'000;
        if (rng.bounded(100) == 0) {
            frame_ns = 33'333'333;
        }
        const rally::platform::FrameSteps steps = clock.advance(frame_ns);
        frames_with_other_steps += steps.count != 4 ? 1 : 0;
        if (rng.bounded(3) != 0) {
            input = random_input(rng);
            // Recolocar no muy a menudo, para que la caja llegue a moverse.
            if (rng.bounded(20) != 0) {
                input.buttons &= static_cast<std::uint16_t>(~rally::sim::kButtonReset);
            }
        }
        recorder.ensure_capacity(steps.count);
        for (int i = 0; i < steps.count; ++i) {
            world.step(input);
            recorder.append(input);
        }
    }
    const std::uint64_t game_hash = world.state_hash();
    const std::uint64_t game_steps = recorder.steps();
    const Recording recorded = recorder.finish(game_hash, -1);
    REQUIRE(frames_with_other_steps > 0); // hubo fotogramas perdidos

    // Al archivo y de vuelta, y repetición sin bucle.
    const rally::io::ParseResult parsed = rally::io::parse(rally::io::serialize(recorded));
    REQUIRE_MESSAGE(parsed.ok, parsed.error);
    const rally::io::ReplayResult replayed = rally::io::replay(parsed.recording);
    CHECK_MESSAGE(replayed.hash_ok, replayed.problem);
    CHECK(replayed.steps == game_steps);
    CHECK(replayed.hash == game_hash);
    CHECK(static_cast<std::int64_t>(game_steps) == clock.steps_done());
    MESSAGE("pasos: ", game_steps, "; fotogramas con otros pasos que 4: ", frames_with_other_steps);
}

namespace {

// Descripción del mundo como la escribían los formatos 1 y 2 (tareas 2.1–2.3): cajas de 37
// bytes, el índice de la caja controlada y, en el 2, la zona y la pista ciega.
std::string old_world_bytes(std::uint32_t format_version) {
    std::string out;
    const auto put = [&out](const void* data, std::size_t size) {
        out.append(static_cast<const char*>(data), size); // x64: little-endian
    };
    const rally::sim::WorldDesc desc = rally::sim::demo_arena(0.0, rally::tests::reference_car());
    std::vector<rally::sim::BoxDesc> bodies = desc.bodies;
    rally::sim::BoxDesc box; // la caja controlada, en el centro
    box.motion = rally::sim::BodyMotion::Dynamic;
    box.y = 0.5;
    bodies.push_back(box);
    const auto count = static_cast<std::uint32_t>(bodies.size());
    put(&count, 4);
    for (const rally::sim::BoxDesc& b : bodies) {
        const auto motion = static_cast<std::uint8_t>(b.motion);
        put(&motion, 1);
        put(&b.x, 8);
        put(&b.y, 8);
        put(&b.z, 8);
        put(&b.half_x, 4);
        put(&b.half_y, 4);
        put(&b.half_z, 4);
    }
    const std::int32_t controlled = static_cast<std::int32_t>(count) - 1;
    put(&controlled, 4);
    if (format_version >= 2) {
        const std::uint8_t zone_and_surface[2] = {0, 1};
        put(zone_and_surface, 2);
    }
    return out;
}

} // namespace

TEST_CASE("recording: a format 1 or 2 recording of the box reads and replays warning of the "
          "sim version") {
    const rally::sim::JoltRuntime runtime;
    for (const std::uint32_t format : {1u, 2u}) {
        INFO("formato ", format);
        Recording recording;
        recording.header.format_version = format;
        recording.header.sim_version = 1; // la de la caja
        recording.header.commit = "2.3";
        recording.header.world = old_world_bytes(format);
        recording.header.car_text.clear();
        recording.runs = {InputRun{240, SimInput{}}};
        recording.footer.complete = true;
        recording.footer.total_steps = 240;
        recording.footer.final_hash = 0x1234u;
        const std::string bytes = rally::io::serialize(recording);
        CHECK(bytes.size() == rally::io::header_bytes(recording.header) +
                                  12 * recording.runs.size() + rally::io::footer_bytes(format));
        const rally::io::ParseResult parsed = rally::io::parse(bytes);
        REQUIRE_MESSAGE(parsed.ok, parsed.error);
        CHECK(parsed.recording.header.format_version == format);
        rally::sim::WorldDesc desc;
        std::string error;
        REQUIRE(rally::io::parse_world(parsed.recording.header.world, desc, error, format));
        CHECK_FALSE(desc.car); // la caja controlada queda como un cuerpo más, sin coche
        const rally::io::ReplayResult replayed = rally::io::replay(parsed.recording);
        CHECK(replayed.ran);
        CHECK(replayed.version_mismatch);
        CHECK_FALSE(replayed.hash_ok);
        CHECK(replayed.steps == 240);
        CHECK(replayed.problem.find("versión de la simulación") != std::string::npos);
    }
}

TEST_CASE("recording: another sim version replays without comparing the hash, and warns") {
    const rally::sim::JoltRuntime runtime;
    Recording recording = simulate_and_record(random_steps(5, 0x7ec0'0000'0000'0006ull));
    REQUIRE(rally::io::replay(recording).hash_ok);

    recording.header.sim_version = rally::sim::kSimVersion - 1;
    recording.footer.final_hash ^= 1; // aunque no cuadre, no se compara
    const rally::io::ReplayResult old = rally::io::replay(recording);
    CHECK(old.ran);
    CHECK(old.version_mismatch);
    CHECK_FALSE(old.hash_ok);
    CHECK(old.steps == recording.footer.total_steps);
    CHECK(old.problem.find("versión de la simulación") != std::string::npos);

    // rewrite (solo grabaciones sintéticas): versión actual y pie nuevo, y ya cuadra.
    const rally::io::ReplayResult rewritten = rally::io::rewrite_footer(recording);
    CHECK(rewritten.hash_ok);
    CHECK(recording.header.sim_version == rally::sim::kSimVersion);
}

TEST_CASE("recording: a different final hash is reported as the problem") {
    const rally::sim::JoltRuntime runtime;
    Recording recording = simulate_and_record(random_steps(2, 0x7ec0'0000'0000'0007ull));
    recording.footer.final_hash ^= 1;
    const rally::io::ReplayResult result = rally::io::replay(recording);
    CHECK(result.ran);
    CHECK_FALSE(result.hash_ok);
    CHECK_FALSE(result.version_mismatch);
    CHECK(result.problem.find("hash final distinto") != std::string::npos);
}

TEST_CASE(
    "recording: the crash save writes an incomplete recording that replays to its last step") {
    const rally::sim::JoltRuntime runtime;
    const std::vector<SimInput> steps = random_steps(3, 0x7ec0'0000'0000'0008ull);
    const rally::sim::WorldDesc desc = rally::sim::demo_arena(0.0, rally::tests::reference_car());
    SimWorld world;
    world.build(desc);
    world.seal();
    Recorder recorder;
    recorder.begin(test_header(desc), steps.size());
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rally_test_crash_save.rrec";
    std::filesystem::remove(path);
    rally::io::set_crash_recording(&recorder, path.string());
    for (const SimInput& input : steps) {
        world.step(input);
        recorder.append(input);
    }
    // Lo que hace el manejador de RALLY_CHECK de la partida antes de mostrar el error (el
    // aborto de verdad lo prueba recording.abort_saves_incomplete en ctest).
    REQUIRE(rally::io::save_crash_recording());
    rally::io::set_crash_recording(nullptr, {});
    CHECK_FALSE(rally::io::save_crash_recording()); // sin grabación registrada, nada

    std::string bytes;
    std::string error;
    REQUIRE(rally::io::read_file(path.string(), bytes, error));
    std::filesystem::remove(path);
    const rally::io::ParseResult parsed = rally::io::parse(bytes);
    REQUIRE_MESSAGE(parsed.ok, parsed.error);
    CHECK_FALSE(parsed.recording.footer.complete);
    CHECK(parsed.recording.footer.final_hash == 0);
    CHECK(parsed.recording.footer.total_steps == steps.size());
    CHECK(expand(parsed.recording) == steps);

    const rally::io::ReplayResult replayed = rally::io::replay(parsed.recording);
    CHECK(replayed.ran);
    CHECK(replayed.incomplete);
    CHECK_FALSE(replayed.hash_ok);
    CHECK(replayed.steps == steps.size());
    // Repetida hasta su último paso, llega al mismo estado que la partida.
    CHECK(replayed.hash == world.state_hash());
    CHECK(replayed.problem.find("incompleta") != std::string::npos);
}
