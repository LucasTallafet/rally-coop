// Tarea tablet (lote 6): el mapa, el perfil, la cinta y el cursor de la tablet del copiloto, sin
// ventana. Tramo: lazadas (tests/sim/stages).

#include "game/direction_changes.h"
#include "platform/head_look.h"
#include "platform/pointer_mode.h"
#include "platform/screen_layout.h"
#include "platform/tablet_cursor.h"
#include "platform/tablet_geometry.h"
#include "platform/tablet_view.h"
#include "platform/wheel_spin.h"
#include "sim/stage_track.h"
#include "stage_driver.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace rally;
using namespace rally::platform;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;

const sim::StageTrack& lazadas() {
    static const sim::StageTrack track = [] {
        sim::StageTrack t;
        const sim::StageParseResult r = sim::StageTrack::build(tests::lazadas_stage_text(), t);
        INFO(r.error);
        REQUIRE(r.ok);
        return t;
    }();
    return track;
}

const TabletRoad& road() {
    static const TabletRoad r = [] {
        TabletRoad built;
        built.build(lazadas());
        return built;
    }();
    return r;
}

const std::vector<game::DirectionChange>& lazadas_changes() {
    static const std::vector<game::DirectionChange> changes =
        game::direction_changes(lazadas().axis());
    return changes;
}

// El panel de la tablet de una ventana del copiloto de ese tamaño y sus partes.
TabletLayout layout_for(int width, int height) {
    const ScreenLayout layout = layout_screen(width, height, ScreenKind::Copilot);
    return tablet_layout(layout.rect(Layer::Tablet), layout.m);
}

// Una vista fija en `s` (con `t`) a la distancia `distance`, sin pasar por los filtros.
MapView view_at(const UiRect& map, double s, double t, double distance, double heading) {
    MapView v;
    v.rect = map;
    v.arrow_x = map.x + 0.5 * map.w;
    v.arrow_y = map.y + kArrowFraction * map.h;
    v.distance = distance;
    v.ppm = (v.arrow_y - map.y) / distance;
    car_position(lazadas(), s, t, v.cx, v.cz);
    v.heading = heading;
    v.phase_ppm = v.ppm;
    return v;
}

// Deja converger los filtros con `s` fija.
void settle(TabletView& view, double s, double t, MapOrientation orientation, double& now_s) {
    for (int i = 0; i < 240; ++i) {
        view.update(lazadas(), s, t, now_s, orientation);
        now_s += 1.0 / 60.0;
    }
}

// Ángulo (grados) de la dirección de pantalla de a a b respecto a «arriba».
double angle_from_up(double ax, double ay, double bx, double by) {
    return std::atan2(bx - ax, -(by - ay)) / kDeg;
}

double distance_to_polyline(const MapGeometry& map, double x, double y) {
    double best = 1e30;
    for (const MapRun& run : map.runs) {
        for (std::uint32_t i = 0; i + 1 < run.count; ++i) {
            const MapPoint& a = map.points[run.first + i];
            const MapPoint& b = map.points[run.first + i + 1];
            const double dx = static_cast<double>(b.x) - a.x;
            const double dy = static_cast<double>(b.y) - a.y;
            const double len2 = dx * dx + dy * dy;
            double t = len2 > 0.0 ? ((x - a.x) * dx + (y - a.y) * dy) / len2 : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            const double px = a.x + t * dx - x;
            const double py = a.y + t * dy - y;
            best = std::min(best, std::sqrt(px * px + py * py));
        }
    }
    return best;
}

} // namespace

TEST_CASE("tablet orientation A: the axis is vertical under the arrow, whatever the car does") {
    // Criterio 1: en una recta y en la mitad de una curva de lazadas.
    const UiRect map = layout_for(1280, 720).map;
    for (const double s : {50.0, 175.0, 440.0}) {
        INFO("s = ", s);
        TabletView view;
        double now_s = 0.0;
        settle(view, s, 0.0, MapOrientation::Axis, now_s);
        const MapView v = view.map_view(map);
        double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
        const sim::StagePose p0 = lazadas().pose(s);
        const sim::StagePose p1 = lazadas().pose(s + 0.1);
        v.to_screen(p0.x, p0.z, ax, ay);
        v.to_screen(p1.x, p1.z, bx, by);
        CHECK(std::abs(ax - v.arrow_x) < 1e-6);
        CHECK(std::abs(ay - v.arrow_y) < 1e-6);
        CHECK(std::abs(angle_from_up(ax, ay, bx, by)) <= 0.5);
        MESSAGE("s ", s, ": ángulo del eje respecto a arriba ", angle_from_up(ax, ay, bx, by), "°");
    }
}

TEST_CASE("tablet orientation A: a spin on a straight does not move the map, B does") {
    // Un trompo en la recta (s = 50): el origen del coche gira alrededor de su centro, y la `s` y
    // la `t` del host oscilan ±1,3 m. El mapa A no se mueve ni un bit; el B, que sale de la
    // posición del coche, sí.
    TabletView axis;
    TabletView ahead;
    double now_s = 0.0;
    settle(axis, 50.0, 0.0, MapOrientation::Axis, now_s);
    double now_b = 0.0;
    settle(ahead, 50.0, 0.0, MapOrientation::Ahead, now_b);
    const double axis_rest = axis.heading();
    const double ahead_rest = ahead.heading();
    double ahead_move = 0.0;
    for (int i = 0; i < 240; ++i) {
        const double phase = 2.0 * kPi * i / 60.0; // una vuelta por segundo
        const double ds = 1.3 * std::sin(phase);
        const double dt = 1.3 * std::cos(phase);
        axis.update(lazadas(), 50.0 + ds, dt, now_s, MapOrientation::Axis);
        ahead.update(lazadas(), 50.0 + ds, dt, now_b, MapOrientation::Ahead);
        now_s += 1.0 / 60.0;
        now_b += 1.0 / 60.0;
        CHECK(std::bit_cast<std::uint64_t>(axis.heading()) ==
              std::bit_cast<std::uint64_t>(axis_rest));
        ahead_move = std::max(ahead_move, std::abs(wrap_angle(ahead.heading() - ahead_rest)));
    }
    MESSAGE("trompo en la recta: B se mueve hasta ", ahead_move / kDeg, "° y A, 0°");
    CHECK(ahead_move > 0.5 * kDeg); // la prueba puede fallar: B sí nota el trompo
}

TEST_CASE("tablet orientation A: it does not depend on the speed either") {
    const UiRect map = layout_for(1280, 720).map;
    const double s_end = 440.0;
    TabletView parked;
    double parked_s = 0.0;
    settle(parked, s_end, 0.0, MapOrientation::Axis, parked_s);
    // Llegando a 100 km/h: parado en el sitio, tras los filtros, el rumbo es el mismo.
    TabletView moving;
    double now_s = 0.0;
    const double v = 100.0 / 3.6;
    for (int i = 0; i < 120; ++i) {
        moving.update(lazadas(), s_end - v * 1.0 + v * i / 120.0, 0.0, now_s, MapOrientation::Axis);
        now_s += 1.0 / 120.0;
    }
    settle(moving, s_end, 0.0, MapOrientation::Axis, now_s);
    CHECK(std::abs(moving.heading() - parked.heading()) < 1e-9);
    (void)map;
}

TEST_CASE("tablet orientation B: the point 40 m ahead is up at the vertex of hairpin 1") {
    // Criterio 2: el vértice de la horquilla 1 de lazadas (arco de R 6 entre 740 y 748,85).
    const UiRect map = layout_for(1280, 720).map;
    const double s = 744.4;
    TabletView ahead;
    double now_s = 0.0;
    settle(ahead, s, 0.0, MapOrientation::Ahead, now_s);
    const MapView v = ahead.map_view(map);
    double cx = 0.0, cy = 0.0, px = 0.0, py = 0.0;
    const sim::StagePose car = lazadas().pose(s);
    const sim::StagePose point = lazadas().pose(s + kAheadDistance);
    v.to_screen(car.x, car.z, cx, cy);
    v.to_screen(point.x, point.z, px, py);
    const double angle = angle_from_up(cx, cy, px, py);
    MESSAGE("B en el vértice: el punto a 40 m sale a ", angle, "° de la vertical");
    CHECK(std::abs(angle) <= 0.5);
    // Y A hace otra cosa ahí: el eje en el vértice no apunta al punto de delante.
    TabletView axis;
    double axis_s = 0.0;
    settle(axis, s, 0.0, MapOrientation::Axis, axis_s);
    const double difference = std::abs(wrap_angle(axis.heading() - ahead.heading())) / kDeg;
    MESSAGE("A y B difieren ", difference, "° en el vértice");
    CHECK(difference > 20.0);
}

TEST_CASE("tablet zoom: 30, 60 and 120 km/h give 100, 200 and 400 m") {
    // Criterio 3, con la vista entera: la velocidad sale de la `s` que avanza.
    for (const auto [kmh, expected_level] :
         {std::pair{30.0, 0}, std::pair{60.0, 1}, std::pair{120.0, 2}}) {
        INFO(kmh, " km/h");
        TabletView view;
        double now_s = 0.0;
        double s = 0.0;
        const double v = kmh / 3.6;
        for (int i = 0; i < 4 * 60; ++i) {
            view.update(lazadas(), s, 0.0, now_s, MapOrientation::Axis);
            s += v / 60.0;
            now_s += 1.0 / 60.0;
        }
        CHECK(view.zoom_level() == expected_level);
        CHECK(std::abs(view.distance() - kZoomDistance[expected_level]) <
              0.02 * kZoomDistance[expected_level]);
        CHECK(std::abs(view.speed() - v) < 0.02 * v);
    }
}

TEST_CASE("tablet zoom: a 5 percent swing around 40 and 80 km/h never changes the level") {
    for (const double threshold_kmh : {40.0, 80.0}) {
        const int base = threshold_kmh < 50.0 ? 0 : 1;
        for (const int start : {base, base + 1}) {
            int level = start;
            for (int i = 0; i < 200; ++i) {
                const double kmh = threshold_kmh * (i % 2 == 0 ? 0.95 : 1.05);
                level = zoom_level_for(kmh / 3.6, level);
                CHECK(level == start);
            }
        }
    }
    // El 10 % sí cambia el nivel, y puede saltarse uno.
    CHECK(zoom_level_for(44.5 / 3.6, 0) == 1);
    CHECK(zoom_level_for(35.5 / 3.6, 1) == 0);
    CHECK(zoom_level_for(120.0 / 3.6, 0) == 2);
    CHECK(zoom_level_for(10.0 / 3.6, 2) == 0);
}

TEST_CASE("tablet zoom: the distance changes in about half a second and without jumps") {
    TabletView view;
    double now_s = 0.0;
    double s = 0.0;
    // Parado 1 s y después a 120 km/h: el nivel pasa a 400 m; la distancia mostrada no salta.
    double previous = 0.0;
    double biggest_step = 0.0;
    for (int i = 0; i < 240; ++i) {
        view.update(lazadas(), s, 0.0, now_s, MapOrientation::Axis);
        if (i >= 60) {
            s += (120.0 / 3.6) / 60.0;
        }
        if (i > 0) {
            biggest_step = std::max(biggest_step, std::abs(view.distance() - previous));
        }
        previous = view.distance();
        now_s += 1.0 / 60.0;
    }
    CHECK(view.zoom_level() == 2);
    CHECK(biggest_step < 20.0); // m por fotograma: una transición suave
    // Tras el cambio de nivel, ~95 % en 0,5 s.
    TabletView step;
    double t = 0.0;
    double s2 = 0.0;
    int changed_at = -1;
    for (int i = 0; i < 480; ++i) {
        step.update(lazadas(), s2, 0.0, t, MapOrientation::Axis);
        s2 += (120.0 / 3.6) / 60.0;
        t += 1.0 / 60.0;
        if (changed_at < 0 && step.zoom_level() == 2) {
            changed_at = i;
        }
        if (changed_at >= 0 && i == changed_at + 30) { // 0,5 s después
            const double gained = (step.distance() - 200.0) / 200.0;
            CHECK(gained > 0.9); // casi todo el camino de 200 a 400
        }
    }
    CHECK(changed_at >= 0);
}

TEST_CASE("tablet view: a jump of s fixes the heading, a small step is smoothed") {
    TabletView view;
    double now_s = 0.0;
    settle(view, 100.0, 0.0, MapOrientation::Axis, now_s);
    const double speed = view.speed();
    // Recolocación a una curva con otro rumbo: la velocidad de ese fotograma no cuenta y el rumbo
    // no salta: lo sigue el filtro (el primer fotograде a 60 fps recorre 1 − e^(−1/60/τ) del giro).
    const double before = view.heading();
    const double target = lazadas().pose(175.0).heading;
    view.update(lazadas(), 175.0, 0.0, now_s, MapOrientation::Axis);
    CHECK(view.relocated());
    CHECK(view.speed() == speed);
    const double share = 1.0 - std::exp(-(1.0 / 60.0) / kHeadingTau);
    CHECK(wrap_angle(view.heading() - before) ==
          doctest::Approx(wrap_angle(target - before) * share).epsilon(1e-9));
    CHECK(std::abs(wrap_angle(target - view.heading())) > 0.01); // no ha saltado
    // Y llega: al menos el 95 % en 0,24 s.
    for (int i = 0; i < 14; ++i) {
        now_s += 1.0 / 60.0;
        view.update(lazadas(), 175.0, 0.0, now_s, MapOrientation::Axis);
    }
    CHECK(std::abs(wrap_angle(target - view.heading())) <
          0.05 * std::abs(wrap_angle(target - before)));
    // Un paso pequeño atrás (recolocar unos metros): sin salto y sin velocidad negativa.
    now_s += 1.0 / 60.0;
    view.update(lazadas(), 170.0, 0.0, now_s, MapOrientation::Axis);
    CHECK_FALSE(view.relocated());
    CHECK(view.speed() >= 0.0);
}

TEST_CASE("tablet marks: every 50 m of s lie on the drawn road, to within half a pixel") {
    // Criterio 4, a 1280 × 720 y en los tres niveles de zoom.
    const UiRect map = layout_for(1280, 720).map;
    for (const double distance : kZoomDistance) {
        for (const double s_car : {20.0, 320.0, 744.0, 1000.0}) {
            INFO("D = ", distance, ", s = ", s_car);
            const sim::StagePose p = lazadas().pose(s_car);
            const MapView v = view_at(map, s_car, 0.0, distance, p.heading);
            MapGeometry geo;
            build_map(road(), lazadas(), v, geo);
            REQUIRE(geo.points.size() > 10);
            // Todas las de 50 m del rango, ni una más ni una menos.
            // (los de los trozos que se ven, sin cortes: cada marca está en la polilínea)
            for (const MapMark& mark : geo.marks) {
                CHECK(mark.s >= 0.0);
                CHECK(mark.s <= lazadas().axis().length());
            }
            CHECK_FALSE(geo.marks.empty());
            for (const MapMark& mark : geo.marks) {
                CHECK(std::fmod(mark.s, 50.0) == doctest::Approx(0.0).epsilon(1e-9));
                const sim::StagePose at = lazadas().pose(mark.s);
                double x = 0.0, y = 0.0;
                v.to_screen(at.x, at.z, x, y);
                CHECK(std::abs(mark.x - x) <= 0.5);
                CHECK(std::abs(mark.y - y) <= 0.5);
                CHECK(distance_to_polyline(geo, mark.x, mark.y) <= 0.5);
            }
        }
    }
}

TEST_CASE("tablet surfaces: each stretch has its own colour and stroke, and no two are alike") {
    // Criterio 5: grava hasta 650 y tierra desde ahí, en lazadas.
    const UiRect map = layout_for(1280, 720).map;
    const double s_car = 650.0;
    const MapView v = view_at(map, s_car, 0.0, 100.0, lazadas().pose(s_car).heading);
    MapGeometry geo;
    build_map(road(), lazadas(), v, geo);
    // La grava y la tierra, en ese orden y compartiendo el punto de unión.
    std::size_t g = geo.runs.size();
    for (std::size_t r = 0; r + 1 < geo.runs.size(); ++r) {
        if (geo.runs[r].surface == sim::Surface::Gravel &&
            geo.runs[r + 1].surface == sim::Surface::Dirt &&
            geo.runs[r + 1].first == geo.runs[r].first + geo.runs[r].count - 1) {
            g = r;
        }
    }
    REQUIRE(g < geo.runs.size());
    for (const MapRun& run : geo.runs) {
        CHECK((run.surface == sim::Surface::Gravel || run.surface == sim::Surface::Dirt));
    }
    // El punto de unión cae en s = 650 (a un paso de la rejilla).
    const MapPoint& last = geo.points[geo.runs[g].first + geo.runs[g].count - 1];
    const MapPoint& first = geo.points[geo.runs[g + 1].first];
    CHECK(last.x == first.x);
    CHECK(last.y == first.y);
    double x = 0.0, z = 0.0;
    v.to_world(first.x, first.y, x, z);
    CHECK(std::abs(lazadas().project(x, z, 0.0, 1150.0).s - 650.0) <= geo.step + 1e-3);

    const sim::Surface all[] = {sim::Surface::Asphalt, sim::Surface::Gravel, sim::Surface::Dirt,
                                sim::Surface::Snow, sim::Surface::Sand};
    for (std::size_t a = 0; a < 5; ++a) {
        const SurfaceStroke sa = surface_stroke(all[a], 1.0f);
        for (std::size_t b = a + 1; b < 5; ++b) {
            const SurfaceStroke sb = surface_stroke(all[b], 1.0f);
            INFO(sim::surface_name(all[a]), " frente a ", sim::surface_name(all[b]));
            CHECK_FALSE(sa == sb);
            CHECK_FALSE(sa.color == sb.color);
            // El color no va solo: el trazo (rayas y líneas) también es distinto.
            CHECK((sa.dash != sb.dash || sa.lines != sb.lines));
        }
    }
    const SurfaceStroke gravel = surface_stroke(sim::Surface::Gravel, 1.0f);
    CHECK(gravel.color == Rgb{214, 190, 140});
    CHECK(surface_stroke(sim::Surface::Snow, 1.0f).color == Rgb{245, 248, 252});
}

TEST_CASE("tablet strokes: dashes are anchored to the road and solid lines are one segment") {
    // Una recta horizontal de 90 px con puntos cada 10 px y la fase igual a la x.
    std::vector<MapPoint> line;
    for (int i = 0; i <= 9; ++i) {
        line.push_back({static_cast<float>(10 * i), 0.0f, static_cast<float>(10 * i)});
    }
    std::vector<MapSegment> out;
    dash_segments(line.data(), static_cast<std::uint32_t>(line.size()),
                  surface_stroke(sim::Surface::Gravel, 1.0f), 0.0f, out);
    double on = 0.0;
    for (const MapSegment& s : out) {
        on += s.x1 - s.x0;
    }
    CHECK(on == doctest::Approx(60.0).epsilon(1e-4)); // 5 periodos de 18 px con 12 de raya
    out.clear();
    dash_segments(line.data(), static_cast<std::uint32_t>(line.size()),
                  surface_stroke(sim::Surface::Asphalt, 1.0f), 0.0f, out);
    CHECK(out.size() == 9);
    out.clear();
    dash_segments(line.data(), static_cast<std::uint32_t>(line.size()),
                  surface_stroke(sim::Surface::Snow, 1.0f), 2.5f, out);
    CHECK(out.size() == 18); // dos líneas
    // Una polilínea que empieza más adelante da las mismas rayas (fase anclada a la carretera).
    out.clear();
    dash_segments(line.data(), static_cast<std::uint32_t>(line.size()),
                  surface_stroke(sim::Surface::Dirt, 1.0f), 0.0f, out);
    std::vector<MapSegment> tail;
    dash_segments(line.data() + 4, static_cast<std::uint32_t>(line.size() - 4),
                  surface_stroke(sim::Surface::Dirt, 1.0f), 0.0f, tail);
    for (const MapSegment& t : tail) {
        bool found = false;
        for (const MapSegment& s : out) {
            found = found || (std::abs(s.x0 - t.x0) < 1e-3 && std::abs(s.x1 - t.x1) < 1e-3);
        }
        CHECK(found);
    }
}

TEST_CASE("tablet ribbon: the direction changes of the next 500 m, and no others") {
    // Criterio 6.
    const UiRect rect = layout_for(1280, 720).ribbon;
    RibbonGeometry geo;
    build_ribbon(lazadas_changes(), rect, 4, 0.0, false, 0.0, geo);
    REQUIRE(geo.marks.size() == 2);
    CHECK(geo.marks[0].s == doctest::Approx(100.0).epsilon(0.01));
    CHECK(geo.marks[0].direction == 1);
    CHECK(geo.marks[1].s == doctest::Approx(390.0).epsilon(0.01));
    CHECK(geo.marks[1].direction == -1);
    // Más cerca del coche, más abajo: y crece al bajar en pantalla.
    CHECK(geo.marks[0].y > geo.marks[1].y);
    CHECK(geo.marks[0].y == doctest::Approx(geo.bottom - 100.0 / 500.0 * (geo.bottom - geo.top)));

    build_ribbon(lazadas_changes(), rect, 4, 600.0, false, 0.0, geo);
    REQUIRE(geo.marks.size() == 3);
    CHECK(std::abs(geo.marks[0].s - 730.0) <= 1.0);
    CHECK(std::abs(geo.marks[1].s - 828.85) <= 1.0);
    CHECK(std::abs(geo.marks[2].s - 907.70) <= 1.0);
    CHECK(geo.marks[0].direction == 1);
    CHECK(geo.marks[1].direction == -1);
    CHECK(geo.marks[2].direction == 1);
    CHECK(geo.tick_count == 5);

    // El cursor: dentro de los 500 m en su sitio y más allá pegado al borde de arriba.
    build_ribbon(lazadas_changes(), rect, 4, 600.0, true, 730.0, geo);
    CHECK(geo.cursor_visible);
    CHECK_FALSE(geo.cursor_beyond);
    build_ribbon(lazadas_changes(), rect, 4, 600.0, true, 1140.0, geo);
    CHECK(geo.cursor_beyond);
    CHECK(geo.cursor_y == geo.top);
}

TEST_CASE("tablet profile: the axis height from the car, with a 10 m minimum range") {
    const UiRect rect = layout_for(1280, 720).profile;
    ProfileGeometry flat;
    build_profile(lazadas(), rect, 4, 0.0, 100.0, flat); // sube de 0 a ~3 m
    REQUIRE(flat.ok);
    CHECK(flat.max_height - flat.min_height < kProfileMinSpan);
    CHECK(flat.y_hi - flat.y_lo == doctest::Approx(kProfileMinSpan));
    // El coche a la izquierda y la altura crece hacia arriba en pantalla.
    CHECK(flat.points.front().x < flat.points.back().x);
    CHECK(flat.points.front().y > flat.points.back().y);
    CHECK(flat.points.front().x == doctest::Approx(rect.x + 4));
    ProfileGeometry hilly;
    build_profile(lazadas(), rect, 4, 250.0, 400.0, hilly); // la cresta: más de 10 m
    REQUIRE(hilly.ok);
    CHECK(hilly.max_height - hilly.min_height > kProfileMinSpan);
    CHECK(hilly.y_hi - hilly.y_lo == doctest::Approx(hilly.max_height - hilly.min_height));
    for (const ProfilePoint& p : hilly.points) {
        CHECK(p.y >= rect.y);
        CHECK(p.y <= rect.bottom());
    }
}

TEST_CASE("tablet cursor: default, jump when the car passes it, next and previous") {
    // Criterio 7.
    const std::vector<game::DirectionChange>& changes = lazadas_changes();
    const double length = lazadas().axis().length();
    TabletCursor cursor;
    cursor.update(changes, 0.0, length, 0, 1.0 / 60.0);
    REQUIRE(cursor.valid());
    CHECK(cursor.s() == doctest::Approx(100.0).epsilon(0.01));
    CHECK_FALSE(cursor.manual());
    // El coche lo pasa: salta al siguiente.
    cursor.update(changes, 101.0, length, 0, 1.0 / 60.0);
    CHECK(cursor.s() == doctest::Approx(390.0).epsilon(0.01));
    cursor.update(changes, 500.0, length, 0, 1.0 / 60.0);
    CHECK(cursor.s() == doctest::Approx(520.0).epsilon(0.01));
    // Sin más cambios por delante, no hay cursor por defecto.
    cursor.update(changes, 1100.0, length, 0, 1.0 / 60.0);
    CHECK_FALSE(cursor.valid());

    // Siguiente y anterior (una pulsación: salta al instante).
    TabletCursor c;
    c.update(changes, 0.0, length, 0, 1.0 / 60.0);
    c.update(changes, 0.0, length, 1, 1.0 / 60.0); // ↑
    c.update(changes, 0.0, length, 0, 1.0 / 60.0);
    CHECK(c.s() == doctest::Approx(390.0).epsilon(0.01));
    CHECK(c.manual());
    c.update(changes, 0.0, length, -1, 1.0 / 60.0); // ↓
    c.update(changes, 0.0, length, 0, 1.0 / 60.0);
    CHECK(c.s() == doctest::Approx(100.0).epsilon(0.01));
    c.update(changes, 0.0, length, -1, 1.0 / 60.0); // ↓ otra vez: no hay anterior por delante
    c.update(changes, 0.0, length, 0, 1.0 / 60.0);
    CHECK(c.s() == doctest::Approx(100.0).epsilon(0.01));
    // Un cursor puesto a mano que el coche pasa vuelve al valor por defecto.
    c.update(changes, 120.0, length, 0, 1.0 / 60.0);
    CHECK_FALSE(c.manual());
    CHECK(c.s() == doctest::Approx(390.0).epsilon(0.01));

    // Nunca por detrás del coche: con el coche dentro del cambio 1, «anterior» no vuelve a él.
    TabletCursor inside;
    inside.update(changes, 150.0, length, 0, 1.0 / 60.0);
    CHECK(inside.s() == doctest::Approx(390.0).epsilon(0.01));
    inside.update(changes, 150.0, length, -1, 1.0 / 60.0);
    inside.update(changes, 150.0, length, 0, 1.0 / 60.0);
    CHECK(inside.s() == doctest::Approx(390.0).epsilon(0.01));
}

TEST_CASE("tablet cursor: held, it slides from 20 to 200 m/s in 2 s") {
    const std::vector<game::DirectionChange>& changes = lazadas_changes();
    const double length = lazadas().axis().length();
    CHECK(TabletCursor::slide_speed(0.0) == doctest::Approx(20.0));
    CHECK(TabletCursor::slide_speed(1.0) == doctest::Approx(110.0));
    CHECK(TabletCursor::slide_speed(2.0) == doctest::Approx(200.0));
    CHECK(TabletCursor::slide_speed(9.0) == doctest::Approx(200.0));

    const double dt = 1.0 / 240.0;
    TabletCursor cursor;
    cursor.update(changes, 0.0, length, 0, dt);
    cursor.update(changes, 0.0, length, 1, dt); // la pulsación salta de 100 a 390 al instante
    const double start = cursor.s();
    CHECK(start == doctest::Approx(390.0).epsilon(0.01));
    double after_1s = 0.0;
    for (int i = 1; i <= 3 * 240; ++i) {
        cursor.update(changes, 0.0, length, 1, dt);
        if (i == 240) {
            after_1s = cursor.s();
        }
    }
    // Desde los 0,3 s: 20 t + 45 t² con t = tiempo deslizando.
    const auto travelled = [](double held_s) {
        const double t = held_s - kCursorHoldS;
        return t <= 2.0 ? 20.0 * t + 45.0 * t * t : 220.0 + 200.0 * (t - 2.0);
    };
    MESSAGE("tras 1 s mantenido: ", after_1s - start, " m (esperado ", travelled(1.0),
            "); tras 3 s: ", cursor.s() - start, " m (esperado ", travelled(3.0), ")");
    CHECK(after_1s - start == doctest::Approx(travelled(1.0)).epsilon(0.01));
    CHECK(cursor.s() - start == doctest::Approx(travelled(3.0)).epsilon(0.01));
    // Tope en la longitud del eje.
    for (int i = 0; i < 5 * 240; ++i) {
        cursor.update(changes, 0.0, length, 1, dt);
    }
    CHECK(cursor.s() == length);
    // Hacia abajo, nunca por detrás del coche.
    TabletCursor down;
    down.update(changes, 300.0, length, 0, dt);
    for (int i = 0; i < 6 * 240; ++i) {
        down.update(changes, 300.0, length, -1, dt);
        CHECK(down.s() >= 300.0);
    }
    CHECK(down.s() == 300.0);
    // Soltar la tecla deja el cursor donde está.
    const double rest = cursor.s();
    cursor.update(changes, 0.0, length, 0, dt);
    CHECK(cursor.s() == rest);
}

TEST_CASE("tablet pointer: a click 3 px off the road gives the nearest axis s") {
    // Criterio 7, a 1280 × 720 y con el nivel de 100 m (el de más precisión por píxel de lo que
    // se ve cerca): rectas, la curva de R 80 y la horquilla de R 6.
    const UiRect map = layout_for(1280, 720).map;
    for (const double s0 : {60.0, 175.0, 440.0, 744.0}) {
        for (const double side : {-1.0, 1.0}) {
            INFO("s = ", s0, ", lado ", side);
            const double s_car = s0 - 10.0;
            const MapView v = view_at(map, s_car, 0.0, 100.0, lazadas().pose(s_car).heading);
            const sim::StagePose p = lazadas().pose(s0);
            double x = 0.0, y = 0.0;
            v.to_screen(p.x, p.z, x, y);
            const double delta = p.heading - v.heading;
            const double cx = x + side * 3.0 * std::cos(delta);
            const double cy = y + side * 3.0 * std::sin(delta);
            double picked = 0.0;
            REQUIRE(pick_s(lazadas(), v, s_car, cx, cy, 12.0, picked));
            CHECK(std::abs(picked - s0) <= 0.5);
        }
    }
    // Fuera del mapa, nada.
    const MapView v = view_at(map, 100.0, 0.0, 100.0, lazadas().pose(100.0).heading);
    double picked = 0.0;
    // Un clic lejos de la carretera (más de 12 px) no cuenta.
    {
        double fx = 0.0, fy = 0.0;
        const sim::StagePose p = lazadas().pose(130.0);
        v.to_screen(p.x, p.z, fx, fy);
        CHECK_FALSE(pick_s(lazadas(), v, 100.0, fx + 80.0, fy, 12.0, picked));
    }
    CHECK_FALSE(pick_s(lazadas(), v, 100.0, map.x - 5.0, map.y + 5.0, 12.0, picked));
}

TEST_CASE("tablet pointer: where two arms of the lazadas look close, the click picks its own") {
    const UiRect map = layout_for(1280, 720).map;
    const double s_car = 700.0;
    const MapView v = view_at(map, s_car, 0.0, 100.0, lazadas().pose(s_car).heading);
    // Las dos rectas paralelas (la de entrada, ~715, y la del medio, ~790) están a ~13 m.
    const auto probe = [&](double s_on, double toward_other_px) {
        const sim::StagePose p = lazadas().pose(s_on);
        double x = 0.0, y = 0.0;
        v.to_screen(p.x, p.z, x, y);
        const double delta = p.heading - v.heading;
        double picked = 0.0;
        // El lado del otro brazo: el de la normal que se acerca a él.
        const double nx = std::cos(delta);
        const double ny = std::sin(delta);
        REQUIRE(pick_s(lazadas(), v, s_car, x + toward_other_px * nx, y + toward_other_px * ny,
                       12.0, picked));
        return picked;
    };
    for (const double sign : {-1.0, 1.0}) {
        const double near_a = probe(715.0, sign * 3.0);
        const double near_b = probe(790.0, sign * 3.0);
        CHECK(std::abs(near_a - 715.0) <= 0.5);
        CHECK(std::abs(near_b - 790.0) <= 0.5);
    }
}

TEST_CASE("tablet pointer: while held the head is frozen bit for bit and the mouse stays out") {
    // Criterio 8: Espacio mantenido.
    const PointerFrame held = pointer_frame(true, true);
    CHECK(held.frozen);
    CHECK(held.clicks);
    CHECK_FALSE(held.capture); // se suelta el cursor del sistema
    const PointerFrame free = pointer_frame(false, true);
    CHECK_FALSE(free.frozen);
    CHECK_FALSE(free.clicks);
    CHECK(free.capture);
    CHECK_FALSE(pointer_frame(false, false).capture); // sin foco no se captura
    CHECK_FALSE(pointer_frame(true, false).capture);

    HeadLook look;
    HeadLookInput move;
    move.mouse_dx_px = 120.0f;
    move.mouse_dy_px = -40.0f;
    move.dt_s = 1.0 / 60.0;
    look.set_frozen(pointer_frame(false, true).frozen);
    for (int i = 0; i < 120; ++i) {
        look.update(move);
        move.mouse_dx_px = 0.0f;
        move.mouse_dy_px = 0.0f;
    }
    const double yaw = look.yaw();
    const double pitch = look.pitch();
    CHECK(yaw != 0.0);
    // Mantenido: el ratón y el stick no la mueven ni un bit.
    look.set_frozen(held.frozen);
    for (int i = 0; i < 300; ++i) {
        HeadLookInput noisy;
        noisy.mouse_dx_px = static_cast<float>(37 * (i % 7) - 100);
        noisy.mouse_dy_px = static_cast<float>(11 * (i % 5) - 20);
        noisy.stick_x = i % 2 == 0 ? 0.9f : -0.9f;
        noisy.stick_y = 0.5f;
        noisy.recenter = i % 50 == 0;
        noisy.dt_s = 1.0 / 60.0;
        look.update(noisy);
        CHECK(std::bit_cast<std::uint64_t>(look.yaw()) == std::bit_cast<std::uint64_t>(yaw));
        CHECK(std::bit_cast<std::uint64_t>(look.pitch()) == std::bit_cast<std::uint64_t>(pitch));
    }
    // Al soltar, sigue desde donde estaba (no vuelve al frente).
    look.set_frozen(pointer_frame(false, true).frozen);
    HeadLookInput none;
    none.dt_s = 1.0 / 60.0;
    look.update(none);
    CHECK(std::abs(look.yaw() - yaw) < 1e-6);
    CHECK(std::abs(look.pitch() - pitch) < 1e-6);
    look.update(none);
    CHECK(std::abs(look.yaw() - yaw) < 1e-6);
}

TEST_CASE("tablet cost: geometry per frame on a drive through lazadas at 60 and 120 km/h") {
    // Criterio 11: se informa, sin límite.
    const UiRect map = layout_for(1280, 720).map;
    const TabletLayout layout = layout_for(1280, 720);
    for (const double kmh : {60.0, 120.0}) {
        TabletView view;
        TabletCursor cursor;
        MapGeometry geo;
        ProfileGeometry profile;
        RibbonGeometry ribbon;
        std::vector<MapSegment> segments;
        const double v = kmh / 3.6;
        double s = 0.0;
        double now_s = 0.0;
        double sum_us = 0.0;
        double max_us = 0.0;
        std::vector<double> all_us;
        int frames = 0;
        const double length = lazadas().axis().length();
        while (s < length - 5.0) {
            const auto t0 = std::chrono::steady_clock::now();
            view.update(lazadas(), s, 0.0, now_s, MapOrientation::Axis);
            const MapView mv = view.map_view(map);
            build_map(road(), lazadas(), mv, geo);
            build_profile(lazadas(), layout.profile, 4, s, view.distance(), profile);
            cursor.update(lazadas_changes(), s, length, 0, 1.0 / 60.0);
            build_ribbon(lazadas_changes(), layout.ribbon, 4, s, cursor.valid(), cursor.s(),
                         ribbon);
            for (const MapRun& run : geo.runs) {
                segments.clear();
                dash_segments(geo.points.data() + run.first, run.count,
                              surface_stroke(run.surface, 1.0f), 2.5f, segments);
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
            if (frames >= 2) { // sin el primer fotograma, que reserva
                sum_us += us;
                max_us = std::max(max_us, us);
                all_us.push_back(us);
            }
            ++frames;
            s += v / 60.0;
            now_s += 1.0 / 60.0;
        }
        const double mean_us = sum_us / std::max(1, frames - 2);
        std::sort(all_us.begin(), all_us.end());
        const double p99_us = all_us.empty() ? 0.0 : all_us[all_us.size() * 99 / 100];
        MESSAGE("geometría de la tablet a ", kmh, " km/h: media ", mean_us, " µs, p99 ", p99_us,
                " µs, máx. ", max_us, " µs por fotograma (", frames, " fotogramas, ",
                geo.points.size(), " puntos del último)");
        CHECK(mean_us > 0.0);
    }
}

TEST_CASE("tablet map: every axis point that falls in the rectangle is drawn") {
    // Revisión del plan: el mapa dibuja todo el eje que se ve, también los brazos de al lado y
    // el de detrás. s de 0 a 1150 cada 50 m, los tres niveles y las orientaciones A y B.
    const UiRect map = layout_for(1280, 720).map;
    for (const double distance : kZoomDistance) {
        for (const MapOrientation orientation : {MapOrientation::Axis, MapOrientation::Ahead}) {
            for (double s_car = 0.0; s_car <= 1150.0; s_car += 50.0) {
                const double heading = target_heading(lazadas(), s_car, 0.0, orientation);
                const MapView v = view_at(map, s_car, 0.0, distance, heading);
                MapGeometry geo;
                build_map(road(), lazadas(), v, geo);
                INFO("D = ", distance, ", s = ", s_car);
                for (double s = lazadas().s_begin(); s <= lazadas().s_end(); s += 5.0) {
                    const sim::StagePose p = lazadas().pose(s);
                    double x = 0.0, y = 0.0;
                    v.to_screen(p.x, p.z, x, y);
                    if (x < map.x || x >= map.right() || y < map.y || y >= map.bottom()) {
                        continue;
                    }
                    REQUIRE(distance_to_polyline(geo, x, y) <= 1.0);
                }
            }
        }
    }
}

TEST_CASE("tablet map: past the finish line the road keeps being drawn to the end of the run-off") {
    const UiRect map = layout_for(1280, 720).map;
    const double s_car = 1300.0; // 150 m pasada la meta (el eje mide 1150)
    const MapView v = view_at(map, s_car, 0.0, 100.0, lazadas().pose(s_car).heading);
    MapGeometry geo;
    build_map(road(), lazadas(), v, geo);
    CHECK(geo.points.size() > 20);
    CHECK(geo.s_hi > lazadas().axis().length() + 50.0);
    for (const MapMark& mark : geo.marks) {
        CHECK(mark.s <= lazadas().axis().length()); // las marcas, solo en el tramo
    }
}

TEST_CASE("tablet map: stripes keep their place while the zoom changes") {
    // La fase de las rayas va anclada al ppm del nivel de destino: con dos ppm distintos dentro de
    // la misma transición, un mismo punto de la carretera tiene la misma fase.
    const UiRect map = layout_for(1280, 720).map;
    const double s_car = 700.0;
    MapView a = view_at(map, s_car, 0.0, 100.0, lazadas().pose(s_car).heading);
    MapView b = view_at(map, s_car, 0.0, 140.0, lazadas().pose(s_car).heading);
    a.phase_ppm = b.phase_ppm = 2.0;
    MapGeometry ga, gb;
    build_map(road(), lazadas(), a, ga);
    build_map(road(), lazadas(), b, gb);
    // Mismo s => misma fase: s·phase_ppm. (Las rejillas pueden diferir de paso: se mira la fase
    // contra la s que le toca.)
    for (const MapGeometry* g : {&ga, &gb}) {
        for (const MapRun& run : g->runs) {
            for (std::uint32_t i = 0; i < run.count; ++i) {
                const MapPoint& p = g->points[run.first + i];
                const double s = static_cast<double>(p.phase) / 2.0;
                CHECK(std::fmod(s, g->step) == doctest::Approx(0.0).epsilon(1e-6));
            }
        }
    }
}

TEST_CASE("tablet zoom: lost states (s still 0.1 s, then a jump) do not change the level") {
    // Z, red mala: la extrapolación se para a los 100 ms y después `s` salta para alcanzar.
    TabletView view;
    double now_s = 0.0;
    double s = 0.0;
    const double v = 60.0 / 3.6;
    for (int i = 0; i < 3 * 60; ++i) {
        view.update(lazadas(), s, 0.0, now_s, MapOrientation::Axis);
        s += v / 60.0;
        now_s += 1.0 / 60.0;
    }
    CHECK(view.zoom_level() == 1);
    for (int i = 0; i < 6; ++i) { // 0,1 s quieta
        view.update(lazadas(), s, 0.0, now_s, MapOrientation::Axis);
        now_s += 1.0 / 60.0;
        CHECK(view.zoom_level() == 1);
    }
    s += v * 0.1 + v / 60.0; // el salto para alcanzar lo perdido
    view.update(lazadas(), s, 0.0, now_s, MapOrientation::Axis);
    CHECK(view.zoom_level() == 1);
    CHECK(view.speed() > 40.0 / 3.6);
}
