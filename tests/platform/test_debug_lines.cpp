#include "platform/debug_lines.h"
#include "platform/screen_layout.h"
#include "platform/ui_text.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace rally;
using namespace rally::platform;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::array<float, 8> kTorques = {-9999.0f, -350.0f, -0.2f,   0.0f,
                                           0.4f,     7.0f,    1234.0f, 25000.0f};
constexpr std::array<float, 6> kRpm = {0.0f, 5.0f, 999.6f, 1000.0f, 6543.0f, 9999.0f};

float torque(int i) { return kTorques[static_cast<std::size_t>(i) % kTorques.size()]; }

// Un estado del tren motriz que cambia todos los campos con `k` y con la combinación de estados.
sim::DrivetrainOutput state(int k, int mode) {
    sim::DrivetrainOutput d;
    d.engine_omega =
        static_cast<float>(kRpm[static_cast<std::size_t>(k) % kRpm.size()] * kPi / 30.0);
    d.boost = static_cast<float>((k % 4) * 0.37 * 1.0e5);
    d.engine_torque = torque(k);
    d.throttle = static_cast<float>((k % 5) * 0.25);
    d.clutch = (mode & 1) != 0 ? sim::ClutchMode::Closed : sim::ClutchMode::Launch;
    d.clutch_open = (mode & 2) != 0;
    d.clutch_slip = torque(k + 3) * 0.05f;
    d.clutch_torque = torque(k + 1);
    d.axle_torque = {torque(k + 2), torque(k + 5)};
    d.center_slip = torque(k + 4) * 0.01f;
    d.viscous_torque = torque(k + 6);
    d.lsd_torque = {std::fabs(torque(k)), std::fabs(torque(k + 7))};
    d.lcp_iterations = (k * 7) % 100;
    d.lcp_failures = (mode & 4) != 0 ? 3 : 0;
    return d;
}

} // namespace

TEST_CASE("debug_lines: the motor, axle and differential lines measure the same in every state") {
    const sim::DrivetrainOutput first = state(0, 0);
    for (const int font : {10, 20}) {
        const int motor = debug_text_width(debug_motor_line(first), font);
        const int axles = debug_text_width(debug_axles_line(first), font);
        const int lsd = debug_text_width(debug_lsd_line(first), font);
        for (int mode = 0; mode < 8; ++mode) {
            for (int k = 0; k < 64; ++k) {
                const sim::DrivetrainOutput d = state(k, mode);
                INFO("font ", font, " modo ", mode, " k ", k, ": ", debug_motor_line(d), " | ",
                     debug_axles_line(d), " | ", debug_lsd_line(d));
                CHECK(debug_text_width(debug_motor_line(d), font) == motor);
                CHECK(debug_text_width(debug_axles_line(d), font) == axles);
                CHECK(debug_text_width(debug_lsd_line(d), font) == lsd);
            }
        }
    }
}

TEST_CASE("debug_lines: torques always carry a sign and rpm have four digits") {
    sim::DrivetrainOutput d = state(0, 0);
    d.engine_omega = static_cast<float>(850.0 * kPi / 30.0);
    d.engine_torque = 0.0f;
    d.clutch_torque = -120.0f;
    const std::string motor = debug_motor_line(d);
    CHECK(motor.find("motor: 0850 rpm") == 0);
    CHECK(motor.find("+0000 N") != std::string::npos);
    CHECK(motor.find("-0120 N") != std::string::npos);
    CHECK(motor.find("embrague arranque") != std::string::npos);
    d.clutch = sim::ClutchMode::Closed;
    d.clutch_open = true;
    CHECK(debug_motor_line(d).find("embrague cerrado, abierto") != std::string::npos);
    // Fuera de rango se recorta a las cifras del campo, sin ensanchar la línea.
    d.engine_torque = 123456.0f;
    CHECK(debug_motor_line(d).find("+9999 N") != std::string::npos);
}

TEST_CASE("debug_lines: the axle and differential lines are split and keep every field") {
    sim::DrivetrainOutput d = state(0, 0);
    d.axle_torque = {120.0f, -30.0f};
    d.lsd_torque = {45.0f, 6.0f};
    d.lcp_iterations = 7;
    CHECK(debug_axles_line(d).find("ejes: +0120 / -0030 N·m; central") == 0);
    CHECK(debug_axles_line(d).find("autobl.") == std::string::npos);
    CHECK(debug_axles_line(d).find("LCP") == std::string::npos);
    const std::string lsd = debug_lsd_line(d);
    CHECK(lsd.find("autobl.: 0045 / 0006 N·m; LCP 07") == 0);
    CHECK(lsd.find("SIN CONVERGER") == std::string::npos);
    d.lcp_failures = 1;
    CHECK(debug_lsd_line(d).find("LCP 07 SIN CONVERGER") != std::string::npos);
}

TEST_CASE("debug_lines: the worst case of each line fits the F3 content width") {
    // Peor caso: pares de 4 cifras con signo, Δω de 3 enteros, rpm 9999, "SIN CONVERGER".
    sim::DrivetrainOutput d = state(0, 7);
    d.engine_omega = static_cast<float>(9999.0 * kPi / 30.0);
    d.axle_torque = {-9999.0f, -9999.0f};
    d.center_slip = -999.0f;
    d.viscous_torque = -9999.0f;
    d.lsd_torque = {9999.0f, 9999.0f};
    d.lcp_iterations = 99;
    d.lcp_failures = 5;
    for (const auto res : {std::pair{1280, 720}, std::pair{1360, 768}, std::pair{1920, 1080}}) {
        const ScreenLayout layout = layout_screen(res.first, res.second);
        const int content = layout.rect(Layer::Debug).w - 2 * layout.m.padding;
        for (const std::string& line : {debug_axles_line(d), debug_lsd_line(d)}) {
            INFO(res.first, "x", res.second, " fuente ", layout.m.font, " ancho ",
                 debug_text_width(line, layout.m.font), " de ", content, ": ", line);
            CHECK(debug_text_width(line, layout.m.font) <= content);
            CHECK(in_default_font(line));
        }
    }
}
