#include "platform/debug_lines.h"

#include "platform/ui_text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rally::platform {

namespace {

// Ancho de la fuente a 10 px: los demás tamaños son múltiplos exactos.
int unit_width(const std::string& s) { return s.empty() ? 0 : text_width(s, 10); }

// Rellena `s` con espacios (4 unidades cada uno) y espacios duros (2) hasta medir lo mismo que
// `like`. Si la diferencia fuera impar se queda 1 corto: lo caza el test.
std::string pad_like(std::string s, const std::string& like) {
    const int target = unit_width(like);
    for (int missing = target - unit_width(s); missing >= 2; missing = target - unit_width(s)) {
        s += missing >= 4 ? " " : "\xC2\xA0";
    }
    return s;
}

double clamp_digits(double value, int int_digits, int decimals) {
    double top = 1.0;
    for (int i = 0; i < int_digits; ++i) {
        top *= 10.0;
    }
    double step = 1.0;
    for (int i = 0; i < decimals; ++i) {
        step *= 0.1;
    }
    const double magnitude = std::fabs(value);
    return std::isnan(magnitude) ? 0.0 : std::min(magnitude, top - step);
}

// «+0350» o «-0350»: signo siempre y las cifras enteras con ceros delante.
std::string signed_field(double value, int int_digits, int decimals) {
    char text[40];
    std::snprintf(text, sizeof text, "%c%0*.*f", value < 0.0 ? '-' : '+',
                  int_digits + (decimals > 0 ? decimals + 1 : 0), decimals,
                  clamp_digits(value, int_digits, decimals));
    return text;
}

std::string unsigned_field(double value, int int_digits, int decimals) {
    char text[40];
    std::snprintf(text, sizeof text, "%0*.*f", int_digits + (decimals > 0 ? decimals + 1 : 0),
                  decimals, clamp_digits(value, int_digits, decimals));
    return text;
}

constexpr double kPi = 3.14159265358979323846;

} // namespace

std::string debug_motor_line(const sim::DrivetrainOutput& d) {
    // Los cuatro estados, de la misma paridad de ancho para poder igualarlos.
    const std::string longest = "arranque, abierto";
    std::string state = d.clutch == sim::ClutchMode::Closed ? "cerrado" : "arranque";
    if (d.clutch_open) {
        state += ", abierto";
    }
    return "motor: " + unsigned_field(static_cast<double>(d.engine_omega) * 30.0 / kPi, 4, 0) +
           " rpm, " + signed_field(static_cast<double>(d.boost) * 1.0e-5, 1, 2) + " bar, " +
           signed_field(static_cast<double>(d.engine_torque), 4, 0) + " N·m (acel. " +
           unsigned_field(static_cast<double>(d.throttle), 1, 2) + "); embrague " +
           pad_like(state, longest) + ", desliz. " +
           signed_field(static_cast<double>(d.clutch_slip), 3, 1) + " rad/s, " +
           signed_field(static_cast<double>(d.clutch_torque), 4, 0) + " N·m";
}

std::string debug_axles_line(const sim::DrivetrainOutput& d) {
    return "ejes: " + signed_field(static_cast<double>(d.axle_torque[0]), 4, 0) + " / " +
           signed_field(static_cast<double>(d.axle_torque[1]), 4, 0) + " N·m; central dw " +
           signed_field(static_cast<double>(d.center_slip), 3, 2) + " rad/s, visco " +
           signed_field(static_cast<double>(d.viscous_torque), 4, 0) + " N·m";
}

// Línea de los autoblocantes (pares delantero y trasero) y las iteraciones del LCP.
std::string debug_lsd_line(const sim::DrivetrainOutput& d) {
    const std::string failed = " SIN CONVERGER";
    return "autobl.: " + unsigned_field(static_cast<double>(d.lsd_torque[0]), 4, 0) + " / " +
           unsigned_field(static_cast<double>(d.lsd_torque[1]), 4, 0) + " N·m; LCP " +
           unsigned_field(d.lcp_iterations, 2, 0) +
           (d.lcp_failures > 0 ? failed : pad_like("", failed));
}

} // namespace rally::platform
