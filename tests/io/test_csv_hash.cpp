#include "io/telemetry_csv.h"
#include "sim/fnv1a.h"
#include "sim/pcg32.h"
#include "sim/sim_input.h"
#include "sim/surface.h"
#include "sim/telemetry.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace {

using rally::sim::CarTelemetry;
using rally::sim::Pcg32;

// FNV-1a del CSV (cabecera y 64 filas) de telemetría sintética fija: no pasa por la simulación,
// así que solo cambia cuando cambia el CSV (columnas, orden, precisión o formato de los
// números). Tiene que ser el mismo en Debug y en Release. Si cambia a propósito, se copia aquí
// el valor nuevo y va en el mensaje del commit («CSV hash: 0x... -> 0x...»). El hash de
// la telemetría de la simulación es otro: kExpectedTelemetryHash (tests/sim).
constexpr std::uint64_t kExpectedCsvHash = 0xdc7d3a061d9f3d6bull;

float real(Pcg32& rng, float scale) { return (rng.next_float01() - 0.5f) * scale; }

std::int8_t surface_index(Pcg32& rng) {
    // Uno de cada seis: sin superficie (-1).
    const std::uint32_t pick = rng.bounded(6);
    return pick == 5 ? std::int8_t{-1} : static_cast<std::int8_t>(pick);
}

// Una telemetría con todos los campos rellenados (ninguno a 0 salvo que el azar lo diga), en
// el orden de CarTelemetry. Un campo nuevo sin rellenar aquí sale a 0 en todas las filas.
CarTelemetry synthetic(Pcg32& rng, std::int64_t step) {
    CarTelemetry t;
    t.step = step;
    t.x = static_cast<double>(real(rng, 2000.0f));
    t.y = static_cast<double>(real(rng, 20.0f));
    t.z = static_cast<double>(real(rng, 2000.0f));
    t.vx = real(rng, 80.0f);
    t.vy = real(rng, 8.0f);
    t.vz = real(rng, 80.0f);
    t.v_forward = real(rng, 80.0f);
    t.v_right = real(rng, 20.0f);
    t.v_up = real(rng, 8.0f);
    t.speed = rng.next_float01() * 60.0f;
    t.heading = real(rng, 6.28f);
    t.yaw_rate = real(rng, 4.0f);
    t.beta = real(rng, 6.28f);
    t.ax = real(rng, 40.0f);
    t.ay = real(rng, 40.0f);
    t.az = real(rng, 40.0f);
    t.a_forward = real(rng, 40.0f);
    t.a_right = real(rng, 40.0f);
    t.target.steer = static_cast<std::int16_t>(static_cast<int>(rng.bounded(65535)) - 32767);
    t.target.throttle = static_cast<std::int16_t>(rng.bounded(32768));
    t.target.brake = static_cast<std::int16_t>(rng.bounded(32768));
    t.target.buttons = static_cast<std::uint16_t>(rng.bounded(256));
    t.steer = real(rng, 2.0f);
    t.throttle = rng.next_float01();
    t.brake = rng.next_float01();
    t.handbrake = rng.next_float01();
    t.pressed = static_cast<std::uint16_t>(rng.bounded(256));
    t.steer_aided = real(rng, 2.0f);
    t.steer_digital = rng.bounded(2) == 1;
    t.assist_active = rng.bounded(2) == 1;
    t.assist_rear_sliding = rng.bounded(2) == 1;
    t.assist_right_max = rng.next_float01();
    t.assist_left_max = rng.next_float01();
    t.steer_speed = rng.next_float01() * 60.0f;
    t.surface = surface_index(rng);
    t.laps = static_cast<std::int32_t>(rng.bounded(12));
    t.lap_steps = rng.bounded(5) == 0 ? -1 : static_cast<std::int64_t>(rng.bounded(40000));
    t.road_steps = rng.bounded(5) == 0 ? -1 : static_cast<std::int64_t>(rng.bounded(80000));
    t.road_resets = static_cast<std::int32_t>(rng.bounded(5));
    t.road_runs = static_cast<std::int32_t>(rng.bounded(9));
    t.impact = rng.next_float01() * 6.0f;
    t.gear = static_cast<std::int8_t>(static_cast<int>(rng.bounded(7)) - 1);
    t.target_gear = static_cast<std::int8_t>(static_cast<int>(rng.bounded(7)) - 1);
    t.gearbox = static_cast<std::uint8_t>(rng.bounded(2));
    t.shifting = rng.bounded(2) == 1;
    t.clutch_mode = static_cast<std::uint8_t>(rng.bounded(2));
    t.clutch_open = rng.bounded(2) == 1;
    t.engine_omega = rng.next_float01() * 900.0f;
    t.boost = rng.next_float01() * 180000.0f;
    t.engine_throttle = rng.next_float01();
    t.engine_torque = real(rng, 600.0f);
    t.clutch_slip = real(rng, 300.0f);
    t.clutch_torque = real(rng, 1200.0f);
    t.axle_torque_front = real(rng, 2000.0f);
    t.axle_torque_rear = real(rng, 2000.0f);
    t.center_slip = real(rng, 40.0f);
    t.viscous_torque = real(rng, 800.0f);
    t.lsd_torque_front = rng.next_float01() * 500.0f;
    t.lsd_torque_rear = rng.next_float01() * 500.0f;
    t.drift_hold = rng.bounded(2) == 1;
    t.lcp_iterations = static_cast<std::uint8_t>(1 + rng.bounded(8));
    t.lcp_failures = static_cast<std::uint8_t>(rng.bounded(5));
    t.traction_torque_cut = rng.next_float01() * 400.0f;
    for (rally::sim::WheelTelemetry& w : t.wheels) {
        w.contact = static_cast<std::uint8_t>(rng.bounded(2));
        w.surface = surface_index(rng);
        w.load = rng.next_float01() * 6000.0f;
        w.compression = rng.next_float01() * 0.25f;
        w.kappa = real(rng, 2.0f);
        w.alpha = real(rng, 1.2f);
        w.slip_x = real(rng, 20.0f);
        w.slip_y = real(rng, 20.0f);
        w.fx = real(rng, 8000.0f);
        w.fy = real(rng, 8000.0f);
        w.fx_pure = real(rng, 8000.0f);
        w.fy_pure = real(rng, 8000.0f);
        w.mu = rng.next_float01() * 1.6f;
        w.omega = real(rng, 300.0f);
        w.brake_torque = rng.next_float01() * 3000.0f;
        w.h = real(rng, 0.1f);
        w.abs_torque_cut = rng.next_float01() * 1500.0f;
    }
    return t;
}

std::string synthetic_csv() {
    Pcg32 rng(0xc5f0'0000'0000'0001ull, 7u);
    std::string csv = rally::io::telemetry_csv_header(
        "synthetic.rrec", true, rally::sim::Surface::Gravel, rally::sim::Surface::Asphalt);
    for (std::int64_t step = 1; step <= 64; ++step) {
        rally::io::append_telemetry_row(synthetic(rng, step), csv);
    }
    return csv;
}

} // namespace

TEST_CASE("csv: the synthetic telemetry CSV has the stored hash") {
    const std::string csv = synthetic_csv();
    CHECK(csv == synthetic_csv());
    const std::uint64_t hash = rally::sim::fnv1a64(csv);
    char message[300];
    std::snprintf(message, sizeof(message),
                  "hash del CSV 0x%016llxull, guardado 0x%016llxull. Si cambia el CSV a propósito, "
                  "copia el actual en kExpectedCsvHash (tests/io/test_csv_hash.cpp), regenera "
                  "tools/telemetry/columns.md y dilo en el commit.",
                  static_cast<unsigned long long>(hash),
                  static_cast<unsigned long long>(kExpectedCsvHash));
    INFO(message);
    CHECK(hash == kExpectedCsvHash);
    std::printf("RALLY_SUMMARY csv=0x%016llx\n", static_cast<unsigned long long>(hash));
}

TEST_CASE("csv: the ABS and traction control columns are in the table, once each") {
    std::set<std::string> names;
    for (const rally::io::TelemetryColumn& column : rally::io::telemetry_columns()) {
        names.insert(column.name);
    }
    for (const char* name : {"abs_torque_cut_fl", "abs_torque_cut_fr", "abs_torque_cut_rl",
                             "abs_torque_cut_rr", "traction_torque_cut"}) {
        CHECK_MESSAGE(names.count(name) == 1, name);
    }
}

TEST_CASE("csv: every field of the synthetic telemetry reaches a column") {
    // Un campo de CarTelemetry sin columna: cambiarlo no cambia ninguna fila. Se comprueba
    // columna por columna: cada una cambia el CSV al cambiar un telemetría con 0 por otra
    // rellenada, salvo las que valen 0 por construcción (el paso del segundo CSV es otro).
    Pcg32 rng(0xc5f0'0000'0000'0002ull, 7u);
    CarTelemetry filled = synthetic(rng, 5);
    // Lo que el azar puede dejar igual que la telemetría vacía (0, none, falso), a la fuerza.
    filled.steer_digital = filled.assist_active = filled.assist_rear_sliding = true;
    filled.shifting = filled.clutch_open = filled.drift_hold = true;
    filled.surface = static_cast<std::int8_t>(rally::sim::Surface::Snow);
    filled.laps = 3;
    filled.lap_steps = 2400;
    filled.road_steps = 3000;
    filled.road_resets = 2;
    filled.road_runs = 4;
    filled.gear = 3;
    filled.target_gear = 4;
    filled.lcp_failures = 2;
    filled.gearbox = 1;
    filled.clutch_mode = 1;
    filled.target.buttons = 5;
    filled.pressed = 3;
    for (rally::sim::WheelTelemetry& w : filled.wheels) {
        w.contact = 1;
        w.surface = static_cast<std::int8_t>(rally::sim::Surface::Dirt);
    }
    std::string zero_row;
    std::string filled_row;
    rally::io::append_telemetry_row(CarTelemetry{}, zero_row);
    rally::io::append_telemetry_row(filled, filled_row);
    const auto split = [](const std::string& row) {
        std::vector<std::string> out;
        std::string current;
        for (const char c : row.substr(0, row.size() - 1)) {
            if (c == ',') {
                out.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
        out.push_back(current);
        return out;
    };
    const std::vector<std::string> a = split(zero_row);
    const std::vector<std::string> b = split(filled_row);
    const auto columns = rally::io::telemetry_columns();
    REQUIRE(a.size() == columns.size());
    REQUIRE(b.size() == columns.size());
    for (std::size_t i = 0; i < columns.size(); ++i) {
        INFO("columna ", std::string(columns[i].name));
        CHECK(a[i] != b[i]);
    }
}
