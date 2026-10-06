// Neumático de la 1.0 (tarea 2.5): forma de la curva, sensibilidad a la carga, combinada,
// dirección de la fuerza, régimen estacionario de step_tire, relajación y huella sin cuerda,
// con el coche congelado tests/data/cars/reference.car.

#include "car_worlds.h"
#include "sim/car_data.h"
#include "sim/sim_world.h"
#include "sim/tire.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace {

using rally::sim::CarData;
using rally::sim::Surface;
using rally::sim::TireForce;
using rally::sim::TireInput;
using rally::sim::TireSurface;
using rally::sim::WheelDynamics;
using rally::tests::reference_car;
using rally::tests::reference_car_text;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr float kSubstep = rally::sim::kStepSeconds / rally::sim::kWheelSubsteps;
constexpr int kSubstepsPerSecond = rally::sim::kStepsPerSecond * rally::sim::kWheelSubsteps;

constexpr std::array kSurfaces = {Surface::Asphalt, Surface::Dirt, Surface::Gravel, Surface::Snow,
                                  Surface::Sand};

double nominal() { return reference_car().nominal_load; }

// Fuerzas estacionarias de la curva a F_z0.
double lateral(const TireSurface& t, double alpha, double load) {
    return rally::sim::tire_force(t, 0.0f, static_cast<float>(std::tan(alpha)),
                                  static_cast<float>(load), static_cast<float>(nominal()))
        .fy;
}
double longitudinal(const TireSurface& t, double kappa, double load) {
    return rally::sim::tire_force(t, static_cast<float>(kappa), 0.0f, static_cast<float>(load),
                                  static_cast<float>(nominal()))
        .fx;
}

// Ángulo (grados) del vector (x, y).
double angle_deg(double x, double y) { return std::atan2(y, x) / kDeg; }

// Diferencia de ángulos en grados, en (-180, 180].
double angle_diff(double a, double b) {
    double d = std::fmod(a - b, 360.0);
    if (d > 180.0) {
        d -= 360.0;
    }
    if (d <= -180.0) {
        d += 360.0;
    }
    return d;
}

// Rueda en una superficie con carga F_z0 y velocidad (vx, vy) en el contacto.
TireInput wheel_input(Surface surface, float vx, float vy) {
    TireInput in;
    in.vx = vx;
    in.vy = vy;
    in.load = reference_car().nominal_load;
    in.surface = surface;
    in.radius = reference_car().wheel_radius;
    return in;
}

// Rueda girando libre (sin par) `seconds` s con la velocidad fija: la última fuerza.
TireForce settle(const TireInput& in, WheelDynamics& state, double seconds) {
    TireForce f;
    const int substeps = static_cast<int>(seconds * kSubstepsPerSecond);
    for (int s = 0; s < substeps; ++s) {
        f = rally::sim::step_tire(reference_car(), in, kSubstep, true, state);
    }
    return f;
}

} // namespace

// --- Forma -----------------------------------------------------------------------------------

TEST_CASE("tire: C is the root between 1 and 2 and the peak of the curve sits at rho 1") {
    const CarData& car = reference_car();
    // C = 2 − (2/π)·asin(slide_ratio) (2.5-inicio §2).
    const std::array<double, 5> expected_c = {1.410, 1.460, 1.287, 1.287, 1.202};
    for (std::size_t i = 0; i < kSurfaces.size(); ++i) {
        const TireSurface& t = car.tire(kSurfaces[i]);
        MESSAGE(std::string(rally::sim::surface_name(kSurfaces[i])), ": C = ", t.shape_c,
                ", B = ", t.shape_b, ", E = ", t.curvature_e, ", ρ máximo a velocidad ",
                t.slide_cap);
        CHECK(t.shape_c == doctest::Approx(expected_c[i]).epsilon(0.001));
        CHECK(rally::sim::tire_curve(t, 1.0f) == doctest::Approx(1.0).epsilon(1e-5));
        CHECK(std::abs(rally::sim::tire_curve_slope(t, 1.0f)) < 1e-4);
        // Pendiente analítica frente a la diferencia finita.
        for (const float rho : {0.3f, 0.8f, 1.7f, 4.0f}) {
            const double numeric =
                (rally::sim::tire_curve(t, rho + 1e-3f) - rally::sim::tire_curve(t, rho - 1e-3f)) /
                2e-3;
            CHECK(rally::sim::tire_curve_slope(t, rho) == doctest::Approx(numeric).epsilon(0.01));
        }
    }
}

TEST_CASE("tire: curvature E of 1 or more, or a missing tire key, fails with its line") {
    const std::string& text = reference_car_text();
    const std::size_t section = text.find("[tire.gravel]");
    REQUIRE(section != std::string::npos);
    const std::size_t at = text.find("curvature_e = 0", section);
    REQUIRE(at != std::string::npos);
    const int line =
        1 +
        static_cast<int>(std::count(text.begin(), text.begin() + static_cast<long>(section), '\n'));
    SUBCASE("E = 1") {
        std::string bad = text;
        bad.replace(at, 15, "curvature_e = 1");
        const rally::sim::CarParseResult r = rally::sim::parse_car(bad, rally::sim::CarKeys::Sim);
        MESSAGE(r.error);
        CHECK_FALSE(r.ok);
        CHECK(r.error.rfind("línea " + std::to_string(line) + ": [tire.gravel]", 0) == 0);
        CHECK(r.error.find("curvature_e") != std::string::npos);
    }
    SUBCASE("E = 0.9 reads") {
        std::string good = text;
        good.replace(at, 15, "curvature_e = 0.9");
        const rally::sim::CarParseResult r = rally::sim::parse_car(good, rally::sim::CarKeys::Sim);
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK(rally::sim::tire_curve(r.car.tire(Surface::Gravel), 1.0f) ==
              doctest::Approx(1.0).epsilon(1e-5));
    }
    SUBCASE("missing key") {
        std::string bad = text;
        bad.replace(at, 15, "");
        const rally::sim::CarParseResult r = rally::sim::parse_car(bad, rally::sim::CarKeys::Sim);
        MESSAGE(r.error);
        CHECK_FALSE(r.ok);
        CHECK(r.error.find("falta la clave 'curvature_e' en [tire.gravel]") != std::string::npos);
    }
}

// Criterio 1.
TEST_CASE("tire: peaks at the table slip within 10 percent and mu Fz within 2 percent") {
    const CarData& car = reference_car();
    const double load = nominal();
    std::string table = "superficie | α pico (°) | κ pico | pico lat / μF_z | lateral a 1,5/2/3/4 "
                        "× α pico | longitudinal a 1,5/2/3/4 × κ pico | asíntota\n";
    for (const Surface surface : kSurfaces) {
        const TireSurface& t = car.tire(surface);
        const double d = t.mu_peak * load;
        // Pico lateral: barrido de α de 0,01° en 0,01°.
        double best_alpha = 0.0;
        double best_fy = 0.0;
        for (int i = 1; i <= 6000; ++i) {
            const double alpha = 0.01 * i * kDeg;
            const double fy = lateral(t, alpha, load);
            if (fy > best_fy) {
                best_fy = fy;
                best_alpha = alpha;
            }
        }
        double best_kappa = 0.0;
        double best_fx = 0.0;
        for (int i = 1; i <= 10000; ++i) {
            const double kappa = 1e-4 * i;
            const double fx = longitudinal(t, kappa, load);
            if (fx > best_fx) {
                best_fx = fx;
                best_kappa = kappa;
            }
        }
        CHECK(std::abs(best_alpha / t.slip_angle_peak - 1.0) <= 0.10);
        CHECK(std::abs(best_fy / d - 1.0) <= 0.02);
        CHECK(std::abs(best_kappa / t.slip_ratio_peak - 1.0) <= 0.10);
        CHECK(std::abs(best_fx / d - 1.0) <= 0.02);
        // A 4 × α pico, entre slide_ratio y slide_ratio + 0,12 veces el pico.
        const double at_four = lateral(t, 4.0 * t.slip_angle_peak, load) / best_fy;
        CHECK(at_four >= t.slide_ratio);
        CHECK(at_four <= t.slide_ratio + 0.12);
        // Más allá nunca baja de slide_ratio: α hasta 89,9° y κ hasta 20.
        double lowest = 1.0;
        for (int i = 0; i <= 1000; ++i) {
            const double alpha = t.slip_angle_peak + (89.9 * kDeg - t.slip_angle_peak) * i / 1000.0;
            lowest = std::min(lowest, lateral(t, alpha, load) / best_fy);
            const double kappa = t.slip_ratio_peak + (20.0 - t.slip_ratio_peak) * i / 1000.0;
            lowest = std::min(lowest, longitudinal(t, kappa, load) / best_fx);
        }
        CHECK(lowest >= t.slide_ratio - 1e-5);
        char row[300];
        std::snprintf(
            row, sizeof(row),
            "%s | %.2f | %.4f | %.4f | %.3f / %.3f / %.3f / %.3f | %.3f / %.3f / %.3f / %.3f | "
            "%.2f\n",
            rally::sim::surface_name(surface), best_alpha / kDeg, best_kappa, best_fy / d,
            lateral(t, 1.5 * t.slip_angle_peak, load) / best_fy,
            lateral(t, 2.0 * t.slip_angle_peak, load) / best_fy,
            lateral(t, 3.0 * t.slip_angle_peak, load) / best_fy, at_four,
            longitudinal(t, 1.5 * t.slip_ratio_peak, load) / best_fx,
            longitudinal(t, 2.0 * t.slip_ratio_peak, load) / best_fx,
            longitudinal(t, 3.0 * t.slip_ratio_peak, load) / best_fx,
            longitudinal(t, 4.0 * t.slip_ratio_peak, load) / best_fx,
            static_cast<double>(t.slide_ratio));
        table += row;
    }
    MESSAGE("forma de la curva a F_z0 = ", load, " N (fuerza / pico):\n", table);
}

// Criterio 2.
TEST_CASE("tire: mu at twice the nominal load is mu0 times 1 minus k within 1 percent") {
    const CarData& car = reference_car();
    for (const Surface surface : kSurfaces) {
        const TireSurface& t = car.tire(surface);
        const double load = 2.0 * nominal();
        double peak = 0.0;
        for (int i = 1; i <= 4000; ++i) {
            peak = std::max(peak, lateral(t, 0.01 * i * kDeg, load));
        }
        const double mu = peak / load;
        const double expected = t.mu_peak * (1.0 - t.load_sensitivity);
        MESSAGE(std::string(rally::sim::surface_name(surface)), ": μ a 2·F_z0 = ", mu,
                " (esperado ", expected, ")");
        CHECK(std::abs(mu / expected - 1.0) <= 0.01);
        // Acotada a 3·F_z0.
        const auto nominal_load = static_cast<float>(nominal());
        CHECK(rally::sim::peak_mu(t, 4.0f * nominal_load, nominal_load) ==
              rally::sim::peak_mu(t, 3.0f * nominal_load, nominal_load));
    }
}

// Criterio 3.
TEST_CASE("tire: combined force never exceeds mu Fz and combined lateral is at most 75 percent") {
    const CarData& car = reference_car();
    for (const Surface surface : kSurfaces) {
        const TireSurface& t = car.tire(surface);
        double worst = 0.0;
        for (const double scale : {0.5, 1.0, 2.0, 3.5}) {
            const double load = scale * nominal();
            const double d =
                rally::sim::peak_mu(t, static_cast<float>(load), static_cast<float>(nominal())) *
                load;
            for (int i = 0; i < 50; ++i) {
                const double kappa = -1.0 + 2.0 * i / 49.0;
                for (int j = 0; j < 50; ++j) {
                    const double alpha = (-60.0 + 120.0 * j / 49.0) * kDeg;
                    const TireForce f = rally::sim::tire_force(
                        t, static_cast<float>(kappa), static_cast<float>(std::tan(alpha)),
                        static_cast<float>(load), static_cast<float>(nominal()));
                    worst = std::max(worst, std::hypot(f.fx, f.fy) / d);
                }
            }
        }
        const double load = nominal();
        const double pure = lateral(t, t.slip_angle_peak, load);
        const TireForce both =
            rally::sim::tire_force(t, t.slip_ratio_peak, t.tan_alpha_peak, static_cast<float>(load),
                                   static_cast<float>(load));
        MESSAGE(std::string(rally::sim::surface_name(surface)),
                ": |F|/(μF_z) máximo en la rejilla ", worst,
                "; lateral con κ pico y α pico / pura = ", both.fy / pure);
        CHECK(worst <= 1.0001);
        CHECK(both.fy / pure <= 0.75);
        CHECK(both.fy_pure == doctest::Approx(pure).epsilon(1e-5));
    }
}

// --- Dirección (2.5-respuesta-plan §2) -------------------------------------------------------

TEST_CASE("tire: a locked wheel sliding at an angle pushes against its sliding velocity") {
    const CarData& car = reference_car();
    std::string table;
    for (const Surface surface : kSurfaces) {
        const TireSurface& t = car.tire(surface);
        for (const double theta : {15.0, 30.0, 45.0, 60.0}) {
            const float speed = 15.0f;
            TireInput in = wheel_input(surface, speed * static_cast<float>(std::cos(theta * kDeg)),
                                       speed * static_cast<float>(std::sin(theta * kDeg)));
            in.stop_torque = 1.0e5f; // bloqueada
            WheelDynamics state;
            const TireForce f = settle(in, state, 2.0);
            REQUIRE(state.omega == 0.0f);
            const double slip_angle =
                angle_deg(state.u / t.relaxation_long, state.w / t.relaxation_lat);
            const double velocity_angle = angle_deg(-in.vx, -in.vy);
            const double force_angle = angle_deg(f.fx, f.fy);
            char row[160];
            std::snprintf(row, sizeof(row),
                          "%s a %.0f°: F a %.2f°, (κ_r, tan α_r) a %.2f°, deslizamiento a %.2f°; "
                          "|F|/D = %.3f\n",
                          rally::sim::surface_name(surface), theta, force_angle, slip_angle,
                          velocity_angle,
                          std::hypot(f.fx, f.fy) /
                              (rally::sim::peak_mu(t, in.load, car.nominal_load) * in.load));
            table += row;
            CHECK(std::abs(angle_diff(force_angle, slip_angle)) <= 0.5);
            CHECK(std::abs(angle_diff(force_angle, velocity_angle)) <= 0.5);
        }
    }
    MESSAGE("rueda bloqueada deslizando a 15 m/s:\n", table);
}

TEST_CASE("tire: force size and direction are continuous in rho in 8 directions") {
    // Por debajo del pico la propia curva sube con pendiente B·C (de 2,9 a 4,4 según la
    // superficie): entre muestras separadas 0,01 en ρ cambia hasta un 4,4 % de D sin ningún
    // salto. Ahí el salto se compara con esa pendiente; desde ρ = 1, donde está la mezcla de la
    // dirección, con el 1 % de D (2.5-respuesta-plan §2).
    const CarData& car = reference_car();
    for (const Surface surface : kSurfaces) {
        const TireSurface& t = car.tire(surface);
        const double load = nominal();
        const double d = t.mu_peak * load;
        const double origin_step = d * t.shape_b * t.shape_c * 0.01;
        double worst_grip = 0.0;  // salto / (D·B·C·0,01) con ρ < 1
        double worst_slide = 0.0; // salto / D con ρ ≥ 1
        double worst_angle = 0.0;
        for (int dir = 0; dir < 8; ++dir) {
            // Dirección en el plano normalizado (ρ_x, ρ_y), fuera de los ejes.
            const double phi = (22.5 + 45.0 * dir) * kDeg;
            TireForce previous;
            double previous_angle = 0.0;
            for (int i = 0; i <= 600; ++i) {
                const double rho = 0.01 * i;
                const double kappa = rho * std::cos(phi) * t.slip_ratio_peak;
                const double tan_alpha = rho * std::sin(phi) * t.tan_alpha_peak;
                const TireForce f = rally::sim::tire_force(
                    t, static_cast<float>(kappa), static_cast<float>(tan_alpha),
                    static_cast<float>(load), static_cast<float>(load));
                const double angle = angle_deg(f.fx, f.fy);
                if (i > 0) {
                    const double jump = std::hypot(f.fx - previous.fx, f.fy - previous.fy);
                    if (rho <= 1.0) {
                        worst_grip = std::max(worst_grip, jump / origin_step);
                    } else {
                        worst_slide = std::max(worst_slide, jump / d);
                    }
                }
                if (i > 1) {
                    worst_angle =
                        std::max(worst_angle, std::abs(angle_diff(angle, previous_angle)));
                }
                previous = f;
                previous_angle = angle;
            }
        }
        MESSAGE(std::string(rally::sim::surface_name(surface)),
                ": salto máximo entre muestras de 0,01 en ρ: ", worst_grip,
                " × D·B·C·0,01 con ρ < 1; ", 100.0 * worst_slide, " % de D con ρ ≥ 1; ",
                worst_angle, "°");
        CHECK(worst_grip <= 1.0 + 1e-4);
        CHECK(worst_slide <= 0.01);
        CHECK(worst_angle <= 1.0);
    }
}

// --- Régimen estacionario de step_tire -------------------------------------------------------

TEST_CASE("tire: steady step_tire gives the curve in grip and past the peak") {
    const CarData& car = reference_car();
    std::string table;
    for (const Surface surface : kSurfaces) {
        const TireSurface& t = car.tire(surface);
        const double d = t.mu_peak * car.nominal_load;
        // Rodando libre a 20 m/s con deriva α: crucero (α pico / 2) y saturación.
        for (const double factor : {0.5, 1.5, 2.0, 4.0}) {
            const double alpha = factor * t.slip_angle_peak;
            const float vx = 20.0f;
            TireInput in = wheel_input(surface, vx, -vx * static_cast<float>(std::tan(alpha)));
            WheelDynamics state;
            state.omega = vx / in.radius;
            const TireForce f = settle(in, state, 2.0);
            const double expected = lateral(t, alpha, car.nominal_load);
            CHECK(f.fy == doctest::Approx(expected).epsilon(0.01));
            char row[120];
            std::snprintf(row, sizeof(row), "%s, %.1f × α pico: F_y %.0f N, curva %.0f N\n",
                          rally::sim::surface_name(surface), factor, static_cast<double>(f.fy),
                          expected);
            table += row;
        }
        // Rueda bloqueada recta a 20 m/s: κ_r = −1, la fuerza de la curva en κ = 1.
        {
            TireInput in = wheel_input(surface, 20.0f, 0.0f);
            in.stop_torque = 1.0e5f;
            WheelDynamics state;
            const TireForce f = settle(in, state, 2.0);
            const double expected = longitudinal(t, -1.0, car.nominal_load);
            CHECK(state.u / t.relaxation_long == doctest::Approx(-1.0).epsilon(1e-3));
            CHECK(f.fx == doctest::Approx(expected).epsilon(0.01));
            char row[120];
            std::snprintf(row, sizeof(row), "%s, bloqueada: F_x %.0f N, curva %.0f N (%.3f D)\n",
                          rally::sim::surface_name(surface), static_cast<double>(f.fx), expected,
                          -expected / d);
            table += row;
        }
        // De lado a 85° (casi parada en su eje): la huella llega al recorte y la fuerza se
        // queda en la de ese ρ, por encima de slide_ratio·D.
        {
            const double alpha = 85.0 * kDeg;
            TireInput in = wheel_input(surface, static_cast<float>(10.0 * std::cos(alpha)),
                                       static_cast<float>(-10.0 * std::sin(alpha)));
            WheelDynamics state;
            state.omega = in.vx / in.radius;
            const TireForce f = settle(in, state, 2.0);
            const double rho = std::hypot(state.u / t.relaxation_long / t.slip_ratio_peak,
                                          state.w / t.relaxation_lat / t.tan_alpha_peak);
            CHECK(rho <= t.slide_cap * (1.0 + 1e-5));
            CHECK(std::hypot(f.fx, f.fy) >= t.slide_ratio * d * (1.0 - 1e-4));
            char row[120];
            std::snprintf(row, sizeof(row),
                          "%s, de lado a 85°: ρ %.2f (recorte %.2f), |F| %.3f D\n",
                          rally::sim::surface_name(surface), rho, static_cast<double>(t.slide_cap),
                          std::hypot(f.fx, f.fy) / d);
            table += row;
        }
    }
    MESSAGE("step_tire estacionario a F_z0:\n", table);
}

// Criterio 4 (relajación de la 2.4, con σ por superficie).
TEST_CASE("tire: after a steer step the relaxed slip reaches 63 percent after rolling sigma") {
    const CarData& car = reference_car();
    for (const Surface surface : kSurfaces) {
        const TireSurface& t = car.tire(surface);
        const double alpha = 0.5 * t.slip_angle_peak;
        for (const float vx : {20.0f, 60.0f}) {
            const TireInput in = wheel_input(surface, vx, vx * static_cast<float>(std::tan(alpha)));
            WheelDynamics state;
            state.omega = vx / in.radius;
            const double final_w = -t.relaxation_lat * std::tan(alpha);
            const double final_fy = lateral(t, -alpha, car.nominal_load);
            double previous = 0.0;
            double crossing = -1.0;
            double peak = 0.0;
            double travelled = 0.0;
            for (int s = 0; s < 4000; ++s) {
                const TireForce f = rally::sim::step_tire(car, in, kSubstep, true, state);
                travelled += vx * kSubstep;
                const double ratio = state.w / final_w;
                peak = std::max(peak, static_cast<double>(f.fy) / final_fy);
                if (crossing < 0.0 && ratio >= 0.632) {
                    crossing = travelled - vx * kSubstep * (ratio - 0.632) / (ratio - previous);
                }
                previous = ratio;
            }
            MESSAGE(std::string(rally::sim::surface_name(surface)), " a ", vx,
                    " m/s: 63 % tras rodar ", crossing, " m (σ_y = ", t.relaxation_lat,
                    " m); fuerza máxima ", peak, " de la final");
            if (vx == 20.0f) {
                CHECK(std::abs(crossing - t.relaxation_lat) / t.relaxation_lat < 0.05);
            }
            CHECK(peak <= 1.0 + 1e-5);
        }
    }
}

// --- Sin cuerda (la 2.4, dividida en dos con la Pacejka: 2.5-plan §7.6) ----------------------

TEST_CASE("tire: at rest, after sliding sideways the footprint stays at the peak, no rope") {
    const CarData& car = reference_car();
    for (const Surface surface : kSurfaces) {
        const TireSurface& t = car.tire(surface);
        // Coche parado con la rueda bloqueada, empujado de lado a 0,5 m/s durante 1 s.
        TireInput in = wheel_input(surface, 0.0f, 0.5f);
        in.stop_torque = 1.0e5f;
        WheelDynamics state;
        settle(in, state, 1.0);
        const double sliding_rho = std::hypot(state.u / t.relaxation_long / t.slip_ratio_peak,
                                              state.w / t.relaxation_lat / t.tan_alpha_peak);
        // Se para: un subpaso con v = 0 y la huella vuelve al pico.
        in.vy = 0.0f;
        const TireForce f = rally::sim::step_tire(car, in, kSubstep, true, state);
        const double rho = std::hypot(state.u / t.relaxation_long / t.slip_ratio_peak,
                                      state.w / t.relaxation_lat / t.tan_alpha_peak);
        const double d = rally::sim::peak_mu(t, in.load, car.nominal_load) * in.load;
        MESSAGE(std::string(rally::sim::surface_name(surface)),
                ": deslizando a 0,5 m/s, ρ = ", sliding_rho, "; parado, ρ = ", rho,
                ", deformación ", std::abs(state.w) * 1000.0,
                " mm (la del pico, σ_y·tan α pico = ", t.relaxation_lat * t.tan_alpha_peak * 1000.0,
                " mm)");
        CHECK(rho <= 1.0 + 1e-5);
        CHECK(std::hypot(f.fx, f.fy) <= d * (1.0 + 1e-5));
    }
}

TEST_CASE("tire: at speed, after 2 s sliding at 20 degrees grip comes back by relaxation") {
    const CarData& car = reference_car();
    const TireSurface& t = car.tire(Surface::Asphalt);
    const float vx = 20.0f;
    const double sliding = 20.0 * kDeg;
    const double gripping = 2.0 * kDeg;
    WheelDynamics state;
    state.omega = vx / car.wheel_radius;
    settle(wheel_input(Surface::Asphalt, vx, vx * static_cast<float>(std::tan(sliding))), state,
           2.0);
    const double stored = state.w;
    const double bound = t.slide_cap * t.relaxation_lat * t.tan_alpha_peak;
    const double d = t.mu_peak * car.nominal_load;
    const double final_fy = lateral(t, -gripping, car.nominal_load);
    const TireInput in =
        wheel_input(Surface::Asphalt, vx, vx * static_cast<float>(std::tan(gripping)));
    double travelled = 0.0;
    double at_six_sigma = 0.0;
    double worst_w = std::abs(stored);
    double worst_f = 0.0;
    for (int s = 0; s < 2000; ++s) {
        const TireForce f = rally::sim::step_tire(car, in, kSubstep, true, state);
        travelled += vx * kSubstep;
        worst_w = std::max(worst_w, static_cast<double>(std::abs(state.w)));
        worst_f = std::max(worst_f, static_cast<double>(std::hypot(f.fx, f.fy)));
        if (at_six_sigma == 0.0 && travelled >= 6.0 * t.relaxation_lat) {
            at_six_sigma = f.fy;
        }
    }
    MESSAGE("tras 2 s a 20°: deformación lateral ", stored * 1000.0,
            " mm (σ_y·tan 20° = ", t.relaxation_lat * std::tan(sliding) * 1000.0, " mm; recorte ",
            bound * 1000.0, " mm); vuelta a 2°: tras rodar 6σ_y, ", at_six_sigma,
            " N frente a la estacionaria ", final_fy, " N (",
            100.0 * (at_six_sigma / final_fy - 1.0), " %)");
    CHECK(stored == doctest::Approx(-t.relaxation_lat * std::tan(sliding)).epsilon(1e-3));
    CHECK(worst_w <= bound * (1.0 + 1e-5));
    CHECK(worst_f <= d * (1.0 + 1e-5));
    CHECK(std::abs(at_six_sigma / final_fy - 1.0) < 0.05);
}

// --- Toma de contacto (2.5-respuesta-3 §1) ---------------------------------------------------

TEST_CASE("tire: touchdown with constant slip starts at damper plus one substep spring") {
    // step_tire solo: huella sin deformar y carga que pasa de 0 a F_z0 en el primer subpaso y se
    // queda; velocidad y deslizamiento fijos (una inercia de rueda enorme mantiene κ). En el
    // primer subpaso la huella se deforma s·φ (φ = (1 − e^(−a·h))/a, a = |v_x|/σ): la fuerza es la
    // del amortiguador de la carcasa, c·g·s·φ/h, más la del muelle de esa deformación (la curva
    // con κ_r = s_x·φ/σ_x y tan α_r = s_y·φ/σ_y; casi k·s·φ, con la rigidez en el origen
    // k = B·C·D/(κ pico·σ_x) y B·C·D/(tan α pico·σ_y)), recortada a μF_z. A 10 m/s, por
    // encima de la rampa (g = 0), solo queda el muelle. Después: |F| ≤ μF_z, sin cambios de signo
    // y, tras rodar 6σ_y, la fuerza de la curva.
    CarData car = reference_car();
    car.wheel_inertia = 1.0e9f;
    std::string table;
    for (const Surface surface : {Surface::Asphalt, Surface::Gravel}) {
        const TireSurface& t = car.tire(surface);
        for (const float speed : {5.0f, 10.0f}) {
            for (const float alpha_factor : {0.5f, 2.0f}) {
                for (const float kappa_factor : {0.0f, 0.5f}) {
                    const double alpha = alpha_factor * t.slip_angle_peak;
                    const double kappa = kappa_factor * t.slip_ratio_peak;
                    TireInput in =
                        wheel_input(surface, speed, -speed * static_cast<float>(std::tan(alpha)));
                    WheelDynamics state;
                    state.omega = speed * (1.0f + static_cast<float>(kappa)) / in.radius;
                    const double d = rally::sim::peak_mu(t, in.load, car.nominal_load) * in.load;
                    // Lo que usa el código, por separado.
                    const double stiffness = t.shape_b * t.shape_c * d;
                    const double corner_mass = in.load / 9.81;
                    const double k_x = stiffness / (t.slip_ratio_peak * t.relaxation_long);
                    const double k_y = stiffness / (t.tan_alpha_peak * t.relaxation_lat);
                    const double c_x =
                        2.0 * car.carcass_damping_long * std::sqrt(k_x * corner_mass);
                    const double c_y = 2.0 * car.carcass_damping_lat * std::sqrt(k_y * corner_mass);
                    const double v_ref = std::max(static_cast<double>(std::hypot(in.vx, in.vy)),
                                                  static_cast<double>(state.omega) * in.radius);
                    const double g = std::clamp(1.0 - v_ref / car.carcass_damping_fade, 0.0, 1.0);
                    const double h = kSubstep;
                    const auto phi = [&](double sigma) {
                        const double a = std::abs(in.vx) / sigma;
                        return (1.0 - std::exp(-a * h)) / a;
                    };
                    const double s_x = kappa * speed;
                    const double s_y = -in.vy;
                    const double phi_x = phi(t.relaxation_long);
                    const double phi_y = phi(t.relaxation_lat);
                    // Muelle: la curva con la deformación del primer subpaso (s·φ).
                    const TireForce spring = rally::sim::tire_force(
                        t, static_cast<float>(s_x * phi_x / t.relaxation_long),
                        static_cast<float>(s_y * phi_y / t.relaxation_lat), in.load,
                        car.nominal_load);
                    const double expected =
                        std::min(std::hypot(c_x * g * phi_x * s_x / h + spring.fx,
                                            c_y * g * phi_y * s_y / h + spring.fy),
                                 d);

                    double first = 0.0;
                    double worst = 0.0;
                    int changes = 0;
                    int sign_x = 0;
                    int sign_y = 0;
                    double travelled = 0.0;
                    double at_six_sigma = 0.0;
                    for (int s = 0; s < 4 * kSubstepsPerSecond; ++s) {
                        const TireForce f = rally::sim::step_tire(car, in, kSubstep, true, state);
                        const double size = std::hypot(f.fx, f.fy);
                        if (s == 0) {
                            first = size;
                        }
                        worst = std::max(worst, size / d);
                        for (const auto& [value, last] :
                             {std::pair<float, int*>{f.fx, &sign_x}, {f.fy, &sign_y}}) {
                            if (value == 0.0f) {
                                continue;
                            }
                            const int sign = value > 0.0f ? 1 : -1;
                            if (*last != 0 && sign != *last) {
                                ++changes;
                            }
                            *last = sign;
                        }
                        travelled += speed * kSubstep;
                        if (at_six_sigma == 0.0 && travelled >= 6.0 * t.relaxation_lat) {
                            const TireForce curve = rally::sim::tire_force(
                                t, static_cast<float>(kappa), static_cast<float>(std::tan(alpha)),
                                in.load, car.nominal_load);
                            at_six_sigma = size / std::hypot(curve.fx, curve.fy) - 1.0;
                        }
                    }
                    char row[220];
                    std::snprintf(row, sizeof(row),
                                  "%s a %.0f m/s, %.1f × α pico, %.1f × κ pico: g %.2f; primer "
                                  "subpaso %.0f N (%.1f %% de μF_z; esperado %.0f; solo el "
                                  "amortiguador %.0f); tras 6σ_y %+.2f %%\n",
                                  rally::sim::surface_name(surface), static_cast<double>(speed),
                                  static_cast<double>(alpha_factor),
                                  static_cast<double>(kappa_factor), g, first, 100.0 * first / d,
                                  expected, std::min(std::hypot(c_x * g * s_x, c_y * g * s_y), d),
                                  100.0 * at_six_sigma);
                    table += row;
                    CHECK(first == doctest::Approx(expected).epsilon(0.01));
                    CHECK(worst <= 1.0 + 1e-5);
                    CHECK(changes == 0);
                    CHECK(std::abs(at_six_sigma) <= 0.02);
                }
            }
        }
    }
    MESSAGE("toma de contacto con F_z0:\n", table);
}
