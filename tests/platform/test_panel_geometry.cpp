// Panel de notas del salpicadero (lote 7, tarea panel): dónde cae en la imagen con el asiento en
// cada extremo de su ajuste, que el volante recto no lo tape, el tamaño de cada hueco y que la
// geometría de cada nota lleve su número o letra, su flecha y sus iconos. Sin ventana.

#include "io/recording.h"
#include "png_dump.h"
#include "platform/cockpit.h"
#include "platform/panel_geometry.h"
#include "platform/seat.h"
#include "sim/car_data.h"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace rally;
using namespace rally::platform;
using game::NoteGrade;
using game::NoteMark;
using game::NoteSide;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFovY = 55.0 * kPi / 180.0; // decisión cerrada: 55° vertical fijos
constexpr double kAspect = 16.0 / 9.0;
constexpr int kWidthPx = 1280;
constexpr int kHeightPx = 720;
// Mínimo que fijo para que el número se lea a 1280×720: un hueco de 48 px de alto, de modo que el
// número (el 60 % del hueco) mida 28 px, la altura de las cifras de la pantalla de la marcha
// (31 px) menos un margen.
constexpr double kMinSlotPx = 48.0;

const sim::CarData& car_205() {
    static const sim::CarData car = [] {
        std::string text;
        std::string error;
        REQUIRE(io::read_file(RALLY_SOURCE_DIR "/data/cars/mid_awd_turbo.car", text, error));
        return sim::load_car_or_abort(text, "mid_awd_turbo", sim::CarKeys::Full);
    }();
    return car;
}

struct Seat {
    const char* name;
    double forward;
    double right;
    double up;
};
// El ojo nominal y los extremos del ajuste del asiento (±15 cm en cada eje).
constexpr std::array<Seat, 7> kSeats = {{
    {"nominal", 0.0, 0.0, 0.0},
    {"adelante", 0.15, 0.0, 0.0},
    {"atrás", -0.15, 0.0, 0.0},
    {"izquierda", 0.0, -0.15, 0.0},
    {"derecha", 0.0, 0.15, 0.0},
    {"abajo", 0.0, 0.0, -0.15},
    {"arriba", 0.0, 0.0, 0.15},
}};

struct Px {
    double x;
    double y;
    bool ahead;
};

// Un punto en ejes del coche, visto desde el ojo nominal desplazado por el asiento.
Px project(const Vec3d& p, const Seat& seat) {
    const Vec3d eye = car_eye(car_205());
    const double ahead = (eye.z - p.z) - seat.forward;
    const double right = (p.x - eye.x) - seat.right;
    const double up = (p.y - eye.y) - seat.up;
    if (ahead <= 0.01) {
        return {0.0, 0.0, false};
    }
    const ScreenFraction f = project_from_eye(ahead, right, up, kFovY, kAspect);
    return {f.x * kWidthPx, f.y * kHeightPx, true};
}

bool inside_screen(const Px& p) {
    return p.ahead && p.x >= 0.0 && p.x <= kWidthPx && p.y >= 0.0 && p.y <= kHeightPx;
}

std::array<Px, 4> panel_corners(const NotePanelFrame& f, const Seat& seat) {
    const double hw = 0.5 * f.width;
    const double hh = 0.5 * f.height;
    return {project(f.at(-hw, -hh), seat), project(f.at(hw, -hh), seat), project(f.at(hw, hh), seat),
            project(f.at(-hw, hh), seat)};
}

bool in_triangle(double px, double py, const Px& a, const Px& b, const Px& c) {
    const auto side = [](double ax, double ay, double bx, double by, double x, double y) {
        return (bx - ax) * (y - ay) - (by - ay) * (x - ax);
    };
    const double d1 = side(a.x, a.y, b.x, b.y, px, py);
    const double d2 = side(b.x, b.y, c.x, c.y, px, py);
    const double d3 = side(c.x, c.y, a.x, a.y, px, py);
    const bool neg = d1 < 0.0 || d2 < 0.0 || d3 < 0.0;
    const bool pos = d1 > 0.0 || d2 > 0.0 || d3 > 0.0;
    return !(neg && pos);
}

PanelView sample_view() {
    PanelView v;
    v.left.kind = PanelSlotKind::Corner;
    v.left.note.id = 1;
    v.left.note.grade = NoteGrade::G3;
    v.left.note.side = NoteSide::Left;
    v.left.note.marks = game::mark_bit(NoteMark::Tightens);
    v.left.note.distance = game::counter_distance(80);
    v.center = v.left.note.distance;
    v.has_center = true;
    v.right.kind = PanelSlotKind::Corner;
    v.right.note.id = 2;
    v.right.note.grade = NoteGrade::Hairpin;
    v.right.note.side = NoteSide::Right;
    return v;
}

} // namespace

TEST_CASE("panel geometry: the 205 has a panel between the door and the display") {
    const NotePanelFrame f = note_panel_frame(car_205());
    REQUIRE(f.valid);
    const sim::CockpitData& k = car_205().cockpit;
    MESSAGE("panel: ", f.width * 1000.0, " x ", f.height * 1000.0, " mm, huecos de ",
            (f.slot_edge[1] - f.slot_edge[0]) * 1000.0, " | ",
            (f.slot_edge[2] - f.slot_edge[1]) * 1000.0, " | ",
            (f.slot_edge[3] - f.slot_edge[2]) * 1000.0, " mm");
    const Vec3d eye = car_eye(car_205());
    // Su borde izquierdo no pasa de la puerta y el derecho queda a la izquierda de la pantalla.
    const double left_edge = f.center.x - 0.5 * f.width - eye.x;
    const double right_edge = f.center.x + 0.5 * f.width - eye.x;
    CHECK(left_edge >= -static_cast<double>(k.door_left));
    CHECK(right_edge <= k.display_right - 0.5 * k.display_width);
    CHECK(f.width == doctest::Approx(0.215).epsilon(0.01));
    // Un coche sin interior (el de referencia de los tests) no lleva panel.
    const sim::CarData none = {};
    CHECK_FALSE(note_panel_frame(none).valid);
    std::vector<CockpitTri> tris;
    append_note_panel(tris, none, sample_view());
    CHECK(tris.empty());
}

TEST_CASE("panel geometry: the panel is inside the 16:9 screen from the nominal eye and the seat "
          "extremes") {
    const NotePanelFrame f = note_panel_frame(car_205());
    REQUIRE(f.valid);
    for (const Seat& seat : kSeats) {
        const auto c = panel_corners(f, seat);
        bool all_inside = true;
        double min_y = 1e9;
        double max_y = -1e9;
        for (const Px& p : c) {
            all_inside = all_inside && inside_screen(p);
            min_y = std::min(min_y, p.y);
            max_y = std::max(max_y, p.y);
        }
        MESSAGE("asiento ", seat.name, ": panel de y ", min_y, " a ", max_y, " px de ", kHeightPx,
                all_inside ? " (entero)" : " (SALE de la imagen)");
        if (std::string(seat.name) == "arriba") {
            // Con el asiento 15 cm arriba el salpicadero entero baja por debajo del borde (−27,5°):
            // la pantalla de la marcha también. Se mide y se informa; no se puede cumplir sin
            // subir el panel sobre el salpicadero.
            const Px display = project(
                cockpit_point(car_205(), car_205().cockpit.display_ahead,
                              car_205().cockpit.display_right,
                              -static_cast<double>(car_205().cockpit.display_below)),
                seat);
            CHECK_FALSE(inside_screen(display));
            continue;
        }
        CHECK(all_inside);
    }
}

TEST_CASE("panel geometry: the straight steering wheel does not cover the panel") {
    const NotePanelFrame f = note_panel_frame(car_205());
    REQUIRE(f.valid);
    std::vector<CockpitTri> wheel;
    append_steering_wheel(wheel, car_205(), 0.0);
    for (const Seat& seat : kSeats) {
        if (std::string(seat.name) == "arriba") {
            continue; // el panel ya no está en la imagen
        }
        std::vector<std::array<Px, 3>> projected;
        for (const CockpitTri& t : wheel) {
            const auto v = [&](const Vec3f& p) {
                return project({p.x, p.y, p.z}, seat);
            };
            const std::array<Px, 3> px = {v(t.a), v(t.b), v(t.c)};
            if (px[0].ahead && px[1].ahead && px[2].ahead) {
                projected.push_back(px);
            }
        }
        int covered = 0;
        constexpr int kN = 24;
        for (int i = 0; i <= kN; ++i) {
            for (int j = 0; j <= kN; ++j) {
                const Px p = project(f.at((-0.5 + static_cast<double>(i) / kN) * f.width,
                                          (-0.5 + static_cast<double>(j) / kN) * f.height),
                                     seat);
                for (const auto& tri : projected) {
                    if (in_triangle(p.x, p.y, tri[0], tri[1], tri[2])) {
                        ++covered;
                        break;
                    }
                }
            }
        }
        MESSAGE("asiento ", seat.name, ": puntos del panel tapados por el volante: ", covered);
        CHECK(covered == 0);
    }
}

TEST_CASE("panel geometry: every slot is large enough to read the number at 1280x720") {
    const NotePanelFrame f = note_panel_frame(car_205());
    REQUIRE(f.valid);
    const double hh = 0.5 * f.height;
    for (const Seat& seat : kSeats) {
        if (std::string(seat.name) == "arriba") {
            continue;
        }
        double smallest = 1e9;
        for (std::size_t s = 0; s < 3; ++s) {
            const Px bl = project(f.at(f.slot_edge[s], -hh), seat);
            const Px br = project(f.at(f.slot_edge[s + 1], -hh), seat);
            const Px tl = project(f.at(f.slot_edge[s], hh), seat);
            const double w = std::hypot(br.x - bl.x, br.y - bl.y);
            const double h = std::hypot(tl.x - bl.x, tl.y - bl.y);
            smallest = std::min({smallest, w, h});
            if (s != 1) {
                // El hueco de una curva: el número, con su alto en el plano, mide su parte.
                CHECK(w >= kMinSlotPx);
            }
            CHECK(h >= kMinSlotPx);
        }
        MESSAGE("asiento ", seat.name, ": el lado menor de un hueco mide ", smallest, " px");
    }
    // Alto del hueco desde el ojo nominal, el dato del informe.
    const Px bl = project(f.at(0.0, -hh), kSeats[0]);
    const Px tl = project(f.at(0.0, hh), kSeats[0]);
    MESSAGE("nominal: alto del hueco ", std::hypot(tl.x - bl.x, tl.y - bl.y), " px");
}

TEST_CASE("panel geometry: grade colours follow the scale and the arrow bends more the tighter the "
          "curve") {
    const std::array<NoteGrade, 7> order = {NoteGrade::G6, NoteGrade::G5, NoteGrade::G4,
                                            NoteGrade::G3, NoteGrade::G2, NoteGrade::G1,
                                            NoteGrade::Hairpin};
    for (std::size_t i = 0; i < order.size(); ++i) {
        for (std::size_t j = i + 1; j < order.size(); ++j) {
            const CockpitColor a = grade_color(order[i]);
            const CockpitColor b = grade_color(order[j]);
            CHECK((a.r != b.r || a.g != b.g || a.b != b.b));
        }
        if (i > 0) {
            CHECK(arrow_turn(order[i]) > arrow_turn(order[i - 1]));
        }
    }
    // docs/01 §5.1: 6 azul, 5 cian, 4 verde, 3 amarillo, 2 naranja, 1 rojo, H morado.
    const auto dominant = [](CockpitColor c) {
        return c.r >= c.g && c.r >= c.b ? 'r' : (c.g >= c.b ? 'g' : 'b');
    };
    CHECK(dominant(grade_color(NoteGrade::G6)) == 'b');
    CHECK(dominant(grade_color(NoteGrade::G4)) == 'g');
    CHECK(dominant(grade_color(NoteGrade::G1)) == 'r');
    CHECK(dominant(grade_color(NoteGrade::G2)) == 'r');
    CHECK(grade_color(NoteGrade::Hairpin).b > grade_color(NoteGrade::Hairpin).g);
    CHECK(grade_color(NoteGrade::Hairpin).r > grade_color(NoteGrade::Hairpin).g);
    // La forma: el giro del final del eje es el del grado, a la derecha o a la izquierda.
    const game::NoteSide sides[2] = {NoteSide::Left, NoteSide::Right};
    for (const NoteSide side : sides) {
        double previous = -1.0;
        for (const NoteGrade g : order) {
            const ArrowShape a = arrow_shape(g, side);
            CHECK(a.end_heading == doctest::Approx(arrow_turn(g)));
            const double dx = a.axis[kArrowSegments][0] - a.axis[kArrowSegments - 1][0];
            const double dy = a.axis[kArrowSegments][1] - a.axis[kArrowSegments - 1][1];
            const double heading = std::atan2(std::abs(dx), dy);
            CHECK(heading > previous);
            previous = heading;
            // Hacia su lado.
            CHECK((side == NoteSide::Left ? a.axis[kArrowSegments][0] <= 0.0
                                           : a.axis[kArrowSegments][0] >= 0.0));
        }
    }
    CHECK(arrow_turn(NoteGrade::Hairpin) > 0.9 * kPi);
}

TEST_CASE("panel geometry: every note has its number or letter, arrow and icons inside its slot") {
    const NotePanelFrame f = note_panel_frame(car_205());
    REQUIRE(f.valid);
    std::vector<CockpitTri> empty;
    append_note_panel(empty, car_205(), PanelView{});
    const std::size_t board = empty.size(); // fondo y huecos
    CHECK(board > 0);
    const auto plane_xy = [&](const Vec3f& p, double& x, double& y) {
        const double dx = p.x - f.center.x;
        const double dy = p.y - f.center.y;
        const double dz = p.z - f.center.z;
        x = dx * f.u.x + dy * f.u.y + dz * f.u.z;
        y = dx * f.v.x + dy * f.v.y + dz * f.v.z;
    };
    constexpr double kTol = 0.0015; // el desvío en z de las capas, proyectado
    const NoteGrade grades[] = {NoteGrade::G1, NoteGrade::G2, NoteGrade::G3, NoteGrade::G4,
                                NoteGrade::G5, NoteGrade::G6, NoteGrade::Hairpin};
    for (const NoteGrade grade : grades) {
        for (const NoteSide side : {NoteSide::Left, NoteSide::Right}) {
            PanelView v;
            v.left.kind = PanelSlotKind::Corner;
            v.left.note.id = 1;
            v.left.note.grade = grade;
            v.left.note.side = side;
            for (int m = 0; m < game::kNoteMarkCount; ++m) {
                v.left.note.marks |= game::mark_bit(static_cast<NoteMark>(m));
            }
            std::vector<CockpitTri> tris;
            append_note_panel(tris, car_205(), v);
            REQUIRE(tris.size() > board + 20);
            for (std::size_t i = board; i < tris.size(); ++i) {
                for (const Vec3f* p : {&tris[i].a, &tris[i].b, &tris[i].c}) {
                    double x = 0.0;
                    double y = 0.0;
                    plane_xy(*p, x, y);
                    CHECK(x >= f.slot_edge[0] - kTol);
                    CHECK(x <= f.slot_edge[1] + kTol);
                    CHECK(std::abs(y) <= 0.5 * f.height + kTol);
                }
            }
        }
    }
    // Un peligro sin curva, cada uno con su icono, dentro de su hueco.
    for (const NoteMark hazard : {NoteMark::Crest, NoteMark::Jump, NoteMark::Caution}) {
        PanelView v;
        v.right.kind = PanelSlotKind::Hazard;
        v.right.note.id = 3;
        v.right.note.marks = game::mark_bit(hazard);
        std::vector<CockpitTri> tris;
        append_note_panel(tris, car_205(), v);
        CHECK(tris.size() > board + 2);
        for (std::size_t i = board; i < tris.size(); ++i) {
            double x = 0.0;
            double y = 0.0;
            plane_xy(tris[i].a, x, y);
            CHECK(x >= f.slot_edge[2] - kTol);
            CHECK(x <= f.slot_edge[3] + kTol);
        }
    }
    // El número de cada grado es distinto, y la H no es ningún número.
    CHECK(segment_mask('H') != 0);
    for (char a = '0'; a <= '9'; ++a) {
        CHECK(segment_mask(a) != 0);
        for (char b = static_cast<char>(a + 1); b <= '9'; ++b) {
            CHECK(segment_mask(a) != segment_mask(b));
        }
        CHECK(segment_mask('H') != segment_mask(a));
    }
    // Tachada añade la barra; la distancia añade sus cifras.
    PanelView v = sample_view();
    std::vector<CockpitTri> plain;
    append_note_panel(plain, car_205(), v);
    v.left.struck = true;
    std::vector<CockpitTri> struck;
    append_note_panel(struck, car_205(), v);
    CHECK(struck.size() == plain.size() + 2);
    PanelView no_center = sample_view();
    no_center.has_center = false;
    std::vector<CockpitTri> without;
    append_note_panel(without, car_205(), no_center);
    CHECK(without.size() < plain.size());
}

TEST_CASE("panel geometry: cost of the panel and captures for the report") {
    using Clock = std::chrono::steady_clock;
    const PanelView v = sample_view();
    std::vector<CockpitTri> tris;
    tris.reserve(4096);
    double with_ns = 0.0;
    constexpr int kRuns = 2000;
    for (int i = 0; i < kRuns; ++i) {
        tris.clear();
        const auto t0 = Clock::now();
        append_note_panel(tris, car_205(), v);
        with_ns += std::chrono::duration<double, std::nano>(Clock::now() - t0).count();
    }
    MESSAGE("append_note_panel con una nota, su distancia y otra: ", with_ns / kRuns / 1000.0,
            " us, ", tris.size(), " triángulos");
    CHECK(tris.size() > 0);

    const char* dir = std::getenv("RALLY_PANEL_CAPTURES");
    if (dir == nullptr || dir[0] == '\0') {
        return;
    }
    // Capturas sin ventana: el interior desde el ojo nominal a 1280×720, con el panel ampliado.
    struct Scene {
        const char* name;
        PanelView view;
    };
    std::vector<Scene> scenes;
    {
        PanelView a;
        a.left.kind = PanelSlotKind::Corner;
        a.left.note.id = 1;
        a.left.note.grade = NoteGrade::G4;
        a.left.note.side = NoteSide::Right;
        scenes.push_back({"panel_1_nota", a});
        PanelView b = sample_view();
        scenes.push_back({"panel_2_nota_y_distancia", b});
        PanelView c;
        c.left.kind = PanelSlotKind::Hazard;
        c.left.note.id = 4;
        c.left.note.marks = game::mark_bit(NoteMark::Jump);
        c.right.kind = PanelSlotKind::Corner;
        c.right.note.id = 5;
        c.right.note.grade = NoteGrade::G2;
        c.right.note.side = NoteSide::Left;
        c.right.note.marks = game::mark_bit(NoteMark::Opens) | game::mark_bit(NoteMark::Crest);
        scenes.push_back({"panel_3_peligro", c});
        PanelView d = sample_view();
        d.left.struck = true;
        d.has_center = false;
        scenes.push_back({"panel_4_tachada", d});
        PanelView e;
        scenes.push_back({"panel_0_vacio", e});
    }
    for (const Scene& scene : scenes) {
        tests::RgbImage img(kWidthPx, kHeightPx, {86, 108, 74});
        std::vector<CockpitTri> all = build_cockpit(car_205());
        append_steering_wheel(all, car_205(), 0.0);
        CockpitDisplay display;
        display.gear = 3;
        display.rpm_fraction = 0.5f;
        display.red_fraction = 0.85f;
        display.boost_fraction = 0.4f;
        append_display(all, car_205(), display);
        append_note_panel(all, car_205(), scene.view);
        const Vec3d eye = car_eye(car_205());
        const auto sv = [&](const Vec3f& p) {
            const double ahead = eye.z - p.z;
            const double right = p.x - eye.x;
            const double up = p.y - eye.y;
            if (ahead < 0.02) {
                return tests::ScreenVertex{-1e6, -1e6, 1e-6};
            }
            const ScreenFraction fr = project_from_eye(ahead, right, up, kFovY, kAspect);
            return tests::ScreenVertex{fr.x * kWidthPx, fr.y * kHeightPx, 1.0 / ahead};
        };
        for (const CockpitTri& t : all) {
            tests::fill_triangle(img, sv(t.a), sv(t.b), sv(t.c), {t.color.r, t.color.g, t.color.b});
        }
        const NotePanelFrame f = note_panel_frame(car_205());
        const auto corners = panel_corners(f, kSeats[0]);
        const int x0 = static_cast<int>(corners[0].x) - 40;
        const int x1 = static_cast<int>(corners[1].x) + 40;
        const int y0 = static_cast<int>(corners[2].y) - 30;
        const int y1 = static_cast<int>(corners[0].y) + 30;
        const std::string base = std::string(dir) + "/" + scene.name;
        tests::write_png(base + "_ampliada.png", tests::crop_scaled(img, x0, y0, x1, y1, 4));
        tests::write_png(base + "_pantalla.png", img);
    }
}
