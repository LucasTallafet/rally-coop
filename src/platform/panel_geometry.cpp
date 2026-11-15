#include "platform/panel_geometry.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace rally::platform {

namespace {

constexpr double kPi = 3.14159265358979323846;

constexpr CockpitColor kBoard{12, 13, 15};
constexpr CockpitColor kSlotBack{24, 26, 30};
constexpr CockpitColor kDistance{236, 236, 230};
constexpr CockpitColor kModifier{225, 225, 222};
constexpr CockpitColor kHazard{255, 196, 40};
constexpr CockpitColor kHazardMark{20, 16, 8};
constexpr CockpitColor kStrike{255, 70, 60};

// Medidas de un hueco (m en el plano del panel).
constexpr double kMargin = 0.004;
constexpr double kStripHeight = 0.015; // fila de los iconos de las marcas
constexpr double kIconCell = 0.0125;   // ancho máximo de un icono de la fila
constexpr double kDigitWidth = 0.021;  // número de la curva
constexpr double kDistanceHeight = 0.036;
constexpr double kDistanceCell = 0.0195;

Vec3f to_f(const Vec3d& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

CockpitColor dim(CockpitColor c, double f) {
    return {static_cast<std::uint8_t>(c.r * f), static_cast<std::uint8_t>(c.g * f),
            static_cast<std::uint8_t>(c.b * f)};
}

// Un plano del panel con un desvío en z por capa (1 mm por capa: lo más lejano, el fondo; lo
// más cercano, la tachadura), como la pantalla de la marcha.
struct Layer {
    const NotePanelFrame& frame;
    double dz;

    Vec3d at(double x, double y) const {
        Vec3d p = frame.at(x, y);
        p.z += dz;
        return p;
    }
    void tri(std::vector<CockpitTri>& out, std::array<double, 2> a, std::array<double, 2> b,
             std::array<double, 2> c, CockpitColor color) const {
        out.push_back({to_f(at(a[0], a[1])), to_f(at(b[0], b[1])), to_f(at(c[0], c[1])), color});
    }
    void quad(std::vector<CockpitTri>& out, std::array<double, 2> a, std::array<double, 2> b,
              std::array<double, 2> c, std::array<double, 2> d, CockpitColor color) const {
        tri(out, a, b, c, color);
        tri(out, a, c, d, color);
    }
    void rect(std::vector<CockpitTri>& out, double x0, double y0, double x1, double y1,
              CockpitColor color) const {
        quad(out, {x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, color);
    }
};

// Carácter de 7 segmentos en la caja (x, y) de w × h; el origen es la esquina de abajo a la
// izquierda.
void glyph(std::vector<CockpitTri>& out, const Layer& layer, char c, double x, double y, double w,
           double h, CockpitColor color) {
    const unsigned mask = segment_mask(c);
    if (c == '+') {
        const double cx = x + 0.5 * w;
        const double cy = y + 0.5 * h;
        const double t = 0.16 * w;
        const double r = 0.4 * w;
        layer.rect(out, cx - r, cy - 0.5 * t, cx + r, cy + 0.5 * t, color);
        layer.rect(out, cx - 0.5 * t, cy - r, cx + 0.5 * t, cy + r, color);
        return;
    }
    const double t = std::min(0.2 * w, 0.14 * h);
    struct Seg {
        double x0, y0, x1, y1;
    };
    const std::array<Seg, 7> segments = {{
        {x, y + h - t, x + w, y + h},                             // a
        {x + w - t, y + 0.5 * h, x + w, y + h},                   // b
        {x + w - t, y, x + w, y + 0.5 * h},                       // c
        {x, y, x + w, y + t},                                     // d
        {x, y, x + t, y + 0.5 * h},                               // e
        {x, y + 0.5 * h, x + t, y + h},                           // f
        {x, y + 0.5 * h - 0.5 * t, x + w, y + 0.5 * h + 0.5 * t}, // g
    }};
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if ((mask >> i & 1u) != 0) {
            const Seg& s = segments[i];
            layer.rect(out, s.x0, s.y0, s.x1, s.y1, color);
        }
    }
}

// Una marca (modificador o peligro) como icono en la caja de centro (cx, cy) y lado `size`.
void mark_icon(std::vector<CockpitTri>& out, const Layer& base, const Layer& front,
               game::NoteMark mark, double cx, double cy, double size, double dim_factor) {
    const auto p = [&](double x, double y) {
        return std::array<double, 2>{cx + x * size, cy + y * size};
    };
    const CockpitColor modifier = dim(kModifier, dim_factor);
    const CockpitColor hazard = dim(kHazard, dim_factor);
    switch (mark) {
    case game::NoteMark::Tightens: // ▶◀
        base.tri(out, p(-0.5, -0.35), p(-0.5, 0.35), p(-0.04, 0.0), modifier);
        base.tri(out, p(0.5, -0.35), p(0.04, 0.0), p(0.5, 0.35), modifier);
        break;
    case game::NoteMark::Opens: // ◀▶
        base.tri(out, p(-0.04, -0.35), p(-0.04, 0.35), p(-0.5, 0.0), modifier);
        base.tri(out, p(0.04, -0.35), p(0.5, 0.0), p(0.04, 0.35), modifier);
        break;
    case game::NoteMark::DontCut: // X
        base.quad(out, p(-0.5, -0.38), p(-0.34, -0.5), p(0.5, 0.38), p(0.34, 0.5), modifier);
        base.quad(out, p(-0.5, 0.38), p(-0.34, 0.5), p(0.5, -0.38), p(0.34, -0.5), modifier);
        break;
    case game::NoteMark::Long: // barra con dos topes
        base.quad(out, p(-0.5, -0.1), p(0.5, -0.1), p(0.5, 0.1), p(-0.5, 0.1), modifier);
        base.quad(out, p(-0.5, -0.35), p(-0.38, -0.35), p(-0.38, 0.35), p(-0.5, 0.35), modifier);
        base.quad(out, p(0.38, -0.35), p(0.5, -0.35), p(0.5, 0.35), p(0.38, 0.35), modifier);
        break;
    case game::NoteMark::Crest: { // bulto: medio disco sobre una base
        constexpr int kFan = 8;
        for (int i = 0; i < kFan; ++i) {
            const double a0 = kPi * i / kFan;
            const double a1 = kPi * (i + 1) / kFan;
            base.tri(out, p(0.0, -0.35), p(0.5 * std::cos(a0), -0.35 + 0.8 * std::sin(a0)),
                     p(0.5 * std::cos(a1), -0.35 + 0.8 * std::sin(a1)), hazard);
        }
        break;
    }
    case game::NoteMark::Jump: // rampa y un salto
        base.tri(out, p(-0.5, -0.4), p(0.12, -0.4), p(0.12, 0.05), hazard);
        base.quad(out, p(0.22, 0.12), p(0.5, 0.12), p(0.5, 0.4), p(0.22, 0.4), hazard);
        break;
    case game::NoteMark::Caution: // triángulo con «!»
        base.tri(out, p(-0.5, -0.42), p(0.5, -0.42), p(0.0, 0.5), hazard);
        front.quad(out, p(-0.06, -0.08), p(0.06, -0.08), p(0.06, 0.24), p(-0.06, 0.24),
                   dim(kHazardMark, 1.0));
        front.quad(out, p(-0.06, -0.3), p(0.06, -0.3), p(0.06, -0.17), p(-0.06, -0.17),
                   dim(kHazardMark, 1.0));
        break;
    }
}

// El peligro mayor de una nota sin curva: el que se enseña grande.
bool main_hazard(const game::Note& note, game::NoteMark& out_mark) {
    for (const game::NoteMark m :
         {game::NoteMark::Caution, game::NoteMark::Jump, game::NoteMark::Crest}) {
        if ((note.marks & game::mark_bit(m)) != 0) {
            out_mark = m;
            return true;
        }
    }
    return false;
}

void draw_slot(std::vector<CockpitTri>& out, const NotePanelFrame& f, const Layer& content,
               const Layer& front, const PanelSlot& slot, double x0, double x1) {
    if (slot.kind == PanelSlotKind::Empty) {
        return;
    }
    const double dim_factor = slot.struck ? 0.4 : 1.0;
    const game::Note& n = slot.note;
    const double bottom = -0.5 * f.height + kMargin;
    const double strip_top = bottom + kStripHeight;
    const double main_y0 = strip_top + 0.002;
    const double main_y1 = 0.5 * f.height - kMargin;
    const double main_h = main_y1 - main_y0;
    game::NoteMark hazard_main = game::NoteMark::Caution;
    bool big_hazard = false;
    if (slot.kind == PanelSlotKind::Hazard) {
        big_hazard = main_hazard(n, hazard_main);
    } else {
        const CockpitColor color = dim(grade_color(n.grade), dim_factor);
        const char c = n.grade == game::NoteGrade::Hairpin
                           ? 'H'
                           : static_cast<char>('0' + static_cast<int>(n.grade));
        glyph(out, content, c, x0 + kMargin, main_y0, kDigitWidth, main_h, color);
        // La flecha, en lo que queda del hueco, ajustada a su caja.
        const ArrowShape a = arrow_shape(n.grade, n.side);
        const double bx0 = x0 + kMargin + kDigitWidth + 0.003;
        const double bx1 = x1 - kMargin;
        double lo_x = a.tip[0];
        double hi_x = a.tip[0];
        double hi_y = a.tip[1];
        double lo_y = a.tip[1];
        for (const auto& pt : a.axis) {
            lo_x = std::min(lo_x, pt[0]);
            hi_x = std::max(hi_x, pt[0]);
            lo_y = std::min(lo_y, pt[1]);
            hi_y = std::max(hi_y, pt[1]);
        }
        constexpr double kHeadHalf = 0.2; // unitario: lo que sobresale la cabeza a cada lado
        lo_x -= kHeadHalf;
        hi_x += kHeadHalf;
        const double scale = std::min((bx1 - bx0) / (hi_x - lo_x), main_h / (hi_y - lo_y));
        const double ox = 0.5 * (bx0 + bx1) - 0.5 * (lo_x + hi_x) * scale;
        const double oy = main_y0 - lo_y * scale;
        const auto map = [&](double x, double y) {
            return std::array<double, 2>{ox + x * scale, oy + y * scale};
        };
        // Eje: tiras con el grosor medido en el plano.
        const double half = 0.07; // unitario
        for (int i = 0; i < kArrowSegments; ++i) {
            const double phi0 = a.end_heading * i / kArrowSegments;
            const double phi1 = a.end_heading * (i + 1) / kArrowSegments;
            const double sgn = n.side == game::NoteSide::Left ? -1.0 : 1.0;
            const auto normal = [&](double phi) {
                return std::array<double, 2>{std::cos(phi) * half, -sgn * std::sin(phi) * half};
            };
            const auto& p0 = a.axis[static_cast<std::size_t>(i)];
            const auto& p1 = a.axis[static_cast<std::size_t>(i) + 1];
            const auto n0 = normal(phi0);
            const auto n1 = normal(phi1);
            content.quad(out, map(p0[0] - n0[0], p0[1] - n0[1]), map(p0[0] + n0[0], p0[1] + n0[1]),
                         map(p1[0] + n1[0], p1[1] + n1[1]), map(p1[0] - n1[0], p1[1] - n1[1]),
                         color);
        }
        // Cabeza: triángulo de la punta, abierto hacia atrás.
        const double sgn = n.side == game::NoteSide::Left ? -1.0 : 1.0;
        const auto& last = a.axis[kArrowSegments];
        const double hx = sgn * std::sin(a.end_heading); // dirección de la punta
        const double hy = std::cos(a.end_heading);
        const double nx = hy;
        const double ny = -hx;
        constexpr double kHeadBack = 0.0;
        content.tri(out, map(last[0] + nx * kHeadHalf + hx * kHeadBack, last[1] + ny * kHeadHalf),
                    map(last[0] - nx * kHeadHalf, last[1] - ny * kHeadHalf),
                    map(a.tip[0], a.tip[1]), color);
    }
    if (big_hazard) {
        const double size = std::min(x1 - x0 - 2.0 * kMargin, main_h);
        mark_icon(out, content, front, hazard_main, 0.5 * (x0 + x1), 0.5 * (main_y0 + main_y1),
                  size, dim_factor);
    }
    // Fila de marcas (todas menos la grande de un peligro).
    int count = 0;
    std::array<game::NoteMark, game::kNoteMarkCount> marks{};
    for (int m = 0; m < game::kNoteMarkCount; ++m) {
        const auto mark = static_cast<game::NoteMark>(m);
        if ((n.marks & game::mark_bit(mark)) != 0 && !(big_hazard && mark == hazard_main)) {
            marks[static_cast<std::size_t>(count++)] = mark;
        }
    }
    if (count > 0) {
        const double cell = std::min(kIconCell, (x1 - x0 - 2.0 * kMargin) / count);
        const double size = 0.8 * std::min(cell, kStripHeight);
        const double start = 0.5 * (x0 + x1) - 0.5 * cell * count;
        for (int i = 0; i < count; ++i) {
            mark_icon(out, content, front, marks[static_cast<std::size_t>(i)],
                      start + cell * (i + 0.5), bottom + 0.5 * kStripHeight, size, dim_factor);
        }
    }
    if (slot.struck) {
        // Tachada: una barra roja de lado a lado.
        front.quad(out, {x0 + 0.002, -0.006}, {x1 - 0.002, 0.004}, {x1 - 0.002, 0.010},
                   {x0 + 0.002, 0.0}, kStrike);
    }
}

} // namespace

NotePanelFrame note_panel_frame(const sim::CarData& car) {
    const sim::CockpitData& k = car.cockpit;
    NotePanelFrame f;
    if (k.display_width <= 0.0f || k.display_height <= 0.0f || k.display_ahead <= 0.0f) {
        return f;
    }
    const double left = -static_cast<double>(k.door_left) + kPanelDoorGap;
    const double right = static_cast<double>(k.display_right) - 0.5 * k.display_width - kPanelGap;
    f.width = right - left;
    if (f.width < kPanelMinWidth) {
        return f;
    }
    f.height = kPanelHeightScale * k.display_height;
    // De cara al ojo, como la pantalla de la marcha: el plano se inclina lo que baja la línea del
    // ojo a su centro.
    const double below = static_cast<double>(k.display_below) - kPanelRaise;
    const double tilt = std::atan2(below, static_cast<double>(k.display_ahead));
    f.center = cockpit_point(car, k.display_ahead, 0.5 * (left + right), -below);
    f.u = {1.0, 0.0, 0.0};
    f.v = {0.0, std::cos(tilt), -std::sin(tilt)};
    double x = -0.5 * f.width;
    f.slot_edge[0] = x;
    for (std::size_t i = 0; i < 3; ++i) {
        x += kPanelSlotShare[i] * f.width;
        f.slot_edge[i + 1] = x;
    }
    f.valid = true;
    return f;
}

void append_note_panel(std::vector<CockpitTri>& out, const sim::CarData& car,
                       const PanelView& view) {
    const NotePanelFrame f = note_panel_frame(car);
    if (!f.valid) {
        return;
    }
    const Layer board{f, -0.003};
    const Layer back{f, -0.002};
    const Layer content{f, -0.001};
    const Layer front{f, 0.0};
    const double hw = 0.5 * f.width;
    const double hh = 0.5 * f.height;
    board.rect(out, -hw, -hh, hw, hh, kBoard);
    // El pie: del salpicadero al panel, para que no flote (la base del parabrisas va a 0,30 m por
    // debajo del ojo; el panel está más arriba).
    {
        const sim::CockpitData& k = car.cockpit;
        const Vec3d top = f.at(0.0, -hh);
        const Vec3d foot =
            cockpit_point(car, k.display_ahead, 0.0, -static_cast<double>(k.windshield_base_below));
        constexpr double kPostHalf = 0.012;
        out.push_back({to_f({top.x - kPostHalf, top.y, top.z - 0.004}),
                       to_f({top.x + kPostHalf, top.y, top.z - 0.004}),
                       to_f({foot.x + kPostHalf, foot.y, foot.z}), kBoard});
        out.push_back({to_f({top.x - kPostHalf, top.y, top.z - 0.004}),
                       to_f({foot.x + kPostHalf, foot.y, foot.z}),
                       to_f({foot.x - kPostHalf, foot.y, foot.z}), kBoard});
    }
    for (std::size_t i = 0; i < 3; ++i) {
        back.rect(out, f.slot_edge[i] + 0.002, -hh + 0.002, f.slot_edge[i + 1] - 0.002, hh - 0.002,
                  kSlotBack);
    }
    draw_slot(out, f, content, front, view.left, f.slot_edge[0], f.slot_edge[1]);
    draw_slot(out, f, content, front, view.right, f.slot_edge[2], f.slot_edge[3]);
    if (view.has_center) {
        const std::string text = game::distance_text(view.center);
        const double x0 = f.slot_edge[1];
        const double x1 = f.slot_edge[2];
        const int n = static_cast<int>(text.size());
        const double cell = std::min(kDistanceCell, (x1 - x0 - 2.0 * kMargin) / std::max(n, 1));
        const double start = 0.5 * (x0 + x1) - 0.5 * cell * n;
        for (int i = 0; i < n; ++i) {
            glyph(out, content, text[static_cast<std::size_t>(i)], start + cell * i + 0.15 * cell,
                  -0.5 * kDistanceHeight, 0.7 * cell, kDistanceHeight, kDistance);
        }
    }
}

CockpitColor grade_color(game::NoteGrade grade) {
    switch (grade) {
    case game::NoteGrade::G6:
        return {60, 120, 255}; // azul
    case game::NoteGrade::G5:
        return {40, 215, 230}; // cian
    case game::NoteGrade::G4:
        return {70, 205, 80}; // verde
    case game::NoteGrade::G3:
        return {245, 215, 50}; // amarillo
    case game::NoteGrade::G2:
        return {255, 140, 30}; // naranja
    case game::NoteGrade::G1:
        return {235, 55, 50}; // rojo
    case game::NoteGrade::Hairpin:
        return {170, 80, 230}; // morado
    case game::NoteGrade::None:
        break;
    }
    return kHazard;
}

double arrow_turn(game::NoteGrade grade) {
    constexpr double kDegrees = kPi / 180.0;
    switch (grade) {
    case game::NoteGrade::G6:
        return 12.0 * kDegrees;
    case game::NoteGrade::G5:
        return 30.0 * kDegrees;
    case game::NoteGrade::G4:
        return 50.0 * kDegrees;
    case game::NoteGrade::G3:
        return 72.0 * kDegrees;
    case game::NoteGrade::G2:
        return 96.0 * kDegrees;
    case game::NoteGrade::G1:
        return 120.0 * kDegrees;
    case game::NoteGrade::Hairpin:
        return 170.0 * kDegrees;
    case game::NoteGrade::None:
        break;
    }
    return 0.0;
}

ArrowShape arrow_shape(game::NoteGrade grade, game::NoteSide side) {
    ArrowShape shape;
    const double turn = arrow_turn(grade);
    const double sgn = side == game::NoteSide::Left ? -1.0 : 1.0;
    shape.end_heading = turn;
    // Arco de longitud 1 y curvatura constante: radio 1/turn (recto si el giro es ~0).
    const double radius = turn > 1e-9 ? 1.0 / turn : 1e9;
    for (int i = 0; i <= kArrowSegments; ++i) {
        const double phi = turn * i / kArrowSegments;
        shape.axis[static_cast<std::size_t>(i)] = {sgn * radius * (1.0 - std::cos(phi)),
                                                   radius * std::sin(phi)};
    }
    // La punta, 0,3 más allá del final, siguiendo la tangente.
    const auto& last = shape.axis[kArrowSegments];
    shape.tip = {last[0] + sgn * std::sin(turn) * 0.3, last[1] + std::cos(turn) * 0.3};
    return shape;
}

unsigned segment_mask(char c) {
    switch (c) {
    case '0':
        return 0x3F;
    case '1':
        return 0x06;
    case '2':
        return 0x5B;
    case '3':
        return 0x4F;
    case '4':
        return 0x66;
    case '5':
        return 0x6D;
    case '6':
        return 0x7D;
    case '7':
        return 0x07;
    case '8':
        return 0x7F;
    case '9':
        return 0x6F;
    case 'H':
        return 0x76;
    case 'y':
        return 0x6E;
    default:
        return 0;
    }
}

} // namespace rally::platform
