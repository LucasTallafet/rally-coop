#include "car_worlds.h"
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
constexpr std::uint64_t kExpectedTelemetryHash = 0xa85dbd03a83752b7ull;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = 180.0 / kPi;

double horizontal_speed(const CarTelemetry& t) { return std::hypot(t.vx, t.vz); }

// Zona de pruebas con el coche de referencia en la salida de la recta de asfalto, mirando a -z.
void build_zone(SimWorld& world) {
    rally::sim::WorldDesc desc =
        rally::sim::zone_world(Surface::Gravel, rally::tests::reference_car());
    desc.aids.steer_assist = true; // la de los jugadores por defecto: el hash la cubre
    world.build(desc);
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

TEST_CASE("telemetry: the car driving straight on asphalt has beta below 0.5 degrees") {
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
    // El microrrelieve del asfalto no es simétrico a izquierda y derecha: desvía un poco.
    CHECK(std::abs(world.telemetry().heading) < 0.01f);
    // Las cuatro ruedas en el asfalto (la superficie del material del contacto).
    for (const rally::sim::WheelTelemetry& w : world.telemetry().wheels) {
        CHECK(w.contact == 1);
        CHECK(w.surface == static_cast<std::int8_t>(Surface::Asphalt));
        CHECK(w.load > 1000.0f);
        CHECK(w.omega > 30.0f);
    }
    // La marcha del controlador (2.6: el automático ya ha subido).
    CHECK(world.telemetry().gear == world.car_controller().gear());
    CHECK(world.telemetry().gear >= 1);
}

TEST_CASE("telemetry: the car spun right with the handbrake slides with beta near -90 degrees") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    build_zone(world);
    SimInput throttle;
    throttle.throttle = kAxisMax;
    for (int i = 0; i < 2 * kStepsPerSecond; ++i) {
        world.step(throttle);
    }
    // Suelta, gira a la derecha y tira del freno de mano: el morro gira a la derecha y el coche
    // sigue deslizando hacia donde iba (-z), a la izquierda del morro.
    SimInput spin;
    spin.steer = kAxisMax;
    spin.buttons = rally::sim::kButtonHandbrake;
    int turning = 0;
    while (world.telemetry().heading < kPi / 2.0 && turning < 3 * kStepsPerSecond) {
        world.step(spin);
        ++turning;
    }
    const CarTelemetry& t = world.telemetry();
    // Dirección del movimiento respecto al morro: β = −(rumbo − rumbo de la velocidad).
    const double travel = std::atan2(t.vx, -t.vz);
    const double expected = -(t.heading - travel) * kDeg;
    MESSAGE("de lado: beta ", t.beta * kDeg, "° con rumbo ", t.heading * kDeg,
            "°, la velocidad hacia ", travel * kDeg, "° (", horizontal_speed(t), " m/s); giro de ",
            turning, " pasos");
    REQUIRE(t.heading >= kPi / 2.0);
    CHECK(horizontal_speed(t) > 3.0);
    CHECK(t.beta * kDeg == doctest::Approx(expected).epsilon(0.001));
    CHECK(t.beta * kDeg < -45.0);
    CHECK(t.beta * kDeg > -135.0);
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
            CHECK(t.heading > 0.0f);
        }
    }
    MESSAGE("girando a la derecha: guiñada mínima ", min_yaw, " rad/s, aceleración lateral mínima ",
            min_lateral, " m/s² en ", checked, " pasos; beta final ", world.telemetry().beta * kDeg,
            "°");
    CHECK(min_yaw > 0.0f);
    CHECK(min_lateral > 0.0f);
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
                  "kExpectedTelemetryHash (tests/sim/test_telemetry.cpp) y dilo en el commit.",
                  static_cast<unsigned long long>(hash),
                  static_cast<unsigned long long>(kExpectedTelemetryHash), first.size());
    INFO(message);
    CHECK(hash == kExpectedTelemetryHash);
    // Para la línea de resumen de tools/ci/comprobar.ps1: el valor medido, no el guardado.
    std::printf("RALLY_SUMMARY telemetria=0x%016llx\n", static_cast<unsigned long long>(hash));
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
    CHECK(header.find("# format=2\n") != std::string::npos);
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

namespace {

using Channel = rally::platform::TelemetryHistory::Channel;
constexpr std::array<Channel, 2> kStepChannels = {
    [](const CarTelemetry& s) { return static_cast<float>(s.step); },
    [](const CarTelemetry& s) { return -static_cast<float>(s.step); },
};

void push_steps(rally::platform::TelemetryHistory& history, int first, int last) {
    CarTelemetry t;
    for (int i = first; i <= last; ++i) {
        t.step = i;
        history.push(t);
    }
}

} // namespace

TEST_CASE("telemetry_history: columns cover fixed steps and join the previous column") {
    // 5 columnas de 2 pasos; los pasos valen 1, 2, 3...
    rally::platform::TelemetryHistory history(kStepChannels, 5, 2);
    CHECK(history.columns() == 5);
    CHECK(history.channel_count() == 2);
    CHECK(!history.column(0, 4).valid);
    CHECK(history.latest(0) == 0.0f);
    // Tres pasos: la última columna (a medias) tiene el paso 3 más el 2 de la anterior; la
    // anterior, los pasos 1 y 2; el resto, nada.
    push_steps(history, 1, 3);
    CHECK(history.steps() == 3);
    CHECK(history.latest(0) == 3.0f);
    CHECK(history.latest(1) == -3.0f);
    for (int c = 0; c < 3; ++c) {
        CHECK(!history.column(0, c).valid);
    }
    CHECK(history.column(0, 3).valid);
    CHECK(history.column(0, 3).min == 1.0f);
    CHECK(history.column(0, 3).max == 2.0f);
    CHECK(history.column(0, 4).min == 2.0f);
    CHECK(history.column(0, 4).max == 3.0f);
    CHECK(history.column(1, 4).min == -3.0f);
    CHECK(history.column(1, 4).max == -2.0f);
    // Doce pasos más (15): ha dado la vuelta. Las columnas absolutas visibles son la 3 a la 7:
    // la 7 tiene el 15 más el 14; la c, los pasos 2c+1 y 2c+2 más el 2c.
    const std::uint64_t before = rally::tests::global_new_count();
    push_steps(history, 4, 15);
    CHECK(rally::tests::global_new_count() == before);
    CHECK(history.column(0, 4).min == 14.0f);
    CHECK(history.column(0, 4).max == 15.0f);
    for (int c = 0; c < 4; ++c) {
        const int absolute = c + 3;
        CHECK(history.column(0, c).valid);
        CHECK(history.column(0, c).min == static_cast<float>(2 * absolute));
        CHECK(history.column(0, c).max == static_cast<float>(2 * absolute + 2));
        CHECK(history.column(1, c).max == -static_cast<float>(2 * absolute));
    }
    history.clear();
    CHECK(history.steps() == 0);
    CHECK(!history.column(0, 4).valid);
    push_steps(history, 99, 99);
    CHECK(history.column(0, 4).min == 99.0f); // sin el paso anterior a clear()
    CHECK(history.column(0, 4).max == 99.0f);
}

TEST_CASE("telemetry_history: visible columns match column by column") {
    using Range = rally::platform::TelemetryHistory::Range;
    rally::platform::TelemetryHistory history(kStepChannels, 7, 3);
    std::array<Range, 7> out{};
    const auto same = [&history, &out]() {
        for (int k = 0; k < 2; ++k) {
            history.visible_columns(k, out);
            for (int c = 0; c < 7; ++c) {
                const Range expected = history.column(k, c);
                const Range& got = out[static_cast<std::size_t>(c)];
                CHECK(got.valid == expected.valid);
                if (expected.valid) {
                    CHECK(got.min == expected.min);
                    CHECK(got.max == expected.max);
                }
            }
        }
    };
    same(); // vacía
    int pushed = 0;
    for (const int more : {1, 4, 9, 30, 2}) { // parcial, y después dando la vuelta
        push_steps(history, pushed + 1, pushed + more);
        pushed += more;
        same();
    }
}

TEST_CASE("telemetry_history: the game history holds 10 s and every page channel") {
    const rally::platform::TelemetryHistory history;
    CHECK(history.columns() * rally::platform::kGraphStepsPerColumn == 10 * kStepsPerSecond);
    CHECK(history.channel_count() ==
          static_cast<int>(rally::platform::default_graph_channels().size()));
    // Páginas: 1 · manejo (beta con la deriva media de cada eje, guiñada, velocidad, entradas
    // aplicadas), 2 · tren motriz (rpm y marcha, presión del turbo y κ_r por rueda) y
    // 3 · suspensión (carga y compresión por rueda).
    const auto pages = rally::platform::default_graph_pages();
    REQUIRE(pages.size() == 3);
    CHECK(std::string(pages[0].title) == "manejo");
    CHECK(pages[0].panels.size() == 4);
    CHECK(pages[0].panels[0].channels.size() == 3);
    CHECK(pages[0].panels[3].channels.size() == 4);
    CHECK(std::string(pages[1].title) == "tren motriz");
    REQUIRE(pages[1].panels.size() == 3);
    CHECK(pages[1].panels[0].channels.size() == 2);
    CHECK(pages[1].panels[1].channels.size() == 1);
    CHECK(pages[1].panels[2].channels.size() == 4);
    CHECK(std::string(pages[2].title) == "suspensión");
    REQUIRE(pages[2].panels.size() == 2);
    CHECK(pages[2].panels[0].channels.size() == 4);
    CHECK(pages[2].panels[1].channels.size() == 4);
    CHECK(rally::platform::first_graph_channel(0) == 0);
    CHECK(rally::platform::first_graph_channel(1) == 9);
    CHECK(rally::platform::first_graph_channel(2) == 16);
    CHECK(rally::platform::default_graph_channels().size() <=
          static_cast<std::size_t>(rally::platform::kMaxGraphChannels));
    // El orden de los canales es el de los paneles: el 5 es la velocidad en km/h.
    CarTelemetry t;
    t.speed = 10.0f;
    CHECK(rally::platform::default_graph_channels()[4](t) == doctest::Approx(36.0f));
    // La deriva de cada eje lleva el signo de beta: ruedas yendo hacia su derecha (α < 0) dan
    // un valor positivo, como beta con la velocidad a la derecha del morro.
    t.wheels[0].alpha = -0.1f;
    t.wheels[1].alpha = -0.3f;
    CHECK(rally::platform::default_graph_channels()[1](t) == doctest::Approx(11.459156f));
    t.wheels[3].kappa = 0.25f;
    CHECK(rally::platform::default_graph_channels()[15](t) == doctest::Approx(25.0f));
    // Página 2: rpm en miles, marcha y presión en bar.
    t.engine_omega = 3.14159265f * 100.0f; // 3000 rpm
    t.gear = 3;
    t.boost = 1.2e5f;
    CHECK(rally::platform::default_graph_channels()[9](t) == doctest::Approx(3.0f));
    CHECK(rally::platform::default_graph_channels()[10](t) == doctest::Approx(3.0f));
    CHECK(rally::platform::default_graph_channels()[11](t) == doctest::Approx(1.2f));
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
}

TEST_CASE("telemetry_history: T pages through the pages with panels and then hides them") {
    using rally::platform::GraphPage;
    using rally::platform::GraphPanel;
    const std::array<GraphPanel, 1> panel = {GraphPanel{"p", "", 0.0f, 1.0f, {}}};
    // Tres páginas con paneles: 1 → 2 → 3 → ocultas → 1.
    const std::array<GraphPage, 3> full = {GraphPage{"a", panel}, GraphPage{"b", panel},
                                           GraphPage{"c", panel}};
    rally::platform::GraphPager pager;
    CHECK(pager.visible(false) == -1);
    for (const int expected : {0, 1, 2, -1, 0}) {
        pager.press(false, full);
        CHECK(pager.visible(false) == expected);
    }
    // Las páginas sin paneles se saltan.
    const std::array<GraphPage, 3> gap = {GraphPage{"a", panel}, GraphPage{"b", {}},
                                          GraphPage{"c", panel}};
    rally::platform::GraphPager skipping;
    for (const int expected : {0, 2, -1}) {
        skipping.press(false, gap);
        CHECK(skipping.visible(false) == expected);
    }
    // Con las de la partida (desde la 2.5 la 2 tiene paneles), T muestra la 1, la 2, la 3 y
    // las oculta.
    rally::platform::GraphPager game;
    for (const int expected : {0, 1, 2, -1}) {
        game.press(false, rally::platform::default_graph_pages());
        CHECK(game.visible(false) == expected);
    }
}

TEST_CASE("telemetry_history: T does nothing in blind mode and blind mode hides the graphs") {
    const auto pages = rally::platform::default_graph_pages();
    rally::platform::GraphPager pager;
    pager.press(true, pages); // a ciegas: no hace nada
    CHECK(pager.visible(false) == -1);
    CHECK(pager.visible(true) == -1);
    pager.press(false, pages);
    CHECK(pager.visible(false) == 0);
    CHECK(pager.visible(true) == -1); // al entrar en el modo a ciegas desaparecen
    pager.press(true, pages);
    CHECK(pager.visible(false) == 0); // y vuelven al salir, sin que T haya cambiado nada
}

TEST_CASE("telemetry_history: merged columns keep the exact envelope") {
    using Range = rally::platform::TelemetryHistory::Range;
    // 480 columnas → 320: cada una de las nuevas junta 1 o 2 de las de la historia.
    std::vector<Range> in(rally::platform::kGraphColumns);
    for (std::size_t c = 0; c < in.size(); ++c) {
        const float v = static_cast<float>(c);
        in[c] = Range{v, v + 0.5f, c >= 10}; // las 10 primeras, sin pasos
    }
    in[301] = Range{-50.0f, 1000.0f, true}; // un pico de un paso
    std::vector<Range> out(320);
    rally::platform::merge_columns(in, out);
    float lowest = 1e9f;
    float highest = -1e9f;
    int valid = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const std::size_t first = i * in.size() / out.size();
        const std::size_t last = (i + 1) * in.size() / out.size();
        bool any = false;
        float lo = 1e9f;
        float hi = -1e9f;
        for (std::size_t c = first; c < last; ++c) {
            if (in[c].valid) {
                any = true;
                lo = std::min(lo, in[c].min);
                hi = std::max(hi, in[c].max);
            }
        }
        CHECK(out[i].valid == any);
        if (any) {
            CHECK(out[i].min == lo);
            CHECK(out[i].max == hi);
            lowest = std::min(lowest, out[i].min);
            highest = std::max(highest, out[i].max);
            ++valid;
        }
    }
    CHECK(lowest == -50.0f);
    // La columna que cubre el pico lo conserva entero.
    std::size_t spike = 0;
    while ((spike + 1) * in.size() / out.size() <= 301) {
        ++spike;
    }
    CHECK(out[spike].min == -50.0f);
    CHECK(out[spike].max == 1000.0f);
    CHECK(highest == 1000.0f);
    CHECK(valid == 313); // las 7 primeras cubren solo columnas 0..9, sin pasos
}

TEST_CASE("telemetry_history: merging into the same width copies the columns") {
    using Range = rally::platform::TelemetryHistory::Range;
    std::vector<Range> in(8);
    for (std::size_t c = 0; c < in.size(); ++c) {
        in[c] = Range{static_cast<float>(c), static_cast<float>(c) + 1.0f, c % 3 != 0};
    }
    std::vector<Range> out(8);
    rally::platform::merge_columns(in, out);
    for (std::size_t c = 0; c < in.size(); ++c) {
        CHECK(out[c].valid == in[c].valid);
        if (in[c].valid) {
            CHECK(out[c].min == in[c].min);
            CHECK(out[c].max == in[c].max);
        }
    }
}
