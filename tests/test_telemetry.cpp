#include "io/telemetry_csv.h"
#include "new_counter.h"
#include "platform/telemetry_history.h"
#include "sim/demo_world.h"
#include "sim/fnv1a.h"
#include "sim/jolt_runtime.h"
#include "sim/pcg32.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"
#include "sim/surface.h"
#include "sim/telemetry.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace {

using rally::sim::CarTelemetry;
using rally::sim::kAxisMax;
using rally::sim::kStepsPerSecond;
using rally::sim::SimInput;
using rally::sim::SimWorld;
using rally::sim::Surface;

// FNV-1a del CSV (cabecera y filas) de 10 s de entradas pseudoaleatorias en la zona
// (telemetry_csv_for_random_inputs). Tiene que ser el mismo en Debug y en Release. Cambia
// cuando una tarea añade columnas o cambia la simulación: se copia aquí el valor nuevo y va en
// el mensaje del commit («Telemetry hash: 0x... -> 0x...»).
constexpr std::uint64_t kExpectedTelemetryHash = 0x4926727084887265ull;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = 180.0 / kPi;

double horizontal_speed(const CarTelemetry& t) { return std::hypot(t.vx, t.vz); }

// Zona de pruebas con la caja en la salida de la recta de asfalto, mirando a -z.
void build_zone(SimWorld& world) {
    world.build(rally::sim::zone_world(Surface::Gravel));
    world.seal();
    world.set_telemetry(true);
}

std::string telemetry_csv_for_random_inputs() {
    SimWorld world;
    build_zone(world);
    rally::sim::Pcg32 rng(0x7e1e'3e7e'0000'0001ull, 3u);
    std::string csv = rally::io::telemetry_csv_header("random.rrec", true, Surface::Gravel);
    SimInput input;
    for (int step = 0; step < 10 * kStepsPerSecond; ++step) {
        if (step % 4 == 0) {
            input.steer = static_cast<std::int16_t>(
                static_cast<int>(rng.bounded(2u * kAxisMax + 1u)) - kAxisMax);
            input.throttle = static_cast<std::int16_t>(rng.bounded(kAxisMax + 1u));
            input.brake = rng.bounded(4) == 0
                              ? static_cast<std::int16_t>(rng.bounded(kAxisMax + 1u))
                              : std::int16_t{0};
            input.buttons = 0;
            if (rng.bounded(8) == 0) {
                input.buttons |= rally::sim::kButtonHandbrake;
            }
            if (rng.bounded(150) == 0) {
                input.buttons |= rally::sim::kButtonReset;
            }
        }
        world.step(input);
        rally::io::append_telemetry_row(world.telemetry(), csv);
    }
    return csv;
}

} // namespace

TEST_CASE("telemetry: the box driving straight on asphalt has beta below 0.5 degrees") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    build_zone(world);
    SimInput throttle;
    throttle.throttle = kAxisMax;
    double max_beta = 0.0;
    int measured = 0;
    for (int i = 0; i < 3 * kStepsPerSecond; ++i) {
        world.step(throttle);
        const CarTelemetry& t = world.telemetry();
        REQUIRE(t.step == i + 1);
        CHECK(t.surface == static_cast<std::int8_t>(Surface::Asphalt));
        if (horizontal_speed(t) > 1.0) {
            max_beta = std::max(max_beta, std::abs(t.beta * kDeg));
            ++measured;
        }
    }
    MESSAGE("recta: |beta| máximo ", max_beta, "° en ", measured, " pasos; velocidad final ",
            world.telemetry().speed, " m/s");
    CHECK(measured > 2 * kStepsPerSecond);
    CHECK(max_beta < 0.5);
    CHECK(world.telemetry().v_forward > 15.0f);
    CHECK(std::abs(world.telemetry().heading) < 1e-4f);
}

TEST_CASE("telemetry: the box turned sideways while sliding on asphalt has beta near -90 degrees") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    build_zone(world);
    SimInput throttle;
    throttle.throttle = kAxisMax;
    for (int i = 0; i < 2 * kStepsPerSecond; ++i) {
        world.step(throttle);
    }
    // Suelta y gira a la derecha en el sitio hasta poner el morro de lado: la caja sigue
    // deslizando hacia donde iba, a la izquierda del morro.
    SimInput turn;
    turn.steer = kAxisMax;
    int turning = 0;
    while (world.telemetry().heading < kPi / 2.0 - 0.01 && turning < 2 * kStepsPerSecond) {
        world.step(turn);
        ++turning;
    }
    const SimInput coast;
    for (int i = 0; i < kStepsPerSecond / 4; ++i) {
        world.step(coast);
    }
    double min_beta = 1e9;
    double max_beta = -1e9;
    int measured = 0;
    for (int i = 0; i < kStepsPerSecond; ++i) {
        world.step(coast);
        const CarTelemetry& t = world.telemetry();
        if (horizontal_speed(t) > 3.0) {
            min_beta = std::min(min_beta, t.beta * kDeg);
            max_beta = std::max(max_beta, t.beta * kDeg);
            ++measured;
        }
    }
    MESSAGE("de lado: beta entre ", min_beta, "° y ", max_beta, "° en ", measured, " pasos; rumbo ",
            world.telemetry().heading * kDeg, "°; giro de ", turning, " pasos");
    CHECK(world.telemetry().heading * kDeg == doctest::Approx(90.0).epsilon(0.03));
    CHECK(measured == kStepsPerSecond);
    CHECK(min_beta > -95.0);
    CHECK(max_beta < -85.0);
}

TEST_CASE("telemetry: turning right gives positive yaw rate and lateral acceleration") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    build_zone(world);
    SimInput input;
    input.throttle = kAxisMax;
    input.steer = kAxisMax / 2;
    int checked = 0;
    float min_yaw = 1e9f;
    float min_lateral = 1e9f;
    for (int i = 0; i < 3 * kStepsPerSecond; ++i) {
        world.step(input);
        const CarTelemetry& t = world.telemetry();
        if (i >= kStepsPerSecond / 2) {
            min_yaw = std::min(min_yaw, t.yaw_rate);
            min_lateral = std::min(min_lateral, t.a_right);
            ++checked;
        }
        if (i + 1 == kStepsPerSecond / 2) {
            // Rumbo tras 0,5 s: la guiñada aplicada integrada (dirección a la mitad, 1,5 rad/s a
            // fondo), positiva.
            CHECK(t.heading > 0.3f);
            CHECK(t.heading < 0.4f);
        }
    }
    MESSAGE("girando a la derecha: guiñada mínima ", min_yaw, " rad/s, aceleración lateral mínima ",
            min_lateral, " m/s² en ", checked, " pasos; beta final ", world.telemetry().beta * kDeg,
            "°");
    CHECK(min_yaw > 0.0f);
    CHECK(min_lateral > 0.0f);
    // La velocidad queda a la izquierda del morro al girar a la derecha.
    CHECK(world.telemetry().beta < 0.0f);
}

TEST_CASE("telemetry: the reset step reports zero acceleration and speed") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    build_zone(world);
    SimInput throttle;
    throttle.throttle = kAxisMax;
    for (int i = 0; i < kStepsPerSecond; ++i) {
        world.step(throttle);
    }
    CHECK(world.telemetry().a_forward > 1.0f);
    SimInput reset;
    reset.buttons = rally::sim::kButtonReset;
    world.step(reset);
    const CarTelemetry& t = world.telemetry();
    CHECK((t.pressed & rally::sim::kButtonReset) != 0);
    CHECK(t.ax == 0.0f);
    CHECK(t.ay == 0.0f);
    CHECK(t.az == 0.0f);
    CHECK(t.a_forward == 0.0f);
    CHECK(t.a_right == 0.0f);
    CHECK(t.speed == 0.0f);
    CHECK(t.impact == 0.0f);
}

TEST_CASE("telemetry: the same inputs give the same CSV twice and the stored hash") {
    const rally::sim::JoltRuntime runtime;
    const std::string first = telemetry_csv_for_random_inputs();
    const std::string second = telemetry_csv_for_random_inputs();
    CHECK(first == second);
    const std::uint64_t hash = rally::sim::fnv1a64(first);
    char message[300];
    std::snprintf(message, sizeof(message),
                  "hash del CSV 0x%016llxull, guardado 0x%016llxull (%zu bytes). Si cambian las "
                  "columnas o la simulación a propósito, copia el actual en "
                  "kExpectedTelemetryHash (tests/test_telemetry.cpp) y dilo en el commit.",
                  static_cast<unsigned long long>(hash),
                  static_cast<unsigned long long>(kExpectedTelemetryHash), first.size());
    INFO(message);
    CHECK(hash == kExpectedTelemetryHash);
}

TEST_CASE("telemetry: appending a row allocates nothing with a reserved buffer") {
    CarTelemetry t;
    t.step = 123456;
    t.x = -12345.678901;
    t.beta = -3.14159f;
    t.lap_steps = 2400;
    t.surface = static_cast<std::int8_t>(Surface::Sand);
    std::string out;
    out.reserve(rally::io::kTelemetryRowMaxBytes * 4);
    const std::uint64_t before = rally::tests::global_new_count();
    for (int i = 0; i < 4; ++i) {
        rally::io::append_telemetry_row(t, out);
    }
    CHECK(rally::tests::global_new_count() == before);
    CHECK(out.size() < rally::io::kTelemetryRowMaxBytes * 4);
}

TEST_CASE("telemetry: rows have one value per column and values rounding to zero have no sign") {
    CarTelemetry t;
    t.step = 7;
    t.beta = -1e-9f;  // 5 decimales: 0.00000
    t.vx = -0.00004f; // 4 decimales: 0.0000
    t.ax = -0.0006f;  // 3 decimales: -0.001
    t.lap_steps = -1; // sin vuelta
    t.surface = -1;   // sin zona
    std::string row;
    rally::io::append_telemetry_row(t, row);
    REQUIRE(!row.empty());
    CHECK(row.back() == '\n');
    std::vector<std::string> values;
    std::string current;
    for (const char c : row.substr(0, row.size() - 1)) {
        if (c == ',') {
            values.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    values.push_back(current);
    const auto columns = rally::io::telemetry_columns();
    REQUIRE(values.size() == columns.size());
    for (std::size_t i = 0; i < columns.size(); ++i) {
        const std::string name = columns[i].name;
        if (name == "beta") {
            CHECK(values[i] == "0.00000");
        } else if (name == "vx") {
            CHECK(values[i] == "0.0000");
        } else if (name == "ax") {
            CHECK(values[i] == "-0.001");
        } else if (name == "step") {
            CHECK(values[i] == "7");
        } else if (name == "lap_time") {
            CHECK(values[i] == "-1.000000");
        } else if (name == "surface") {
            CHECK(values[i] == "none");
        }
        CHECK(values[i].find("-0.0000") == std::string::npos);
    }
}

TEST_CASE("telemetry: the header and the markdown name every column once") {
    const std::string header =
        rally::io::telemetry_csv_header("dir/zone_lap.rrec", true, Surface::Snow);
    CHECK(header.find("# format=1\n") != std::string::npos);
    CHECK(header.find("# sim_version=") != std::string::npos);
    CHECK(header.find("# recording=dir/zone_lap.rrec\n") != std::string::npos);
    CHECK(header.find("# blind_surface=snow\n") != std::string::npos);
    CHECK(
        rally::io::telemetry_csv_header("a", false, Surface::Snow).find("# blind_surface=none\n") !=
        std::string::npos);
    const std::string md = rally::io::telemetry_columns_markdown();
    std::set<std::string> names;
    for (const rally::io::TelemetryColumn& column : rally::io::telemetry_columns()) {
        CHECK(names.insert(column.name).second);
        CHECK(md.find(std::string("| `") + column.name + "` |") != std::string::npos);
        CHECK(header.find(column.name) != std::string::npos);
        CHECK((column.kind == rally::io::ColumnKind::Text) == (column.text != nullptr));
        CHECK((column.kind == rally::io::ColumnKind::Text) == (column.number == nullptr));
    }
}

TEST_CASE("telemetry_history: the ring keeps the last steps and push allocates nothing") {
    rally::platform::TelemetryHistory history(5);
    CHECK(history.capacity() == 5);
    CHECK(history.size() == 0);
    CarTelemetry t;
    const std::uint64_t before = rally::tests::global_new_count();
    for (int i = 1; i <= 12; ++i) {
        t.step = i;
        history.push(t);
    }
    CHECK(rally::tests::global_new_count() == before);
    REQUIRE(history.size() == 5);
    for (int i = 0; i < 5; ++i) {
        CHECK(history.at(i).step == 8 + i);
    }
    // Columnas con el búfer dado la vuelta: una columna por paso, unida al paso anterior.
    using Range = rally::platform::TelemetryHistory::Range;
    const std::array<rally::platform::TelemetryHistory::Channel, 1> step_channel = {
        [](const CarTelemetry& s) { return static_cast<float>(s.step); }};
    std::array<Range, 5> columns{};
    history.column_ranges(step_channel, 5, columns);
    CHECK(columns[0].valid);
    CHECK(columns[0].min == 8.0f);
    CHECK(columns[0].max == 8.0f);
    for (int c = 1; c < 5; ++c) {
        CHECK(columns[static_cast<std::size_t>(c)].min == static_cast<float>(7 + c));
        CHECK(columns[static_cast<std::size_t>(c)].max == static_cast<float>(8 + c));
    }
    history.clear();
    CHECK(history.size() == 0);
    t.step = 99;
    history.push(t);
    CHECK(history.at(0).step == 99);
    // La de la partida: 10 s a 240 Hz.
    CHECK(rally::platform::TelemetryHistory().capacity() == 10 * kStepsPerSecond);
}

TEST_CASE("telemetry_history: column ranges give each channel's minimum and maximum") {
    using Range = rally::platform::TelemetryHistory::Range;
    using Channel = rally::platform::TelemetryHistory::Channel;
    rally::platform::TelemetryHistory history(10);
    const std::array<Channel, 2> channels = {
        [](const CarTelemetry& s) { return static_cast<float>(s.step); },
        [](const CarTelemetry& s) { return -static_cast<float>(s.step); },
    };
    std::array<Range, 5 * 2> out{};
    CarTelemetry t;
    // Tres pasos en una historia de 10 repartida en 5 columnas de 2: solo las dos últimas
    // columnas tienen datos (la 3, el paso 1; la 4, los pasos 2 y 3 más el 1 de la anterior).
    for (int i = 1; i <= 3; ++i) {
        t.step = i;
        history.push(t);
    }
    history.column_ranges(channels, 5, out);
    for (int c = 0; c < 3; ++c) {
        CHECK(!out[static_cast<std::size_t>(c * 2)].valid);
    }
    CHECK(out[3 * 2].valid);
    CHECK(out[3 * 2].min == 1.0f);
    CHECK(out[3 * 2].max == 1.0f);
    CHECK(out[4 * 2].min == 1.0f);
    CHECK(out[4 * 2].max == 3.0f);
    CHECK(out[4 * 2 + 1].min == -3.0f);
    CHECK(out[4 * 2 + 1].max == -1.0f);
    // Llena: la columna c tiene los pasos 2c+1 y 2c+2, más el último de la anterior.
    for (int i = 4; i <= 10; ++i) {
        t.step = i;
        history.push(t);
    }
    const std::uint64_t before = rally::tests::global_new_count();
    history.column_ranges(channels, 5, out);
    CHECK(rally::tests::global_new_count() == before);
    CHECK(out[0].min == 1.0f);
    CHECK(out[0].max == 2.0f);
    for (int c = 1; c < 5; ++c) {
        CHECK(out[static_cast<std::size_t>(c * 2)].min == static_cast<float>(2 * c));
        CHECK(out[static_cast<std::size_t>(c * 2)].max == static_cast<float>(2 * c + 2));
        CHECK(out[static_cast<std::size_t>(c * 2 + 1)].max == -static_cast<float>(2 * c));
    }
}

TEST_CASE("telemetry_history: the graphs show beta as 0 below 3 m/s horizontal") {
    CarTelemetry t;
    t.beta = 1.0f;
    t.vx = 2.0f;
    t.vz = -2.0f; // 2,83 m/s
    t.vy = 10.0f; // la vertical no cuenta
    CHECK(rally::platform::graph_beta_deg(t) == 0.0f);
    t.vz = -2.3f; // 3,05 m/s
    CHECK(rally::platform::graph_beta_deg(t) == doctest::Approx(57.29578f));
    // Los paneles por defecto: beta, guiñada, velocidad y entradas aplicadas; caben en el sitio
    // que reserva el dibujo.
    const auto panels = rally::platform::default_graph_panels();
    CHECK(panels.size() == 4);
    CHECK(panels[3].channels.size() == 4);
    std::size_t channels = 0;
    for (const rally::platform::GraphPanel& panel : panels) {
        channels += panel.channels.size();
    }
    CHECK(channels <= static_cast<std::size_t>(rally::platform::kMaxGraphChannels));
}

TEST_CASE("telemetry_history: T does nothing in blind mode and blind mode hides the graphs") {
    rally::platform::GraphToggle toggle;
    CHECK(!toggle.visible(false));
    toggle.press(true); // a ciegas: no hace nada
    CHECK(!toggle.visible(false));
    CHECK(!toggle.visible(true));
    toggle.press(false);
    CHECK(toggle.visible(false));
    CHECK(!toggle.visible(true)); // al entrar en el modo a ciegas desaparecen
    toggle.press(true);
    CHECK(toggle.visible(false)); // y vuelven al salir, sin que T haya cambiado nada
    toggle.press(false);
    CHECK(!toggle.visible(false));
}
