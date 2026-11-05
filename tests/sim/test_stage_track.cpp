#include "io/recording.h"
#include "new_counter.h"
#include "sim/pcg32.h"
#include "sim/stage_track.h"
#include "stage_driver.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// Seguimiento del coche en el tramo (tarea tramo-seguimiento, lote 4): sim/stage_track.h.
// Trayectorias de puntos que se mueven por el tramo a pasos de 1/240 s: sin coche ni Jolt.
//
// Cada campo de StageRunState se comprueba con un valor esperado en algún test:
//   s, t: criterio 1; on_road: 2 y 6; reset_s: 7; exit_s y off_travel: 4 y 6; last_x, last_z: 12;
//   steps: 7; start_step, finish_step, split_count, split_steps: 8; resets: 7 y 8; shortcuts: 4.

using rally::sim::kRunStartDistance;
using rally::sim::kShortcutMargin;
using rally::sim::kStageEntryLength;
using rally::sim::kStageRunoffLength;
using rally::sim::kStageStripHalfWidth;
using rally::sim::StagePlace;
using rally::sim::StagePose;
using rally::sim::StageProjection;
using rally::sim::StageRun;
using rally::sim::StageRunState;
using rally::sim::StageTrack;

namespace {

constexpr double kDt = 1.0 / 240.0;
constexpr double kMm = 1.0e-3;

// Lazadas: s de las juntas de la primera horquilla (tests/sim/stages/lazadas.stage).
constexpr double kHairpinStart = 730.0;
constexpr double kHairpinEnd = 730.0 + 20.0 + 8.849555921538759;

std::string num(double value) {
    std::array<char, 64> buffer{};
    const std::to_chars_result r =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string(buffer.data(), r.ptr);
}

const std::string& lazadas_text() { return rally::tests::lazadas_stage_text(); }

// Tramo llano con la planta `plan` (sin peralte; semianchos `width`).
std::string stage_text(const std::string& plan, double length, const std::string& width = "3 3") {
    return "format = 1\nname = test\nstart_x = 0\nstart_z = 0\nstart_heading_deg = 0\n"
           "start_height = 0\n[plan]\n" +
           plan + "[profile]\npvi 0 0 0\npvi " + num(length) +
           " 0 0\n[bank]\nbank 0 0\n[width]\nwidth 0 " + width + "\n[surface]\nsurface 0 gravel\n";
}

StageTrack load(const std::string& text) {
    StageTrack track;
    const rally::sim::StageParseResult result = StageTrack::build(text, track);
    INFO(result.error);
    REQUIRE(result.ok);
    return track;
}

const StageTrack& lazadas() {
    static const StageTrack track = load(lazadas_text());
    return track;
}

struct Point {
    double x = 0.0;
    double z = 0.0;
};

// Punto a (s, t): t + a la derecha del avance.
Point at(const StageTrack& track, double s, double t) {
    const StagePose p = track.pose(s);
    return {p.x + t * std::cos(p.heading), p.z + t * std::sin(p.heading)};
}

double dist(Point a, Point b) { return std::hypot(a.x - b.x, a.z - b.z); }

// Un paso de la pasada como lo da SimWorld: begin_step() cuenta el paso y step() decide.
StageRun::Step step_run(StageRun& run, double x, double z) {
    run.begin_step();
    return run.step(x, z);
}

// Lleva el punto por (s, t) de s_from a s_to a `speed` m/s; f(s_real, step) tras cada paso.
// Devuelve el último Step.
template <typename F>
StageRun::Step drive_s(const StageTrack& track, StageRun& run, double s_from, double s_to, double t,
                       double speed, F&& f) {
    const double ds = speed * kDt * (s_to >= s_from ? 1.0 : -1.0);
    double s = s_from;
    StageRun::Step step;
    while (s != s_to) {
        s = (ds > 0.0) ? std::min(s + ds, s_to) : std::max(s + ds, s_to);
        const Point p = at(track, s, t);
        step = step_run(run, p.x, p.z);
        f(s, step);
    }
    return step;
}

template <typename F>
StageRun::Step drive_line(StageRun& run, Point from, Point to, double speed, F&& f) {
    const double length = dist(from, to);
    const int steps = std::max(1, static_cast<int>(std::ceil(length / (speed * kDt))));
    StageRun::Step step;
    for (int i = 1; i <= steps; ++i) {
        const double u = static_cast<double>(i) / steps;
        const Point p{from.x + (to.x - from.x) * u, from.z + (to.z - from.z) * u};
        step = step_run(run, p.x, p.z);
        if (!f(p, step)) {
            break;
        }
    }
    return step;
}

const auto kNothing = [](double, const StageRun::Step&) {};

void place(StageRun& run, const StageTrack& track, StagePlace why, double s) {
    const Point p = at(track, s, 0.0);
    run.placed(why, s, p.x, p.z);
}

bool same_bits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

bool same_state(const StageRunState& a, const StageRunState& b) {
    bool same =
        same_bits(a.s, b.s) && same_bits(a.t, b.t) && same_bits(a.reset_s, b.reset_s) &&
        same_bits(a.exit_s, b.exit_s) && same_bits(a.off_travel, b.off_travel) &&
        same_bits(a.last_x, b.last_x) && same_bits(a.last_z, b.last_z) && a.steps == b.steps &&
        a.start_step == b.start_step && a.finish_step == b.finish_step && a.resets == b.resets &&
        a.shortcuts == b.shortcuts && a.split_count == b.split_count && a.on_road == b.on_road;
    for (int i = 0; i < rally::sim::kMaxSplits; ++i) {
        same = same && a.split_steps[i] == b.split_steps[i];
    }
    return same;
}

// s de un error «s = …: …».
double error_s(const std::string& error) {
    REQUIRE(error.rfind("s = ", 0) == 0);
    double s = 0.0;
    const std::size_t colon = error.find(':');
    REQUIRE(colon != std::string::npos);
    const std::from_chars_result r = std::from_chars(error.data() + 4, error.data() + colon, s);
    REQUIRE(r.ec == std::errc{});
    return s;
}

} // namespace

// --- Geometría y prolongaciones
// --------------------------------------------------------------------

TEST_CASE("stage_track: extensions continue the ends straight with their values") {
    const StageTrack& track = lazadas();
    const double length = track.axis().length();
    CHECK(track.s_begin() == -kStageEntryLength);
    CHECK(track.s_end() == length + kStageRunoffLength);
    const StagePose p0 = track.axis().pose(0.0);
    const StagePose e = track.pose(-20.0);
    CHECK(e.s == -20.0);
    CHECK(std::hypot(e.x - p0.x, e.z - p0.z) == doctest::Approx(20.0).epsilon(1e-12));
    CHECK(e.heading == p0.heading);
    CHECK(e.half_width_left == p0.half_width_left);
    CHECK(e.surface == p0.surface);
    CHECK(e.curvature == 0.0);
    const StagePose pl = track.axis().pose(length);
    CHECK(kStageRunoffLength == 250.0);
    const StagePose r = track.pose(length + kStageRunoffLength);
    CHECK(r.s == length + kStageRunoffLength);
    CHECK(std::hypot(r.x - pl.x, r.z - pl.z) == doctest::Approx(kStageRunoffLength).epsilon(1e-12));
    CHECK(r.y == doctest::Approx(pl.y + kStageRunoffLength * pl.grade).epsilon(1e-12));
    CHECK(r.heading == pl.heading);
    CHECK(r.surface == pl.surface);
    // Acotada fuera.
    CHECK(track.pose(-100.0).s == track.s_begin());
    CHECK(track.pose(1e9).s == track.s_end());
    CHECK(track.pose(std::nan("")).s == track.s_begin());
    CHECK(track.core_half_width(0.0, -1) == 4.0);
    CHECK(track.core_half_width(length, 1) == doctest::Approx(3.6).epsilon(1e-12));
}

// --- Criterio 1: por el eje y paralelo a él
// -------------------------------------------------------

TEST_CASE("stage_track: s follows the axis and parallels at 10 50 and 120 kmh") {
    const StageTrack& track = lazadas();
    for (const double kmh : {10.0, 50.0, 120.0}) {
        for (const double t : {0.0, 2.0, -2.0}) {
            CAPTURE(kmh);
            CAPTURE(t);
            StageRun run(track);
            place(run, track, StagePlace::Start, track.s_begin());
            double max_s = 0.0;
            double max_t = 0.0;
            double max_jump = -1.0; // Δs − cota (≤ 1 mm)
            bool backwards = false;
            bool off = false;
            double prev_s = track.s_begin();
            double prev_real = track.s_begin();
            Point prev_p = at(track, prev_real, t);
            long steps = 0;
            drive_s(track, run, track.s_begin(), track.s_end(), t, kmh / 3.6,
                    [&](double s_real, const StageRun::Step&) {
                        const StageRunState& st = run.state();
                        const Point p = at(track, s_real, t);
                        max_s = std::max(max_s, std::abs(st.s - s_real));
                        max_t = std::max(max_t, std::abs(st.t - t));
                        backwards = backwards || st.s < prev_s;
                        off = off || !st.on_road;
                        // Un punto a t del eje avanza d = Δs·(1 − κ·t): Δs ≤ d / (1 − κ·t).
                        const double k0 = track.pose(prev_real).curvature;
                        const double k1 = track.pose(s_real).curvature;
                        const double factor = std::max(1.0 / (1.0 - k0 * t), 1.0 / (1.0 - k1 * t));
                        max_jump =
                            std::max(max_jump, (st.s - prev_s) - (dist(prev_p, p) * factor + kMm));
                        prev_s = st.s;
                        prev_real = s_real;
                        prev_p = p;
                        ++steps;
                    });
            MESSAGE(kmh, " km/h, t = ", t, ": ", steps, " pasos, |s − s_real| ", max_s,
                    " m, |t − t_real| ", max_t, " m");
            CHECK(max_s <= kMm);
            CHECK(max_t <= kMm);
            CHECK(max_jump <= 0.0);
            CHECK_FALSE(backwards);
            CHECK_FALSE(off);
            CHECK(run.state().s == track.s_end());
            CHECK(run.state().shortcuts == 0);
        }
    }
}

// --- Criterio 2: prolongaciones y extremos -------------------------------------------------------

TEST_CASE("stage_track: beyond both ends s stays at the end and the point is off the road") {
    const StageTrack& track = lazadas();
    const StagePose b = track.pose(track.s_begin());
    const StagePose e = track.pose(track.s_end());
    // 10 m detrás de la salida de la prolongación y 10 m más allá de la frenada, en el eje.
    const Point behind{b.x - 10.0 * std::sin(b.heading), b.z + 10.0 * std::cos(b.heading)};
    const Point beyond{e.x + 10.0 * std::sin(e.heading), e.z - 10.0 * std::cos(e.heading)};
    const StageProjection pb = track.project(behind.x, behind.z, track.s_begin(), 0.0);
    CHECK(pb.s == track.s_begin());
    CHECK_FALSE(pb.on_road);
    CHECK(pb.distance == doctest::Approx(10.0).epsilon(1e-9));
    const StageProjection pe = track.acquire(beyond.x, beyond.z);
    CHECK(pe.s == track.s_end());
    CHECK_FALSE(pe.on_road);

    // Una pasada que sigue por la frenada y se sale por el final.
    StageRun run(track);
    place(run, track, StagePlace::Start, track.s_end() - 5.0);
    const Point from = at(track, track.s_end() - 5.0, 0.0);
    bool left_road = false;
    drive_line(run, from, beyond, 20.0, [&](Point p, const StageRun::Step&) {
        const StageRunState& st = run.state();
        if (dist(p, beyond) < 9.0) {
            CHECK(st.s == track.s_end());
            left_road = left_road || !st.on_road;
        }
        return true;
    });
    CHECK(left_road);
    CHECK_FALSE(run.state().on_road);
    // Y hacia atrás por detrás de la salida.
    StageRun back(track);
    place(back, track, StagePlace::Start, track.s_begin() + 5.0);
    drive_line(back, at(track, track.s_begin() + 5.0, 0.0), behind, 20.0,
               [](Point, const StageRun::Step&) { return true; });
    CHECK(back.state().s == track.s_begin());
    CHECK_FALSE(back.state().on_road);
}

// --- Criterio 3: adquisición frente a fuerza bruta -----------------------------------------------

namespace {

// La s del punto del eje más cercano por fuerza bruta: muestreo a 0,05 m de todo el tramo y
// refino ternario de cada mínimo local que puede ganar.
struct Brute {
    std::vector<double> s, x, z;
    explicit Brute(const StageTrack& track) {
        const double step = 0.05;
        const auto count = static_cast<std::size_t>((track.s_end() - track.s_begin()) / step);
        for (std::size_t i = 0; i <= count; ++i) {
            const double si =
                std::min(track.s_begin() + step * static_cast<double>(i), track.s_end());
            const StagePose p = track.pose(si);
            s.push_back(si);
            x.push_back(p.x);
            z.push_back(p.z);
        }
    }
    struct Result {
        double s;
        double distance;
    };
    Result nearest(const StageTrack& track, double px, double pz) const {
        std::vector<double> d2(s.size());
        double best = 1e300;
        for (std::size_t i = 0; i < s.size(); ++i) {
            d2[i] = (px - x[i]) * (px - x[i]) + (pz - z[i]) * (pz - z[i]);
            best = std::min(best, d2[i]);
        }
        const double limit = (std::sqrt(best) + 0.05) * (std::sqrt(best) + 0.05);
        Result r{0.0, 1e300};
        const auto distance = [&](double si) {
            const StagePose p = track.pose(si);
            return std::hypot(px - p.x, pz - p.z);
        };
        for (std::size_t i = 0; i < s.size(); ++i) {
            const bool local =
                (i == 0 || d2[i] <= d2[i - 1]) && (i + 1 == s.size() || d2[i] <= d2[i + 1]);
            if (!local || d2[i] > limit) {
                continue;
            }
            double lo = s[i == 0 ? 0 : i - 1];
            double hi = s[i + 1 == s.size() ? i : i + 1];
            for (int it = 0; it < 100; ++it) {
                const double m1 = lo + (hi - lo) / 3.0;
                const double m2 = hi - (hi - lo) / 3.0;
                if (distance(m1) < distance(m2)) {
                    hi = m2;
                } else {
                    lo = m1;
                }
            }
            const double sm = 0.5 * (lo + hi);
            const double dm = distance(sm);
            if (dm < r.distance) {
                r = {sm, dm};
            }
        }
        return r;
    }
};

} // namespace

TEST_CASE("stage_track: acquire finds the nearest axis point of 2000 points of the strip") {
    const StageTrack& track = lazadas();
    const Brute brute(track);
    rally::sim::Pcg32 rng(0x5eed'2026'1025ull, 7);
    const auto uniform = [&rng] { return static_cast<double>(rng.next_u32()) / 4294967296.0; };
    double max_s = 0.0;
    double max_d = 0.0;
    int ties = 0;
    for (int i = 0; i < 2000; ++i) {
        const double s = track.s_begin() + (track.s_end() - track.s_begin()) * uniform();
        const double t = kStageStripHalfWidth * (2.0 * uniform() - 1.0);
        const Point p = at(track, s, t);
        const StageProjection a = track.acquire(p.x, p.z);
        const Brute::Result b = brute.nearest(track, p.x, p.z);
        CAPTURE(i);
        CAPTURE(s);
        CAPTURE(t);
        // acquire nunca da algo más lejano que la fuerza bruta.
        CHECK(a.distance <= b.distance + 1e-6);
        max_d = std::max(max_d, std::abs(a.distance - b.distance));
        if (std::abs(a.s - b.s) > kMm) {
            // Empate: dos puntos del eje a la misma distancia (s mal definida).
            CHECK(std::abs(a.distance - b.distance) <= 1e-6);
            ++ties;
        } else {
            max_s = std::max(max_s, std::abs(a.s - b.s));
        }
    }
    MESSAGE("adquisición: |s − s_bruta| ", max_s, " m, |d − d_bruta| ", max_d, " m, empates ",
            ties);
    CHECK(max_s <= kMm);
    CHECK(ties == 0);
}

TEST_CASE("stage_track: an arc turning more than 180 degrees is projected exactly") {
    // Arco de 200° a R 20 (se parte en dos piezas al cargar), contracurva de 20° y recta.
    const double pi = 3.14159265358979323846;
    const double big = 20.0 * pi * 200.0 / 180.0;
    const double small = 20.0 * pi * 20.0 / 180.0;
    const StageTrack track = load(stage_text("straight 30\narc " + num(big) + " 20\narc " +
                                                 num(small) + " -20\nstraight 30\n",
                                             60.0 + big + small));
    const Brute brute(track);
    rally::sim::Pcg32 rng(2026, 3);
    const auto uniform = [&rng] { return static_cast<double>(rng.next_u32()) / 4294967296.0; };
    double max_s = 0.0;
    for (int i = 0; i < 500; ++i) {
        const double s = 30.0 + big * uniform();
        const double t = 30.0 * (2.0 * uniform() - 1.0);
        const Point p = at(track, s, t);
        const StageProjection a = track.acquire(p.x, p.z);
        const Brute::Result b = brute.nearest(track, p.x, p.z);
        CAPTURE(s);
        CAPTURE(t);
        CHECK(a.distance <= b.distance + 1e-6);
        max_s = std::max(max_s, std::abs(a.s - b.s));
        // En la ventana de la pasada, igual.
        const StageProjection w = track.project(p.x, p.z, s - 20.0, s + 20.0);
        if (std::abs(t) < 15.0) {
            CHECK(std::abs(w.s - s) <= kMm);
        }
    }
    MESSAGE("arco de 200°: |s − s_bruta| ", max_s, " m");
    CHECK(max_s <= kMm);
}

TEST_CASE("stage_track: a point outside the strip is acquired against the whole stage") {
    const StageTrack& track = lazadas();
    const Brute brute(track);
    int outside = 0;
    for (const double t : {100.0, -100.0, 300.0, -300.0}) {
        for (const double s : {-20.0, 50.0, 500.0, 800.0, 1200.0, track.s_end()}) {
            CAPTURE(s);
            CAPTURE(t);
            const Point p = at(track, s, t);
            const Brute::Result b = brute.nearest(track, p.x, p.z);
            if (b.distance <= kStageStripHalfWidth + 1.0) {
                continue; // el tramo se curva: otra parte del eje queda cerca
            }
            ++outside;
            const StageProjection a = track.acquire(p.x, p.z);
            CHECK(a.distance > kStageStripHalfWidth);
            CHECK(std::abs(a.distance - b.distance) <= 1e-6);
            CHECK(std::abs(a.s - b.s) <= kMm);
            CHECK_FALSE(a.on_road);
        }
    }
    // Y uno muy lejos, fuera de toda celda.
    const StageProjection far = track.acquire(5000.0, -7000.0);
    const Brute::Result b = brute.nearest(track, 5000.0, -7000.0);
    CHECK(std::abs(far.s - b.s) <= kMm);
    CHECK(std::abs(far.distance - b.distance) <= 1e-6);
    MESSAGE("puntos fuera de la franja: ", outside + 1);
    CHECK(outside >= 10);
}

// --- Criterio 4: atajo entre los brazos de las lazadas
// ---------------------------------------------

TEST_CASE("stage_track: a straight line from one arm of the hairpins to the next is a shortcut") {
    const StageTrack& track = lazadas();
    // Los brazos de la horquilla 1 son antiparalelos: 730 − x queda enfrente de 758,85 + x.
    const double from_s = kHairpinStart - 30.0;
    const double to_s = kHairpinEnd + 30.0;
    StageRun run(track);
    place(run, track, StagePlace::Start, from_s - 20.0);
    drive_s(track, run, from_s - 20.0, from_s, 0.0, 50.0 / 3.6, kNothing);
    const double reset_before = run.state().reset_s;
    CHECK(reset_before == doctest::Approx(from_s).epsilon(1e-9));
    bool shortcut = false;
    double last_on_road_s = run.state().s;
    StageRun::Step result;
    Point where;
    drive_line(run, at(track, from_s, 0.0), at(track, to_s, 0.0), 50.0 / 3.6,
               [&](Point p, const StageRun::Step& step) {
                   if (step.shortcut) {
                       shortcut = true;
                       result = step;
                       where = p;
                       return false;
                   }
                   // Ni reset_s ni nada de la pasada avanzan fuera.
                   CHECK(run.state().reset_s == reset_before);
                   if (run.state().on_road) {
                       last_on_road_s = run.state().s;
                   }
                   return true;
               });
    REQUIRE(shortcut);
    const StageRunState& st = run.state();
    StageProjection back;
    REQUIRE(track.on_any_road(where.x, where.z, back));
    const double gain = back.s - st.exit_s;
    MESSAGE("atajo: ganancia de s ", gain, " m, recorrido fuera ", st.off_travel, " m, margen ",
            gain - st.off_travel - kShortcutMargin, " m (salida en s = ", st.exit_s,
            ", vuelta en s = ", back.s, ")");
    CHECK(result.reset_s == st.exit_s);
    CHECK(st.exit_s == last_on_road_s);
    CHECK(st.shortcuts == 1);
    CHECK(st.reset_s == reset_before);
    CHECK(st.split_count == 0);
    CHECK(st.finish_step == -1);
    CHECK_FALSE(st.on_road);
    CHECK(st.off_travel == doctest::Approx(13.35 - 3.6 - 3.6).epsilon(0.05));
    CHECK(gain - st.off_travel > kShortcutMargin);
    // Quien llama recoloca en exit_s: sigue desde ahí, en carretera, sin otro atajo.
    place(run, track, StagePlace::Shortcut, st.exit_s);
    CHECK(run.state().s == st.exit_s);
    CHECK(run.state().t == 0.0);
    CHECK(run.state().on_road);
    drive_s(track, run, st.exit_s, st.exit_s + 5.0, 0.0, 50.0 / 3.6, kNothing);
    CHECK(run.state().shortcuts == 1);
    CHECK(run.state().on_road);
    CHECK(run.state().resets == 0);
}

TEST_CASE("stage_track: falling back to an earlier pass is not a shortcut") {
    const StageTrack& track = lazadas();
    const double from_s = kHairpinEnd + 30.0; // segundo brazo
    const double to_s = kHairpinStart - 30.0; // enfrente, en el primero
    StageRun run(track);
    place(run, track, StagePlace::Start, from_s - 10.0);
    drive_s(track, run, from_s - 10.0, from_s, 0.0, 50.0 / 3.6, kNothing);
    const double reset_before = run.state().reset_s;
    bool shortcut = false;
    drive_line(run, at(track, from_s, 0.0), at(track, to_s, 0.0), 50.0 / 3.6,
               [&](Point, const StageRun::Step& step) {
                   shortcut = shortcut || step.shortcut;
                   return true;
               });
    const StageRunState& st = run.state();
    CHECK_FALSE(shortcut);
    CHECK(st.shortcuts == 0);
    CHECK(st.on_road);
    CHECK(st.s == doctest::Approx(to_s).epsilon(1e-9));
    CHECK(st.reset_s == reset_before);
    // Y sigue desde esa pasada.
    drive_s(track, run, to_s, to_s + 5.0, 0.0, 50.0 / 3.6, kNothing);
    CHECK(std::abs(run.state().s - (to_s + 5.0)) <= kMm);
    CHECK(run.state().reset_s == reset_before);
}

TEST_CASE("stage_track: a shortcut over a split does not count the split") {
    // Horquilla en s = 980; el parcial de 1000 cae dentro.
    const double hairpin = 20.0 + 8.849555921538759;
    const double length = 980.0 + hairpin + 1200.0;
    const StageTrack track =
        load(stage_text("straight 980\nspiral 10 inf 6\narc 8.849555921538759 6\n"
                        "spiral 10 6 inf\nstraight 1200\n",
                        length));
    StageRun run(track);
    place(run, track, StagePlace::Start, 940.0);
    drive_s(track, run, 940.0, 960.0, 0.0, 50.0 / 3.6, kNothing);
    bool shortcut = false;
    drive_line(run, at(track, 960.0, 0.0), at(track, 980.0 + hairpin + 20.0, 0.0), 50.0 / 3.6,
               [&](Point, const StageRun::Step& step) {
                   shortcut = step.shortcut;
                   return !step.shortcut;
               });
    REQUIRE(shortcut);
    CHECK(run.state().split_count == 0);
    CHECK(run.state().shortcuts == 1);
    const double exit_s = run.state().exit_s;
    place(run, track, StagePlace::Shortcut, exit_s);
    drive_s(track, run, exit_s, 1010.0, 0.0, 50.0 / 3.6, kNothing);
    CHECK(run.state().split_count == 1);
}

// --- Criterio 5: corte de curva justo por debajo y por encima del margen
// ---------------------------

namespace {

struct Cut {
    bool shortcut = false;
    bool returned = false;
    double exit_s = 0.0;
    double off_travel = 0.0;
    double back_s = 0.0;
};

// Cruza en línea recta de brazo a brazo de la horquilla 1, x metros antes de su inicio, a 10 km/h.
Cut cut_hairpin(const StageTrack& track, double x) {
    StageRun run(track);
    place(run, track, StagePlace::Start, 690.0);
    drive_s(track, run, 690.0, kHairpinStart - x, 0.0, 10.0 / 3.6, kNothing);
    Cut cut;
    Point where;
    drive_line(run, at(track, kHairpinStart - x, 0.0), at(track, kHairpinEnd + x, 0.0), 10.0 / 3.6,
               [&](Point p, const StageRun::Step& step) {
                   where = p;
                   cut.shortcut = step.shortcut;
                   cut.returned = step.shortcut || run.state().on_road;
                   // Hasta que vuelve a la carretera (los primeros pasos aún están en ella).
                   return !(cut.returned && run.state().exit_s != 0.0);
               });
    const StageRunState& st = run.state();
    cut.exit_s = st.exit_s;
    cut.off_travel = st.off_travel;
    StageProjection back;
    REQUIRE(track.on_any_road(where.x, where.z, back));
    cut.back_s = back.s;
    return cut;
}

} // namespace

TEST_CASE("stage_track: cutting a curve 0.5 m under the margin is allowed and 0.5 m over is not") {
    const StageTrack& track = lazadas();
    // Geometría: los brazos rectos de la horquilla 1 son antiparalelos y 730 − x queda enfrente de
    // 758,85 + x. Cortando en perpendicular, ganancia = (758,85 − 730) + 2·x y recorrido fuera =
    // ancho entre ejes − 2 núcleos (semiancho 2,6 + arcén 1).
    const double width = dist(at(track, kHairpinStart, 0.0), at(track, kHairpinEnd, 0.0));
    const double core = track.core_half_width(kHairpinStart, 1);
    const double travel = width - 2.0 * core;
    for (const double target : {-0.5, 0.5}) {
        CAPTURE(target);
        const double x = 0.5 * (kShortcutMargin + travel + target - (kHairpinEnd - kHairpinStart));
        const double gain = kHairpinEnd - kHairpinStart + 2.0 * x;
        const Cut cut = cut_hairpin(track, x);
        const double d = 10.0 / 3.6 * kDt;
        MESSAGE("corte por dentro de la horquilla a ", x, " m de su inicio: ganancia ", gain,
                " m, recorrido fuera ", travel, " m (", cut.off_travel, " medido), margen ",
                gain - travel - kShortcutMargin, " m, atajo ", cut.shortcut);
        CHECK(cut.returned);
        CHECK(cut.shortcut == (target > 0.0));
        CHECK(std::abs(cut.exit_s - (kHairpinStart - x)) <= kMm);
        CHECK(std::abs(cut.back_s - (kHairpinEnd + x)) <= kMm);
        // El paso de salida y el de vuelta caen hasta un paso más allá de cada borde.
        CHECK(cut.off_travel >= travel - kMm);
        CHECK(cut.off_travel <= travel + 2.0 * d + kMm);
    }
}

// --- on_any_road con pasadas de anchos distintos
// ----------------------------------------------------

TEST_CASE("stage_track: on_any_road finds the wide road even when a narrow axis is nearer") {
    // Primer brazo al norte con semiancho 3 (núcleo 4); horquilla de R 10; el segundo brazo,
    // con semiancho 0,5 (núcleo 1,5), se acerca hasta x ≈ 7,2 (1,7 m entre núcleos) y se va al
    // este. Un punto a 3,8 m del eje ancho está en su arcén y a ≈ 3,4 m del eje estrecho.
    const double pi = 3.14159265358979323846;
    const std::string plan = "straight 100\narc " + num(10.0 * pi) +
                             " 10\narc 35.97 100\narc 35.97 -100\nstraight 30\narc " +
                             num(15.0 * pi) + " -30\n";
    const double length = 100.0 + 10.0 * pi + 35.97 + 35.97 + 30.0 + 15.0 * pi;
    std::string text = stage_text(plan, length);
    const std::string from = "width 0 3 3\n";
    text.replace(text.find(from), from.size(),
                 "width 0 3 3\nwidth " + num(100.0 + 10.0 * pi) + " 3 3\nwidth " +
                     num(100.0 + 10.0 * pi + 10.0) + " 0.5 0.5\n");
    const StageTrack track = load(text);
    const Point p = at(track, 20.0, 3.8);
    const StageProjection nearest = track.acquire(p.x, p.z);
    CHECK(nearest.s > 150.0);
    CHECK(nearest.distance < 3.8);
    CHECK_FALSE(nearest.on_road);
    StageProjection road;
    REQUIRE(track.on_any_road(p.x, p.z, road));
    CHECK(std::abs(road.s - 20.0) <= kMm);
    CHECK(road.t == doctest::Approx(3.8).epsilon(1e-9));
    CHECK(road.on_road);
    MESSAGE("eje más cercano a ", nearest.distance, " m (s = ", nearest.s,
            "); calzada pisada en s = ", road.s);
    // Y una pasada que viene de fuera vuelve a la ancha, no a la estrecha.
    StageRun run(track);
    place(run, track, StagePlace::Start, 10.0);
    drive_s(track, run, 10.0, 15.0, 0.0, 50.0 / 3.6, kNothing);
    drive_line(run, at(track, 15.0, 0.0), at(track, 17.0, 5.0), 10.0 / 3.6,
               [](Point, const StageRun::Step&) { return true; });
    CHECK_FALSE(run.state().on_road);
    drive_line(run, at(track, 17.0, 5.0), p, 10.0 / 3.6,
               [](Point, const StageRun::Step&) { return true; });
    CHECK(run.state().on_road);
    CHECK(std::abs(run.state().s - 20.0) <= kMm);
    CHECK(run.state().shortcuts == 0);
}

// --- Criterio 6: salida y vuelta normal ---------------------------------------------------------

TEST_CASE("stage_track: leaving 10 m to the side and coming back is not a shortcut") {
    const StageTrack& track = lazadas();
    StageRun run(track);
    place(run, track, StagePlace::Start, 10.0);
    drive_s(track, run, 10.0, 30.0, 0.0, 50.0 / 3.6, kNothing);
    // Los primeros pasos de la salida aún están en el arcén (|t| ≤ 4 m): reset_s avanza hasta el
    // último de ellos y desde ahí no se mueve.
    double reset_at_exit = run.state().reset_s;
    double travel = 0.0;
    double exit_s = run.state().s;
    bool was_off = false;
    bool shortcut = false;
    int off_steps = 0;
    Point prev = at(track, 30.0, 0.0);
    const auto watch = [&](Point p, const StageRun::Step& step) {
        const StageRunState& st = run.state();
        shortcut = shortcut || step.shortcut;
        if (!st.on_road) {
            if (!was_off) {
                CHECK(st.exit_s == exit_s);
            }
            was_off = true;
            ++off_steps;
            travel += dist(prev, p);
            CHECK(st.reset_s == reset_at_exit);
            CHECK(st.off_travel == doctest::Approx(travel).epsilon(1e-9));
        } else if (!was_off) {
            exit_s = st.s;
            reset_at_exit = st.reset_s;
        }
        prev = p;
        return true;
    };
    drive_line(run, at(track, 30.0, 0.0), at(track, 40.0, -10.0), 50.0 / 3.6, watch);
    CHECK_FALSE(run.state().on_road);
    CHECK(run.state().t == doctest::Approx(-10.0).epsilon(1e-9));
    drive_line(run, at(track, 40.0, -10.0), at(track, 50.0, 0.0), 50.0 / 3.6, watch);
    CHECK(was_off);
    CHECK(off_steps > 50);
    CHECK(exit_s > 30.0);
    CHECK(exit_s < 35.0);
    CHECK_FALSE(shortcut);
    CHECK(run.state().shortcuts == 0);
    CHECK(run.state().on_road);
    CHECK(std::abs(run.state().s - 50.0) <= kMm);
    // De vuelta en el arcén (|t| ≤ 4 m, s ≈ 46) reset_s vuelve a avanzar, hasta 50.
    CHECK(std::abs(run.state().reset_s - 50.0) <= kMm);
}

// --- Criterio 7: recolocación
// ---------------------------------------------------------------------

TEST_CASE("stage_track: reset_s is the furthest s on the road and placed resets s and t") {
    const StageTrack& track = lazadas();
    StageRun run(track);
    place(run, track, StagePlace::Start, 10.0);
    CHECK(run.state().reset_s == 10.0);
    drive_s(track, run, 10.0, 50.0, 0.0, 50.0 / 3.6, kNothing);
    const double furthest = run.state().reset_s;
    CHECK(std::abs(furthest - 50.0) <= kMm);
    // Marcha atrás: s baja, reset_s no.
    double prev_s = run.state().s;
    bool went_back = true;
    drive_s(track, run, 50.0, 40.0, 1.0, 10.0 / 3.6, [&](double, const StageRun::Step&) {
        went_back = went_back && run.state().s < prev_s;
        prev_s = run.state().s;
        CHECK(run.state().reset_s == furthest);
    });
    CHECK(went_back);
    CHECK(std::abs(run.state().s - 40.0) <= kMm);
    // Parado: s igual, los pasos cuentan.
    const std::int64_t steps = run.state().steps;
    const double still = run.state().s;
    const Point p = at(track, 40.0, 1.0);
    for (int i = 0; i < 100; ++i) {
        step_run(run, p.x, p.z);
    }
    CHECK(run.state().s == still);
    CHECK(run.state().steps == steps + 100);
    CHECK(run.state().reset_s == furthest);
    // Adelante otra vez hasta 55 y fuera de la carretera hasta 70.
    drive_s(track, run, 40.0, 55.0, 0.0, 50.0 / 3.6, kNothing);
    CHECK(std::abs(run.state().reset_s - 55.0) <= kMm);
    // Los primeros pasos de la línea aún pisan el arcén (|t| ≤ 4 m): ahí reset_s sigue al máximo.
    double at_exit = run.state().reset_s;
    bool off_seen = false;
    drive_line(run, at(track, 55.0, 0.0), at(track, 70.0, -12.0), 50.0 / 3.6,
               [&](Point, const StageRun::Step&) {
                   if (!run.state().on_road) {
                       off_seen = true;
                       CHECK(run.state().reset_s == at_exit);
                   } else {
                       CHECK_FALSE(off_seen);
                       CHECK(run.state().reset_s == run.state().s);
                       at_exit = run.state().reset_s;
                   }
                   return true;
               });
    CHECK(off_seen);
    CHECK(at_exit > 55.0);
    CHECK(at_exit < 61.0);
    CHECK_FALSE(run.state().on_road);
    CHECK(run.state().s > at_exit + 5.0); // s sigue avanzando fuera; reset_s no
    CHECK(run.state().reset_s == at_exit);
    // Botón: en reset_s, t = 0, en carretera, una recolocación más.
    place(run, track, StagePlace::Button, run.state().reset_s);
    CHECK(run.state().s == at_exit);
    CHECK(run.state().t == 0.0);
    CHECK(run.state().on_road);
    CHECK(run.state().resets == 1);
    CHECK(run.state().off_travel == 0.0);
    CHECK(run.state().reset_s == at_exit);
}

TEST_CASE("stage_track: a long jump without placed is acquired again") {
    const StageTrack& track = lazadas();
    StageRun run(track);
    place(run, track, StagePlace::Start, 90.0);
    drive_s(track, run, 90.0, 100.0, 0.0, 50.0 / 3.6, kNothing);
    const Point far = at(track, 400.0, 1.0);
    step_run(run, far.x, far.z);
    CHECK(std::abs(run.state().s - 400.0) <= kMm);
    CHECK(run.state().t == doctest::Approx(1.0).epsilon(1e-6));
    CHECK(run.state().on_road);
}

// --- Criterio 8: pasada, parciales y meta
// ----------------------------------------------------------

namespace {

struct Expected {
    std::int64_t start = -1;
    std::vector<std::int64_t> splits;
    std::int64_t finish = -1;
};

} // namespace

TEST_CASE("stage_track: the run starts, counts each split once and finishes at L") {
    struct Case {
        double length;
        int splits;
    };
    for (const Case c : {Case{2500.0, 2}, Case{2150.0, 1}}) {
        CAPTURE(c.length);
        const double straight = c.length - 400.0;
        const StageTrack track = load(stage_text(
            "straight 200\narc 100 300\narc 100 -300\nstraight " + num(straight) + "\n", c.length));
        StageRun run(track);
        // Antes de empezar: recolocar en la prolongación de entrada no empieza la pasada.
        const double s0 = -10.0 + 0.000123;
        place(run, track, StagePlace::Start, s0);
        drive_s(track, run, s0, -5.0, 0.0, 10.0 / 3.6, kNothing);
        place(run, track, StagePlace::Button, run.state().reset_s);
        CHECK(run.state().resets == 1);
        CHECK(run.state().start_step == -1);
        CHECK(run.state().reset_s == doctest::Approx(-5.0).epsilon(1e-9));

        // Hasta la meta y 30 m más, a 120 km/h. Lo esperado sale de s_real (el test 1 da
        // |s − s_real| ≤ 1 mm; ningún paso cae a menos de 1 mm de un umbral).
        const std::vector<double> thresholds = [&] {
            std::vector<double> v{kRunStartDistance};
            for (int k = 1; k <= c.splits; ++k) {
                v.push_back(1000.0 * k);
            }
            v.push_back(track.axis().length());
            return v;
        }();
        Expected expected;
        double prev_real = run.state().s;
        // 119,3 km/h: a 120 km/h el paso es 1/7,2 m y cae justo en los umbrales enteros.
        drive_s(track, run, run.state().s, track.axis().length() + 30.0, 0.0, 119.3 / 3.6,
                [&](double s_real, const StageRun::Step&) {
                    const std::int64_t step = run.state().steps;
                    for (const double th : thresholds) {
                        CHECK(std::abs(s_real - th) > kMm);
                    }
                    if (expected.start < 0 && s_real > kRunStartDistance) {
                        expected.start = step;
                    }
                    for (int k = 1; k <= c.splits; ++k) {
                        if (prev_real < 1000.0 * k && s_real >= 1000.0 * k) {
                            expected.splits.push_back(step);
                        }
                    }
                    if (expected.finish < 0 && s_real >= track.axis().length()) {
                        expected.finish = step;
                    }
                    prev_real = s_real;
                });
        const StageRunState& st = run.state();
        CHECK(st.start_step == expected.start);
        REQUIRE(st.split_count == c.splits);
        REQUIRE(expected.splits.size() == static_cast<std::size_t>(c.splits));
        for (int k = 0; k < c.splits; ++k) {
            CHECK(st.split_steps[k] == expected.splits[static_cast<std::size_t>(k)]);
        }
        CHECK(st.split_steps[c.splits] == 0);
        CHECK(st.finish_step == expected.finish);
        MESSAGE("L = ", c.length, ": salida en el paso ", st.start_step, ", parciales ",
                st.split_count, ", meta en el paso ", st.finish_step);

        // Tras la meta: recolocar no la cuenta otra vez.
        const StageRunState before = st;
        place(run, track, StagePlace::Button, st.reset_s);
        CHECK(run.state().resets == 2);
        drive_s(track, run, run.state().s, run.state().s + 10.0, 0.0, 50.0 / 3.6, kNothing);
        CHECK(run.state().finish_step == before.finish_step);
        CHECK(run.state().split_count == before.split_count);
        CHECK(run.state().start_step == before.start_step);
    }
}

TEST_CASE("stage_track: driving back and forth over a split counts it once") {
    const StageTrack track = load(stage_text("straight 2500\n", 2500.0));
    StageRun run(track);
    place(run, track, StagePlace::Start, 990.0);
    drive_s(track, run, 990.0, 1005.0, 0.0, 50.0 / 3.6, kNothing);
    REQUIRE(run.state().split_count == 1);
    const std::int64_t first = run.state().split_steps[0];
    drive_s(track, run, 1005.0, 995.0, 0.0, 10.0 / 3.6, kNothing);
    drive_s(track, run, 995.0, 1010.0, 0.0, 50.0 / 3.6, kNothing);
    CHECK(run.state().split_count == 1);
    CHECK(run.state().split_steps[0] == first);
    CHECK(run.state().split_steps[1] == 0);
}

// --- Criterio 9: validación al cargar
// -------------------------------------------------------------

TEST_CASE("stage_track: the test stage passes the load validation") {
    StageTrack track;
    const rally::sim::StageParseResult result = StageTrack::build(lazadas_text(), track);
    INFO(result.error);
    CHECK(result.ok);
}

TEST_CASE("stage_track: a curve tighter than its core fails with its s") {
    StageTrack track = lazadas();
    const std::uint64_t before = track.axis().hash();
    // R 6 con semiancho 5: núcleo interior 6 + 0,5 > 6 (arco de 86°).
    const rally::sim::StageParseResult result =
        StageTrack::build(stage_text("straight 50\narc 9 6\nstraight 50\n", 109.0, "5 5"), track);
    CAPTURE(result.error);
    CHECK_FALSE(result.ok);
    CHECK(result.error.rfind("s = 50.00: ", 0) == 0);
    CHECK(result.error.find("más cerrada que su núcleo") != std::string::npos);
    // Con error, `out` como estaba.
    CHECK(track.axis().hash() == before);
    // R 6 con semiancho 3 (núcleo 4) sí vale.
    const rally::sim::StageParseResult good =
        StageTrack::build(stage_text("straight 50\narc 9 6\nstraight 50\n", 109.0), track);
    INFO(good.error);
    CHECK(good.ok);
    // Un error del formato sigue siendo «línea N: …».
    const rally::sim::StageParseResult format =
        StageTrack::build(stage_text("straight 0\n", 0.0), track);
    CHECK(format.error.rfind("línea ", 0) == 0);
}

TEST_CASE("stage_track: two arms closer than allowed fail with their s") {
    // Recta al norte, horquilla de R 10 (brazos a 20 m), el segundo brazo se acerca al primero
    // hasta x ≈ 9 (núcleos de 4 m: 1 m entre ellos), sigue paralelo y se va al este.
    const std::string plan = "straight 100\narc " + num(10.0 * 3.14159265358979323846) +
                             " 10\narc 33.3 100\narc 33.3 -100\nstraight 20\narc " +
                             num(15.0 * 3.14159265358979323846) + " -30\n";
    const double length =
        100.0 + 10.0 * 3.14159265358979323846 + 33.3 + 33.3 + 20.0 + 15.0 * 3.14159265358979323846;
    StageTrack track;
    const rally::sim::StageParseResult result = StageTrack::build(stage_text(plan, length), track);
    CAPTURE(result.error);
    REQUIRE_FALSE(result.ok);
    CHECK(result.error.find("núcleo está a") != std::string::npos);
    // El primer punto del primer brazo con el otro a < 1,5 m de núcleo a núcleo: a la altura de
    // donde el segundo brazo, ya girando al este, pasa por x = 9,5 (s ≈ 9,3).
    const double s = error_s(result.error);
    MESSAGE("brazos: ", result.error);
    CHECK(s >= 8.0);
    CHECK(s <= 10.5);
    // Con el segundo brazo a x ≈ 11 (2,9 m entre núcleos) vale.
    const std::string wide = "straight 100\narc " + num(10.0 * 3.14159265358979323846) +
                             " 10\narc 30 100\narc 30 -100\nstraight 20\narc " +
                             num(15.0 * 3.14159265358979323846) + " -30\n";
    const double wide_length = length - 6.6;
    const rally::sim::StageParseResult ok = StageTrack::build(stage_text(wide, wide_length), track);
    INFO(ok.error);
    CHECK(ok.ok);
}

TEST_CASE(
    "stage_track: an arc turning a full turn fails with its s and one of 200 degrees passes") {
    // Tercera respuesta del consultor a tramo-fisica, §4: un arco que gira 360° o más se cierra
    // sobre sí mismo y se rechaza con su s.
    constexpr double kPi = 3.14159265358979323846;
    StageTrack track;
    const double full = 2.0 * kPi * 30.0;
    const rally::sim::StageParseResult bad = StageTrack::build(
        stage_text("straight 50\narc " + num(full) + " 30\nstraight 50\n", 100.0 + full), track);
    CAPTURE(bad.error);
    CHECK_FALSE(bad.ok);
    CHECK(bad.error.rfind("s = 50.00: ", 0) == 0);
    CHECK(bad.error.find("más de una vuelta") != std::string::npos);
    const double part = 200.0 / 180.0 * kPi * 30.0;
    const rally::sim::StageParseResult good = StageTrack::build(
        stage_text("straight 50\narc " + num(part) + " 30\nstraight 50\n", 100.0 + part), track);
    INFO(good.error);
    CHECK(good.ok);
}

// --- Criterio 10: sin reservas
// --------------------------------------------------------------------

TEST_CASE("stage_track: 100000 steps on the road, off it and acquiring do not allocate") {
    const StageTrack& track = lazadas();
    StageRun run(track);
    place(run, track, StagePlace::Start, track.s_begin());
    double sum = 0.0;
    const std::uint64_t before = rally::tests::global_new_count();
    // En carretera.
    double s = track.s_begin();
    for (int i = 0; i < 40'000; ++i) {
        s = std::min(s + 50.0 / 3.6 * kDt, track.s_end());
        const Point p = at(track, s, 1.0);
        sum += step_run(run, p.x, p.z).reset_s;
    }
    // Fuera (con atajos y vueltas) y adquiriendo (saltos sin placed).
    s = 0.0;
    for (int i = 0; i < 60'000; ++i) {
        s += 50.0 / 3.6 * kDt;
        if (s > track.axis().length()) {
            s = 0.0;
        }
        const double t = (i / 1000) % 2 == 0 ? -7.0 : 9.0;
        const Point p = at(track, s, t);
        const StageRun::Step step = step_run(run, p.x, p.z);
        if (step.shortcut) {
            const Point e = at(track, step.reset_s, 0.0);
            run.placed(StagePlace::Shortcut, step.reset_s, e.x, e.z);
        }
        sum += run.state().s;
    }
    const std::uint64_t after = rally::tests::global_new_count();
    CHECK(after - before == 0);
    CHECK(sum != 0.0);
}

// --- Criterio 11: coste
// ---------------------------------------------------------------------------

namespace {

// Media de µs por paso de un punto que recorre el tramo una y otra vez con t fijo.
double step_cost(const StageTrack& track, double t, int steps) {
    StageRun run(track);
    place(run, track, StagePlace::Start, track.s_begin());
    const double ds = 50.0 / 3.6 * kDt;
    double s = track.s_begin();
    double sum = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < steps; ++i) {
        s += ds;
        if (s > track.s_end()) {
            s = track.s_begin();
            run = StageRun(track);
            place(run, track, StagePlace::Start, s);
        }
        const Point p = at(track, s, t);
        sum += step_run(run, p.x, p.z).reset_s;
    }
    const auto t1 = std::chrono::steady_clock::now();
    CHECK(sum != 0.0);
    // Restamos el coste de calcular el punto (pose y seno/coseno del test).
    const auto t2 = std::chrono::steady_clock::now();
    double check = 0.0;
    s = track.s_begin();
    for (int i = 0; i < steps; ++i) {
        s += ds;
        if (s > track.s_end()) {
            s = track.s_begin();
        }
        const Point p = at(track, s, t);
        check += p.x;
    }
    const auto t3 = std::chrono::steady_clock::now();
    CHECK(check != 0.0);
    const double total = std::chrono::duration<double, std::micro>(t1 - t0).count();
    const double points = std::chrono::duration<double, std::micro>(t3 - t2).count();
    return (total - points) / steps;
}

std::string synthetic_30km(double& length) {
    std::string plan;
    length = 0.0;
    for (int i = 0; i < 69; ++i) {
        plan += "straight 100\nspiral 30 inf 60\narc 60 60\nspiral 30 60 inf\n"
                "straight 100\nspiral 30 inf -60\narc 60 -60\nspiral 30 -60 inf\n";
        length += 440.0;
    }
    return plan;
}

} // namespace

TEST_CASE("stage_track: a step costs at most 2 us on the road and 6 us off it in Release") {
    const StageTrack& track = lazadas();
#ifdef NDEBUG
    constexpr int kSteps = 1'000'000;
#else
    constexpr int kSteps = 100'000;
#endif
    const double on_road = step_cost(track, 0.0, kSteps);
    const double off_road = step_cost(track, -7.0, kSteps);

    // Adquisición sola, en puntos de la franja.
    rally::sim::Pcg32 rng(42, 1);
    std::vector<Point> points;
    for (int i = 0; i < 10'000; ++i) {
        const double s = track.s_begin() + (track.s_end() - track.s_begin()) *
                                               (static_cast<double>(rng.next_u32()) / 4294967296.0);
        const double t = 60.0 * (2.0 * (static_cast<double>(rng.next_u32()) / 4294967296.0) - 1.0);
        points.push_back(at(track, s, t));
    }
    double sum = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (const Point& p : points) {
        sum += track.acquire(p.x, p.z).s;
    }
    const auto t1 = std::chrono::steady_clock::now();
    CHECK(sum != 0.0);
    const double acquire = std::chrono::duration<double, std::micro>(t1 - t0).count() / 10'000.0;

    // Carga e índice: lazadas y 30 km sintéticos.
    const auto b0 = std::chrono::steady_clock::now();
    const StageTrack again = load(lazadas_text());
    const auto b1 = std::chrono::steady_clock::now();
    double length = 0.0;
    const std::string plan = synthetic_30km(length);
    const std::string text = stage_text(plan, length);
    const auto b2 = std::chrono::steady_clock::now();
    const StageTrack long_track = load(text);
    const auto b3 = std::chrono::steady_clock::now();
    const double long_on_road = step_cost(long_track, 0.0, kSteps / 10);
    const double long_off_road = step_cost(long_track, -7.0, kSteps / 10);

#ifdef NDEBUG
    const std::string build = "Release";
#else
    const std::string build = "Debug, sin límite";
#endif
    MESSAGE("paso (", build, "): en carretera ", on_road, " µs, fuera ", off_road,
            " µs; adquisición ", acquire, " µs");
    MESSAGE("lazadas: carga ", std::chrono::duration<double, std::milli>(b1 - b0).count(),
            " ms, índice y muestras ", again.index_bytes() / 1024.0, " KiB");
    MESSAGE("30 km (", length, " m): carga ",
            std::chrono::duration<double, std::milli>(b3 - b2).count(), " ms, índice y muestras ",
            long_track.index_bytes() / 1024.0 / 1024.0, " MiB; paso en carretera ", long_on_road,
            " µs, fuera ", long_off_road, " µs");
#ifdef NDEBUG
    CHECK(on_road <= 2.0);
    CHECK(off_road <= 6.0);
#endif
}

// --- Criterio 12: estado
// ----------------------------------------------------------------------------

TEST_CASE("stage_track: the run state is POD and two identical runs give the same bits") {
    static_assert(std::is_trivial_v<StageRunState>);
    static_assert(std::is_standard_layout_v<StageRunState>);
    const StageTrack& track = lazadas();
    const auto run_once = [&track] {
        StageRun run(track);
        place(run, track, StagePlace::Start, -10.0);
        drive_s(track, run, -10.0, 700.0, 1.5, 120.0 / 3.6, kNothing);
        // Atajo a la horquilla, recolocar, salir y volver, botón.
        drive_line(run, at(track, 700.0, 1.5), at(track, kHairpinEnd + 30.0, 0.0), 50.0 / 3.6,
                   [&run, &track](Point, const StageRun::Step& step) {
                       if (step.shortcut) {
                           place(run, track, StagePlace::Shortcut, step.reset_s);
                           return false;
                       }
                       return true;
                   });
        drive_s(track, run, run.state().s, 1100.0, -1.0, 90.0 / 3.6, kNothing);
        drive_line(run, at(track, 1100.0, -1.0), at(track, 1120.0, -15.0), 50.0 / 3.6,
                   [](Point, const StageRun::Step&) { return true; });
        place(run, track, StagePlace::Button, run.state().reset_s);
        drive_s(track, run, run.state().s, track.s_end(), 0.0, 120.0 / 3.6, kNothing);
        return run.state();
    };
    const StageRunState a = run_once();
    const StageRunState b = run_once();
    CHECK(same_state(a, b));
    CHECK(a.shortcuts == 1);
    CHECK(a.resets == 1);
    CHECK(a.finish_step > a.start_step);
    CHECK(a.start_step > 0);
    // last_x y last_z: la posición del último paso.
    const Point last = at(track, track.s_end(), 0.0);
    CHECK(a.last_x == last.x);
    CHECK(a.last_z == last.z);
}
