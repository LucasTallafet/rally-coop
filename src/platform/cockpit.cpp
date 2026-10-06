#include "platform/cockpit.h"

#include "platform/seat.h"

#include <array>
#include <cmath>

namespace rally::platform {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Paleta del interior (colores planos, oscuros para no distraer de la carretera).
constexpr CockpitColor kRoof{58, 58, 62};
constexpr CockpitColor kDash{32, 32, 35};
constexpr CockpitColor kDashFace{26, 26, 28};
constexpr CockpitColor kPillar{44, 44, 48};
constexpr CockpitColor kDoor{50, 50, 54};
constexpr CockpitColor kRim{22, 22, 22};
constexpr CockpitColor kSpoke{70, 70, 74};
constexpr CockpitColor kTape{230, 190, 40};
constexpr CockpitColor kScreen{10, 12, 14};
constexpr CockpitColor kSegmentOn{255, 170, 40};
constexpr CockpitColor kSegmentOff{30, 26, 22};
constexpr CockpitColor kRpm{90, 200, 90};
constexpr CockpitColor kRpmRed{230, 60, 50};
constexpr CockpitColor kBoost{80, 150, 240};
constexpr CockpitColor kBarOff{28, 30, 34};

// Por debajo de esto (m bajo el ojo) ya no se ve nada: las caras bajan hasta aquí.
constexpr double kFloorBelow = 0.9;

Vec3f to_f(const Vec3d& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

void quad(std::vector<CockpitTri>& out, const Vec3d& a, const Vec3d& b, const Vec3d& c,
          const Vec3d& d, CockpitColor color) {
    out.push_back({to_f(a), to_f(b), to_f(c), color});
    out.push_back({to_f(a), to_f(c), to_f(d), color});
}

Vec3d add(const Vec3d& a, const Vec3d& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3d scale(const Vec3d& a, double s) { return {a.x * s, a.y * s, a.z * s}; }

// Plano del volante o de la pantalla: centro y ejes (u a la derecha, v hacia arriba del plano).
struct Plane {
    Vec3d center;
    Vec3d u;
    Vec3d v;
    Vec3d at(double x, double y) const { return add(center, add(scale(u, x), scale(v, y))); }
    void rect(std::vector<CockpitTri>& out, double x0, double y0, double x1, double y1,
              CockpitColor color) const {
        quad(out, at(x0, y0), at(x1, y0), at(x1, y1), at(x0, y1), color);
    }
};

// Plano inclinado `tilt` respecto a la vertical, con la parte de arriba hacia el conductor.
Plane tilted_plane(const Vec3d& center, double tilt) {
    return {center, {1.0, 0.0, 0.0}, {0.0, std::cos(tilt), std::sin(tilt)}};
}

} // namespace

Vec3d cockpit_point(const sim::CarData& car, double ahead, double right, double up) {
    const Vec3d eye = car_eye(car);
    return {eye.x + right, eye.y + up, eye.z - ahead};
}

std::vector<CockpitTri> build_cockpit(const sim::CarData& car) {
    const sim::CockpitData& k = car.cockpit;
    std::vector<CockpitTri> out;
    const auto p = [&car](double ahead, double right, double up) {
        return cockpit_point(car, ahead, right, up);
    };
    const double left = -k.door_left;
    const double right = k.door_right;
    const double base_up = -k.windshield_base_below;
    const double top_up = k.windshield_top_above;

    // Techo: del borde superior del parabrisas hacia atrás.
    quad(out, p(k.windshield_top_ahead, left, top_up), p(k.windshield_top_ahead, right, top_up),
         p(-k.roof_behind, right, k.roof_above), p(-k.roof_behind, left, k.roof_above), kRoof);
    // Salpicadero: arriba, de la base del parabrisas a su borde trasero, y la cara de atrás.
    quad(out, p(k.windshield_base_ahead, left, base_up), p(k.windshield_base_ahead, right, base_up),
         p(k.dash_rear_ahead, right, base_up), p(k.dash_rear_ahead, left, base_up), kDash);
    quad(out, p(k.dash_rear_ahead, left, base_up), p(k.dash_rear_ahead, right, base_up),
         p(k.dash_rear_ahead, right, -kFloorBelow), p(k.dash_rear_ahead, left, -kFloorBelow),
         kDashFace);
    // Montantes: de la base al borde superior del parabrisas, centrados en pillar_left/right.
    const double w = 0.5 * k.pillar_width;
    for (const double c :
         {-static_cast<double>(k.pillar_left), static_cast<double>(k.pillar_right)}) {
        quad(out, p(k.windshield_base_ahead, c - w, base_up),
             p(k.windshield_base_ahead, c + w, base_up), p(k.windshield_top_ahead, c + w, top_up),
             p(k.windshield_top_ahead, c - w, top_up), kPillar);
    }
    // Puertas: de la ventanilla hacia abajo, del parabrisas hacia atrás.
    for (const double side : {left, right}) {
        quad(out, p(k.windshield_base_ahead, side, -k.sill_below),
             p(-k.roof_behind, side, -k.sill_below), p(-k.roof_behind, side, -kFloorBelow),
             p(k.windshield_base_ahead, side, -kFloorBelow), kDoor);
        // Entre el montante y la puerta, el lateral bajo la base del parabrisas.
        quad(out, p(k.windshield_base_ahead, side, base_up),
             p(k.windshield_base_ahead, side, -k.sill_below),
             p(k.dash_rear_ahead, side, -k.sill_below), p(k.dash_rear_ahead, side, base_up), kDoor);
    }
    return out;
}

double steering_wheel_angle(float steer_applied, const sim::CarData& car) {
    return static_cast<double>(steer_applied) * static_cast<double>(car.max_steer) *
           static_cast<double>(car.cockpit.steering_ratio);
}

void append_steering_wheel(std::vector<CockpitTri>& out, const sim::CarData& car, double angle) {
    const sim::CockpitData& k = car.cockpit;
    const Plane plane = tilted_plane(
        cockpit_point(car, k.wheel_ahead, 0.0, -static_cast<double>(k.wheel_center_below)),
        k.wheel_tilt);
    const double r = 0.5 * k.wheel_diameter;
    constexpr double kRimHalf = 0.016;
    // Girar a la derecha es girar en el sentido de las agujas del reloj visto desde el asiento.
    const auto rim_point = [&](double phi, double radius) {
        return plane.at(radius * std::cos(phi - angle), radius * std::sin(phi - angle));
    };
    for (int i = 0; i < kWheelSegments; ++i) {
        const double a0 = 2.0 * kPi * i / kWheelSegments;
        const double a1 = 2.0 * kPi * (i + 1) / kWheelSegments;
        quad(out, rim_point(a0, r - kRimHalf), rim_point(a0, r + kRimHalf),
             rim_point(a1, r + kRimHalf), rim_point(a1, r - kRimHalf), kRim);
    }
    // Marca de arriba (a las 12 con las ruedas rectas).
    constexpr double kTapeHalf = 0.09;
    quad(out, rim_point(0.5 * kPi - kTapeHalf, r - kRimHalf * 1.2),
         rim_point(0.5 * kPi - kTapeHalf, r + kRimHalf * 1.2),
         rim_point(0.5 * kPi + kTapeHalf, r + kRimHalf * 1.2),
         rim_point(0.5 * kPi + kTapeHalf, r - kRimHalf * 1.2), kTape);
    // Radios a las 3, a las 9 y a las 6, y el buje.
    constexpr double kSpokeHalf = 0.08;
    const double hub = 0.045;
    for (const double phi : {0.0, kPi, 1.5 * kPi}) {
        quad(out, rim_point(phi - kSpokeHalf * 0.5, hub), rim_point(phi - 0.03, r - kRimHalf),
             rim_point(phi + 0.03, r - kRimHalf), rim_point(phi + kSpokeHalf * 0.5, hub), kSpoke);
    }
    for (int i = 0; i < 8; ++i) {
        const double a0 = 2.0 * kPi * i / 8.0;
        const double a1 = 2.0 * kPi * (i + 1) / 8.0;
        out.push_back(
            {to_f(plane.center), to_f(rim_point(a0, hub)), to_f(rim_point(a1, hub)), kSpoke});
    }
}

void append_display(std::vector<CockpitTri>& out, const sim::CarData& car,
                    const CockpitDisplay& d) {
    const sim::CockpitData& k = car.cockpit;
    const Vec3d center =
        cockpit_point(car, k.display_ahead, k.display_right, -static_cast<double>(k.display_below));
    // De cara al ojo: el plano se inclina lo que baja la línea del ojo al centro.
    const double tilt = std::atan2(static_cast<double>(k.display_below), k.display_ahead);
    const Plane plane = tilted_plane(center, -tilt);
    const double hw = 0.5 * k.display_width;
    const double hh = 0.5 * k.display_height;
    // Fondo, un poco detrás del contenido (por el plano: 1 mm más lejos del ojo).
    const Plane back{add(center, {0.0, 0.0, -0.001}), plane.u, plane.v};
    back.rect(out, -hw, -hh, hw, hh, kScreen);

    // Marcha: 7 segmentos en el tercio izquierdo.
    // Segmentos a (arriba), b, c (derecha), d (abajo), e, f (izquierda), g (medio).
    constexpr std::array<std::uint8_t, 7> kDigits = {
        0b1010000, // r: e, g
        0b1010100, // n: c, e, g
        0b0000110, // 1: b, c
        0b1011011, // 2
        0b1001111, // 3
        0b1100110, // 4
        0b1101101, // 5
    };
    const std::uint8_t mask = kDigits[static_cast<std::size_t>(d.gear + 1)];
    const double dh = 0.8 * hh; // media altura del dígito
    const double dw = 0.45 * dh;
    const double cx = -hw + 0.18 * k.display_width;
    const double t = 0.12 * dh; // grosor
    struct Segment {
        double x0, y0, x1, y1;
    };
    const std::array<Segment, 7> segments = {{
        {cx - dw, dh - t, cx + dw, dh},        // a
        {cx + dw - t, 0.0, cx + dw, dh},       // b
        {cx + dw - t, -dh, cx + dw, 0.0},      // c
        {cx - dw, -dh, cx + dw, -dh + t},      // d
        {cx - dw, -dh, cx - dw + t, 0.0},      // e
        {cx - dw, 0.0, cx - dw + t, dh},       // f
        {cx - dw, -0.5 * t, cx + dw, 0.5 * t}, // g
    }};
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const bool on = (mask >> i & 1u) != 0;
        const Segment& s = segments[i];
        plane.rect(out, s.x0, s.y0, s.x1, s.y1, on ? kSegmentOn : kSegmentOff);
    }
    // Barras: rpm arriba y presión abajo, en los dos tercios derechos.
    const double x0 = -hw + 0.38 * k.display_width;
    const double x1 = hw - 0.06 * k.display_width;
    const double bar = 0.28 * k.display_height;
    const auto fill = [&](double y0, double fraction, double red_from, CockpitColor color) {
        const double f = std::fmin(std::fmax(fraction, 0.0), 1.0);
        const double xf = x0 + (x1 - x0) * f;
        plane.rect(out, x0, y0, x1, y0 + bar, kBarOff);
        const double xr = x0 + (x1 - x0) * red_from;
        if (xf <= xr) {
            plane.rect(out, x0, y0, xf, y0 + bar, color);
        } else {
            plane.rect(out, x0, y0, xr, y0 + bar, color);
            plane.rect(out, xr, y0, xf, y0 + bar, kRpmRed);
        }
    };
    // Las barras van 0,5 mm por delante del fondo.
    fill(0.08 * k.display_height, d.rpm_fraction, d.red_fraction, kRpm);
    if (d.turbo) {
        fill(-0.08 * k.display_height - bar, d.boost_fraction, 1.0, kBoost);
    }
}

ScreenFraction project_from_eye(double ahead, double right, double up, double fovy, double aspect) {
    const double t = std::tan(0.5 * fovy);
    const double ndc_x = right / ahead / (t * aspect);
    const double ndc_y = up / ahead / t;
    return {0.5 + 0.5 * ndc_x, 0.5 - 0.5 * ndc_y};
}

} // namespace rally::platform
