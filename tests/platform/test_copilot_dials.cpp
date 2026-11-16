// Los diales del copiloto (lote 7): la rueda, la máquina de los diales, las tres entradas, el
// contador, borrar, el cursor con notas, la geometría y el lado host de las notas. Sin ventana ni
// mando.

#include "game/notes.h"
#include "net/copilot.h"
#include "net/host.h"
#include "net/notes_wire.h"
#include "platform/copilot_dial_input.h"
#include "platform/copilot_dials.h"
#include "platform/copilot_dials_layout.h"
#include "platform/copilot_keys.h"
#include "platform/copilot_wheel.h"
#include "platform/head_look.h"
#include "platform/key_tables.h"
#include "platform/notes_host.h"
#include "platform/screen_layout.h"
#include "platform/tablet_cursor.h"
#include "platform/tablet_view.h"
#include "platform/ui_text.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

using namespace rally::platform;
namespace game = rally::game;
namespace net = rally::net;

namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr float kPi = 3.14159265358979323846f;

// El vector a `deg` desde arriba, en sentido horario, de módulo `mag`.
void vec_at(float deg, float mag, float& x, float& y) {
    x = std::sin(deg * kPi / 180.0f) * mag;
    y = std::cos(deg * kPi / 180.0f) * mag;
}

DialsOutput frame(CopilotDials& d, const DialsInput& in, double anchor = 123.0, bool valid = true,
                  bool attach = false) {
    return d.update(in, kDt, anchor, valid, attach);
}

// Abre un dial con `hold`, elige con el vector y suelta: las órdenes del fotograma de soltar.
DialsOutput hold_pick_release(CopilotDials& d, DialsInput hold, float x, float y,
                              bool attach = false) {
    hold.analog = true;
    hold.x = x;
    hold.y = y;
    for (int i = 0; i < 3; ++i) {
        const DialsOutput o = frame(d, hold, 123.0, true, attach);
        CHECK(o.count == 0);
    }
    return frame(d, DialsInput{}, 123.0, true, attach);
}

bool same(const game::NoteOrder& a, const game::NoteOrder& b) { return a == b; }

} // namespace

TEST_CASE("dials wheel: every position of the three wheels is picked at its own angle") {
    for (int i = 0; i < kMainWheel.count; ++i) {
        for (const float off : {-14.0f, 0.0f, 14.0f}) {
            if ((i == 0 && off < 0.0f) || (i == kMainWheel.count - 1 && off > 0.0f)) {
                continue; // el 6 y la H tienen 15° por su lado
            }
            float x, y;
            vec_at(wheel_center_deg(kMainWheel, i) + off, 0.9f, x, y);
            CHECK(wheel_pick(kMainWheel, x, y, -1) == i);
            CHECK(wheel_pick(kMainWheel, -x, y, -1) == i); // simétrica: el otro lado da lo mismo
        }
    }
    for (int i = 0; i < kRing8Wheel.count; ++i) {
        for (const float off : {-21.0f, 0.0f, 21.0f}) {
            float x, y;
            vec_at(wheel_center_deg(kRing8Wheel, i) + off, 0.9f, x, y);
            CHECK(wheel_pick(kRing8Wheel, x, y, -1) == i);
        }
    }
    CHECK(wheel_pick(kMainWheel, 0.0f, 0.0f, -1) == -1); // el centro cancela
    CHECK(wheel_pick(kRing8Wheel, 0.3f, 0.2f, -1) == -1);
}

TEST_CASE("dials wheel: no flicker with the stick shaking 2 degrees around a boundary") {
    for (const WheelSpec& spec : {kMainWheel, kRing8Wheel}) {
        const int boundaries = spec.shape == WheelShape::Circle ? spec.count : spec.count - 1;
        for (int i = 0; i < boundaries; ++i) {
            const int next = (i + 1) % spec.count;
            const float boundary = wheel_center_deg(spec, i) + 0.5f * spec.step_deg();
            // Desde cada lado de la frontera: se parte de la posición vecina y el ruido de ±2° no
            // la cambia en 100 fotogramas.
            for (const int start : {i, next}) {
                float x, y;
                vec_at(boundary + (start == i ? -10.0f : 10.0f), 0.9f, x, y);
                int current = wheel_pick(spec, x, y, -1);
                REQUIRE(current == start);
                for (int f = 0; f < 100; ++f) {
                    vec_at(boundary + (f % 2 == 0 ? 2.0f : -2.0f), 0.9f, x, y);
                    current = wheel_pick(spec, x, y, current);
                    CHECK(current == start);
                }
            }
            // La histéresis existe: 5° pasada la frontera, sí cambia; 3°, no.
            float x, y;
            vec_at(boundary + 3.0f, 0.9f, x, y);
            CHECK(wheel_pick(spec, x, y, i) == i);
            vec_at(boundary + 5.0f, 0.9f, x, y);
            CHECK(wheel_pick(spec, x, y, i) == next);
        }
    }
}

TEST_CASE("dials wheel: the dead zone has its own hysteresis and the chosen one survives it") {
    float x, y;
    vec_at(90.0f, 0.55f, x, y);
    int current = wheel_pick(kRing8Wheel, x, y, -1);
    REQUIRE(current == 2);
    for (int f = 0; f < 100; ++f) {
        vec_at(90.0f, f % 2 == 0 ? 0.45f : 0.55f, x, y);
        current = wheel_pick(kRing8Wheel, x, y, current);
        CHECK(current == 2); // ni cancela ni cambia
    }
    vec_at(90.0f, 0.35f, x, y);
    CHECK(wheel_pick(kRing8Wheel, x, y, current) == -1);
    // Sin elegir, por debajo de 0,5 no se elige.
    vec_at(90.0f, 0.45f, x, y);
    CHECK(wheel_pick(kRing8Wheel, x, y, -1) == -1);
}

TEST_CASE(
    "dials main: the 14 positions give the right order, centre cancels, no shoulder does nothing") {
    const game::NoteGrade grades[7] = {
        game::NoteGrade::G6, game::NoteGrade::G5, game::NoteGrade::G4,     game::NoteGrade::G3,
        game::NoteGrade::G2, game::NoteGrade::G1, game::NoteGrade::Hairpin};
    for (const bool right : {false, true}) {
        for (int i = 0; i < 7; ++i) {
            CopilotDials d;
            DialsInput hold;
            hold.main_left = !right;
            hold.main_right = right;
            float x, y;
            vec_at(wheel_center_deg(kMainWheel, i), 0.9f, x, y);
            x = right ? std::fabs(x) : -std::fabs(x);
            const DialsOutput out = hold_pick_release(d, hold, x, y);
            REQUIRE(out.count == 1);
            CHECK(same(out.orders[0],
                       game::corner_order(123.0, grades[i],
                                          right ? game::NoteSide::Right : game::NoteSide::Left)));
        }
    }
    {
        CopilotDials d;
        DialsInput hold;
        hold.main_right = true;
        CHECK(hold_pick_release(d, hold, 0.0f, 0.0f).count == 0); // centro: cancela
    }
    {
        CopilotDials d; // sin hombro, el stick no elige nada
        DialsInput in;
        in.analog = true;
        in.x = 0.0f;
        in.y = 1.0f;
        for (int i = 0; i < 20; ++i) {
            CHECK(frame(d, in).count == 0);
        }
        CHECK_FALSE(d.open());
    }
    {
        CopilotDials d; // sin ancla: no sale nota de curva y se dice
        DialsInput hold;
        hold.main_left = true;
        hold.analog = true;
        hold.y = 1.0f;
        frame(d, hold, 0.0, false);
        const DialsOutput out = frame(d, DialsInput{}, 0.0, false);
        CHECK(out.count == 0);
        CHECK(out.no_anchor);
    }
}

TEST_CASE("dials secondary: every position, and a hazard attaches or stands alone") {
    const game::NoteMark marks[7] = {game::NoteMark::Crest,   game::NoteMark::Jump,
                                     game::NoteMark::Caution, game::NoteMark::Tightens,
                                     game::NoteMark::Opens,   game::NoteMark::DontCut,
                                     game::NoteMark::Long};
    for (int i = 0; i < 8; ++i) {
        for (const bool attach : {false, true}) {
            CopilotDials d;
            DialsInput hold;
            hold.secondary = true;
            float x, y;
            vec_at(wheel_center_deg(kRing8Wheel, i), 0.9f, x, y);
            const DialsOutput out = hold_pick_release(d, hold, x, y, attach);
            REQUIRE(out.count == 1);
            if (i == 7) {
                CHECK(same(out.orders[0], game::distance_order(game::wheel_distance(0))));
            } else if (game::is_hazard(marks[i]) && !attach) {
                CHECK(same(out.orders[0], game::hazard_order(123.0, marks[i]))); // sola
            } else {
                CHECK(same(out.orders[0], game::mark_order(marks[i]))); // a la última nota
            }
        }
    }
}

TEST_CASE("dials distance wheel: the 8 values, and the form is written in the order") {
    for (int i = 0; i < 8; ++i) {
        CopilotDials d;
        DialsInput hold;
        hold.distance = true;
        float x, y;
        vec_at(wheel_center_deg(kRing8Wheel, i), 0.9f, x, y);
        const DialsOutput out = hold_pick_release(d, hold, x, y);
        REQUIRE(out.count == 1);
        CHECK(same(out.orders[0], game::distance_order(game::wheel_distance(i))));
        CHECK(out.orders[0].distance.source == game::DistanceSource::Wheel);
    }
    CHECK(game::wheel_distance(0).kind == game::DistanceKind::Link);
    CHECK(game::wheel_distance(7).kind == game::DistanceKind::Over300);
    CHECK(game::wheel_distance(3).metres == 80);
}

TEST_CASE(
    "dials counter: starts at 50, steps of 10 with limits, repeats, sings and goes back to 50") {
    DistanceCounter c;
    CHECK(c.metres() == 50);
    c.update(1, kDt);
    CHECK(c.metres() == 60); // la primera pulsación cuenta al instante
    for (int i = 0; i < 20; ++i) {
        c.update(1, kDt); // 0,33 s: aún en la espera
    }
    CHECK(c.metres() == 60);
    for (int i = 0; i < 12; ++i) {
        c.update(1, kDt); // pasada la espera repite a 8 por segundo
    }
    CHECK(c.metres() > 60);
    c.update(0, kDt);
    c.update(-1, kDt);
    const int after = c.metres();
    CHECK(after % 10 == 0);
    // Topes.
    DistanceCounter top;
    for (int i = 0; i < 6000; ++i) {
        top.update(1, kDt);
    }
    CHECK(top.metres() == 990);
    DistanceCounter bottom;
    for (int i = 0; i < 6000; ++i) {
        bottom.update(-1, kDt);
    }
    CHECK(bottom.metres() == 0);
    CHECK(top.sing() == 990);
    CHECK(top.metres() == 50);
    // Los tiempos que cuenta el informe: de 50 a 300 m, unos 2,5 s.
    DistanceCounter run;
    double t = 0.0;
    while (run.metres() < 300) {
        run.update(1, 0.01);
        t += 0.01;
    }
    CHECK(t == doctest::Approx(2.5).epsilon(0.15));
}

TEST_CASE("dials counter: never reads the car or the direction changes") {
    // Por construcción: ni el contador ni la máquina de los diales reciben la `s` del coche ni los
    // cambios de dirección; solo el ancla del cursor, que elige el copiloto.
    static_assert(
        std::is_same_v<decltype(&DistanceCounter::update), void (DistanceCounter::*)(int, double)>);
    static_assert(std::is_same_v<decltype(&DistanceCounter::sing), int (DistanceCounter::*)()>);
    CopilotDials d;
    DialsInput toggle;
    toggle.toggle_form = true;
    frame(d, toggle);
    CHECK(d.form() == DistanceForm::Counter);
    DialsInput hold;
    hold.distance = true;
    hold.step_held = 1;
    frame(d, hold);
    hold.step_held = 0;
    frame(d, hold);
    hold.step_held = 1;
    frame(d, hold);
    hold.step_held = 0;
    frame(d, hold);
    const DialsOutput out = frame(d, DialsInput{});
    REQUIRE(out.count == 1);
    CHECK(same(out.orders[0], game::distance_order(game::counter_distance(70))));
    CHECK(out.orders[0].distance.source == game::DistanceSource::Counter);
    CHECK(d.view().counter_m == 50); // vuelve a 50
    // Con la forma «contador» las mismas órdenes de la rueda ya no salen de la rueda.
    CHECK(d.view().form == DistanceForm::Counter);
}

TEST_CASE("dials erase: short press and 1 s hold give two different orders, never both") {
    {
        CopilotDials d;
        DialsInput in;
        in.erase_down = true;
        int total = 0;
        for (int i = 0; i < 10; ++i) {
            total += frame(d, in).count; // 0,17 s mantenido
        }
        CHECK(total == 0);
        const DialsOutput out = frame(d, DialsInput{});
        REQUIRE(out.count == 1);
        CHECK(same(out.orders[0], game::erase_last_order()));
    }
    {
        CopilotDials d;
        DialsInput in;
        in.erase_down = true;
        std::vector<game::NoteOrder> orders;
        for (int i = 0; i < 120; ++i) { // 2 s mantenido
            const DialsOutput o = frame(d, in);
            for (int k = 0; k < o.count; ++k) {
                orders.push_back(o.orders[static_cast<std::size_t>(k)]);
            }
        }
        const DialsOutput released = frame(d, DialsInput{}); // soltar tras vaciar: nada más
        CHECK(released.count == 0);
        REQUIRE(orders.size() == 1);
        CHECK(same(orders[0], game::clear_pending_order()));
    }
}

TEST_CASE("dials: while one dial is open the others do not open, and the head is frozen") {
    CopilotDials d;
    HeadLook head;
    DialsInput in;
    in.main_left = true;
    in.analog = true;
    in.y = 1.0f;
    frame(d, in);
    head.set_frozen(d.open());
    CHECK(head.frozen());
    in.secondary = true; // se pulsa otro mientras el principal sigue abierto
    frame(d, in);
    CHECK(d.view().open == DialKind::Main);
    in = DialsInput{};
    const DialsOutput out = frame(d, in);
    head.set_frozen(d.open());
    CHECK_FALSE(head.frozen()); // se suelta al cerrar
    CHECK(out.count == 1);
    // La ayuda cancela sin cantar.
    in.main_left = true;
    in.analog = true;
    in.y = 1.0f;
    frame(d, in);
    d.cancel();
    CHECK_FALSE(d.open());
    CHECK(frame(d, DialsInput{}).count == 0);
}

TEST_CASE("dials input: trigger hysteresis and the first shoulder wins") {
    PadDialMapper m;
    PadState pad;
    pad.active = true;
    pad.throttle = 0.6f;
    CHECK(m.map(pad, DistanceForm::Wheel).secondary);
    for (int i = 0; i < 20; ++i) {
        pad.throttle = i % 2 == 0 ? 0.35f : 0.45f; // ronda el umbral
        CHECK(m.map(pad, DistanceForm::Wheel).secondary);
    }
    pad.throttle = 0.2f;
    CHECK_FALSE(m.map(pad, DistanceForm::Wheel).secondary);
    pad.throttle = 0.4f; // cerrado: abrir exige 0,5
    CHECK_FALSE(m.map(pad, DistanceForm::Wheel).secondary);

    PadDialMapper s;
    PadState sh;
    sh.active = true;
    sh.shoulder_right = true;
    DialsInput a = s.map(sh, DistanceForm::Wheel);
    CHECK(a.main_right);
    sh.shoulder_left = true; // el segundo no hace nada hasta soltar
    a = s.map(sh, DistanceForm::Wheel);
    CHECK(a.main_right);
    CHECK_FALSE(a.main_left);
    sh.shoulder_right = false;
    a = s.map(sh, DistanceForm::Wheel);
    CHECK(a.main_left);
}

TEST_CASE("dials input: the stick sign matches the wheel (up is positive)") {
    PadReading reading;
    reading.id = 1;
    reading.left_y = -1.0f; // SDL: arriba es negativo
    reading.left_trigger = 0.0f;
    PadTracker tracker;
    reading.any_button = true; // activa el mando
    const PadState state = tracker.update(&reading, 1);
    CHECK(state.pick_y == doctest::Approx(1.0f));
}

namespace {

// Una intención de cada tipo con las tres entradas: las órdenes tienen que ser iguales.
struct Rig {
    DialsGeometry g;
    CopilotDials pad_d, mouse_d, key_d;
    PadDialMapper pad_m;
    MouseDialMapper mouse_m;
    std::vector<game::NoteOrder> pad_o, mouse_o, key_o;

    Rig() {
        const ScreenLayout layout = layout_screen(1280, 720, ScreenKind::Copilot);
        g = dials_geometry(layout.rect(Layer::DialsLeft), layout.rect(Layer::DialsRight), layout.m);
    }
    static void take(std::vector<game::NoteOrder>& to, const DialsOutput& o) {
        for (int i = 0; i < o.count; ++i) {
            to.push_back(o.orders[static_cast<std::size_t>(i)]);
        }
    }
    void pad(const PadState& s) {
        take(pad_o, pad_d.update(pad_m.map(s, pad_d.form()), kDt, 123.0, true, false));
    }
    void mouse(MouseDialReading r) {
        r.enabled = true;
        take(mouse_o, mouse_d.update(mouse_m.map(r, g, mouse_d.form()), kDt, 123.0, true, false));
    }
    void keys(const KeyDialReading& r) {
        take(key_o, key_d.update(dials_from_keys(r, key_d.form()), kDt, 123.0, true, false));
    }
    void toggle_form() {
        PadState p;
        p.active = true;
        p.back = true;
        pad(p);
        p.back = false;
        pad(p);
        MouseDialReading r;
        r.left_pressed = r.left_down = true;
        r.x = g.form_button.x + 1;
        r.y = g.form_button.y + 1;
        mouse(r);
        mouse(MouseDialReading{});
        KeyDialReading k;
        k.form_pressed = true;
        keys(k);
    }
};

PadState pad_state(const Rig&) {
    PadState p;
    p.active = true;
    return p;
}

} // namespace

TEST_CASE("dials input: the same orders come out with pad, mouse and keyboard") {
    // Curva «3 derecha».
    {
        Rig r;
        PadState p = pad_state(r);
        p.shoulder_right = true;
        float x, y;
        vec_at(wheel_center_deg(kMainWheel, 3), 0.9f, x, y);
        p.pick_x = x;
        p.pick_y = y;
        r.pad(p);
        r.pad(p);
        r.pad(pad_state(r));
        MouseDialReading m;
        m.left_pressed = m.left_down = true;
        m.x = r.g.main.cx + r.g.main.r / 2;
        m.y = r.g.main.cy;
        r.mouse(m);
        const DialPoint target = dial_item_point(r.g.main, kMainWheel, 3, 1, 0.8f);
        m.left_pressed = false;
        m.x = target.x;
        m.y = target.y;
        r.mouse(m);
        r.mouse(MouseDialReading{});
        KeyDialReading k;
        k.right = true;
        r.keys(k);
        k.number_pressed[3] = true; // tecla 4: posición 6 − 4 = 2? no: la 3
        k.number_pressed[3] = false;
        k.number_pressed[2] = true; // tecla «3» (índice 2)
        r.keys(k);
        k.number_pressed[2] = false;
        r.keys(k);
        r.keys(KeyDialReading{});
        const game::NoteOrder want =
            game::corner_order(123.0, game::NoteGrade::G3, game::NoteSide::Right);
        REQUIRE(r.pad_o.size() == 1);
        REQUIRE(r.mouse_o.size() == 1);
        REQUIRE(r.key_o.size() == 1);
        CHECK(same(r.pad_o[0], want));
        CHECK(same(r.mouse_o[0], want));
        CHECK(same(r.key_o[0], want));
    }
    // Horquilla a la izquierda, con la tecla 7.
    {
        Rig r;
        KeyDialReading k;
        k.left = true;
        r.keys(k);
        k.number_pressed[6] = true;
        r.keys(k);
        k.number_pressed[6] = false;
        r.keys(k);
        r.keys(KeyDialReading{});
        REQUIRE(r.key_o.size() == 1);
        CHECK(same(r.key_o[0],
                   game::corner_order(123.0, game::NoteGrade::Hairpin, game::NoteSide::Left)));
    }
    // Secundario «se abre» y distancia «100» de la rueda.
    {
        Rig r;
        PadState p = pad_state(r);
        p.throttle = 1.0f;
        float x, y;
        vec_at(wheel_center_deg(kRing8Wheel, 4), 0.9f, x, y);
        p.pick_x = x;
        p.pick_y = y;
        r.pad(p);
        r.pad(p);
        r.pad(pad_state(r));
        MouseDialReading m;
        m.left_pressed = m.left_down = true;
        m.x = r.g.secondary.cx;
        m.y = r.g.secondary.cy;
        r.mouse(m);
        const DialPoint t = dial_item_point(r.g.secondary, kRing8Wheel, 4, 1, 0.8f);
        m.left_pressed = false;
        m.x = t.x;
        m.y = t.y;
        r.mouse(m);
        r.mouse(MouseDialReading{});
        KeyDialReading k;
        k.secondary = true;
        r.keys(k);
        k.number_pressed[4] = true; // tecla 5 = posición 4
        r.keys(k);
        r.keys(KeyDialReading{});
        const game::NoteOrder want = game::mark_order(game::NoteMark::Opens);
        // Con `attach` falso solo el peligro cambia: «se abre» es un modificador.
        REQUIRE(r.pad_o.size() == 1);
        REQUIRE(r.mouse_o.size() == 1);
        REQUIRE(r.key_o.size() == 1);
        CHECK(same(r.pad_o[0], want));
        CHECK(same(r.mouse_o[0], want));
        CHECK(same(r.key_o[0], want));
    }
    // Contador: 80 m (tres veces [+]) y cantar.
    {
        Rig r;
        r.toggle_form();
        REQUIRE(r.pad_d.form() == DistanceForm::Counter);
        REQUIRE(r.mouse_d.form() == DistanceForm::Counter);
        REQUIRE(r.key_d.form() == DistanceForm::Counter);
        PadState p = pad_state(r);
        p.brake = 1.0f;
        for (int i = 0; i < 3; ++i) {
            p.dpad_right = true;
            r.pad(p);
            p.dpad_right = false;
            r.pad(p);
        }
        r.pad(pad_state(r)); // suelta el gatillo: canta
        for (int i = 0; i < 3; ++i) {
            MouseDialReading m;
            m.left_pressed = m.left_down = true;
            m.x = r.g.counter.plus.x + 1;
            m.y = r.g.counter.plus.y + 1;
            r.mouse(m);
            r.mouse(MouseDialReading{});
        }
        MouseDialReading sing;
        sing.left_pressed = sing.left_down = true;
        sing.x = r.g.counter.sing.x + 1;
        sing.y = r.g.counter.sing.y + 1;
        r.mouse(sing);
        r.mouse(MouseDialReading{});
        KeyDialReading k;
        k.distance = true;
        for (int i = 0; i < 3; ++i) {
            k.right = true;
            r.keys(k);
            k.right = false;
            r.keys(k);
        }
        r.keys(KeyDialReading{});
        const game::NoteOrder want = game::distance_order(game::counter_distance(80));
        REQUIRE(r.pad_o.size() == 1);
        REQUIRE(r.mouse_o.size() == 1);
        REQUIRE(r.key_o.size() == 1);
        CHECK(same(r.pad_o[0], want));
        CHECK(same(r.mouse_o[0], want));
        CHECK(same(r.key_o[0], want));
    }
    // Borrar: corto y mantenido.
    {
        Rig r;
        PadState p = pad_state(r);
        p.face_west = true;
        r.pad(p);
        r.pad(pad_state(r));
        MouseDialReading m;
        m.left_pressed = m.left_down = true;
        m.x = r.g.erase.x + 1;
        m.y = r.g.erase.y + 1;
        r.mouse(m);
        r.mouse(MouseDialReading{});
        KeyDialReading k;
        k.erase_down = true;
        r.keys(k);
        r.keys(KeyDialReading{});
        REQUIRE(r.pad_o.size() == 1);
        REQUIRE(r.mouse_o.size() == 1);
        REQUIRE(r.key_o.size() == 1);
        CHECK(same(r.pad_o[0], game::erase_last_order()));
        CHECK(same(r.mouse_o[0], game::erase_last_order()));
        CHECK(same(r.key_o[0], game::erase_last_order()));
    }
}

TEST_CASE("dials input: a click on the road is not a click on a dial") {
    Rig r;
    MouseDialReading m;
    m.enabled = true;
    m.left_pressed = m.left_down = true;
    const ScreenLayout layout = layout_screen(1280, 720, ScreenKind::Copilot);
    const UiRect& tablet = layout.rect(Layer::Tablet);
    m.x = tablet.x + tablet.w / 2;
    m.y = tablet.y + tablet.h / 3;
    const DialsInput in = r.mouse_m.map(m, r.g, DistanceForm::Wheel);
    CHECK_FALSE(in.main_left);
    CHECK_FALSE(in.main_right);
    CHECK_FALSE(in.secondary);
    CHECK_FALSE(in.distance);
    CHECK_FALSE(r.mouse_m.captured());
    CHECK_FALSE(overlaps(tablet, layout.rect(Layer::DialsLeft)));
    CHECK_FALSE(overlaps(tablet, layout.rect(Layer::DialsRight)));
}

TEST_CASE("dials keys: no dial key repeats a key the copilot window reads") {
    const auto help = copilot_help_table();
    const auto is_dial = [](const KeyBinding& b) {
        return b.table == &kCopilotKeys &&
               b.action >= static_cast<std::uint8_t>(CopilotAction::DialLeft);
    };
    int dial_rows = 0;
    bool help_has_h = false;
    for (const KeyBinding& d : help) {
        if (!is_dial(d)) {
            for (const auto* keys : {&d.keys, &d.pair_keys}) {
                for (const KeyChord& c : *keys) {
                    help_has_h = help_has_h || c.key == Key::H;
                }
            }
            continue;
        }
        ++dial_rows;
        for (const KeyBinding& other : help) {
            if (is_dial(other)) {
                continue;
            }
            for (const auto* a : {&d.keys, &d.pair_keys}) {
                for (const KeyChord& ka : *a) {
                    for (const auto* b : {&other.keys, &other.pair_keys}) {
                        for (const KeyChord& kb : *b) {
                            if (ka.key != Key::None && ka.key == kb.key && ka.alt == kb.alt) {
                                INFO("tecla ", std::string(key_label(ka.key)));
                                CHECK(false); // ninguna, sin excepciones
                            }
                        }
                    }
                }
            }
        }
    }
    CHECK(dial_rows == 9);
    CHECK(help_has_h); // la ayuda sigue en H, solo ayuda
    CHECK(count_key_conflicts(key_table()) == 0);
}

TEST_CASE("dials: the map starts oriented ahead") {
    CHECK(kDefaultMapOrientation == MapOrientation::Ahead);
}

namespace {

std::vector<game::DirectionChange> three_changes() {
    return {{100.0, 150.0, 0.5}, {300.0, 350.0, -0.5}, {500.0, 550.0, 0.5}};
}

net::NotesState notes_with(std::initializer_list<double> anchors, std::uint32_t revision) {
    net::NotesState n;
    n.revision = revision;
    for (const double a : anchors) {
        n.anchors[static_cast<std::size_t>(n.count++)] = a;
    }
    return n;
}

} // namespace

TEST_CASE(
    "dials cursor: skips changes that already have a note, jumps on send, stays on the last") {
    const auto changes = three_changes();
    TabletCursor c;
    c.update(changes, 0.0, 1000.0, 0, kDt); // sin estado de notas: como en el lote 6
    CHECK(c.s() == 100.0);
    const net::NotesState none = notes_with({}, 1);
    c.update(changes, 0.0, 1000.0, 0, kDt, &none);
    CHECK(c.s() == 100.0);
    const net::NotesState one = notes_with({100.0}, 2);
    c.update(changes, 0.0, 1000.0, 0, kDt, &one);
    CHECK(c.s() == 300.0);
    // Al enviar una nota en 300, salta al siguiente sin esperar al estado del host.
    c.note_sent(300.0, one.revision);
    c.update(changes, 0.0, 1000.0, 0, kDt, &one);
    CHECK(c.s() == 500.0);
    // Llega el estado con la nota nueva: sigue en el siguiente sin nota.
    const net::NotesState two = notes_with({100.0, 300.0}, 3);
    c.update(changes, 0.0, 1000.0, 0, kDt, &two);
    CHECK(c.s() == 500.0);
    // Con todos con nota, se queda en el último.
    const net::NotesState all = notes_with({100.0, 300.0, 500.0}, 4);
    c.update(changes, 0.0, 1000.0, 0, kDt, &all);
    CHECK(c.s() == 500.0);
    CHECK(c.valid());
    // Se borra la de 300: vuelve a ese cambio.
    const net::NotesState erased = notes_with({100.0, 500.0}, 5);
    c.update(changes, 0.0, 1000.0, 0, kDt, &erased);
    CHECK(c.s() == 300.0);
    // El coche pasa la de 300: el siguiente sin nota por delante es ninguno, se queda en el último.
    c.update(changes, 320.0, 1000.0, 0, kDt, &erased);
    CHECK(c.s() == 500.0);
    // Una nota anclada algo antes del cambio (20 m) cuenta como suya.
    const net::NotesState early = notes_with({282.0}, 6);
    c.update(changes, 0.0, 1000.0, 0, kDt, &early);
    CHECK(c.s() == 100.0);
}

TEST_CASE("dials cursor: a hand-moved cursor is not an attach target, and note_sent resets it") {
    const auto changes = three_changes();
    TabletCursor c;
    const net::NotesState none = notes_with({}, 1);
    c.update(changes, 0.0, 1000.0, 0, kDt, &none);
    c.place(420.0, 0.0, 1000.0);
    CHECK(c.manual());
    c.note_sent(420.0, 1);
    CHECK_FALSE(c.manual()); // el salto automático no cuenta como mover a mano
}

namespace {

struct Res {
    int w, h;
};

bool within(const UiRect& outer, const UiRect& r) { return contains(outer, r); }

bool circle_within(const UiRect& outer, const DialCircle& c) {
    return within(outer, UiRect{c.cx - c.r, c.cy - c.r, 2 * c.r, 2 * c.r});
}

UiRect box(const DialCircle& c) { return UiRect{c.cx - c.r, c.cy - c.r, 2 * c.r, 2 * c.r}; }

} // namespace

TEST_CASE("dials layout: everything fits in its column, nothing overlaps, the texts fit") {
    for (const Res res : {Res{640, 360}, Res{1280, 720}, Res{1920, 1080}, Res{2560, 1440}}) {
        const ScreenLayout layout = layout_screen(res.w, res.h, ScreenKind::Copilot);
        const UiRect& left = layout.rect(Layer::DialsLeft);
        const UiRect& right = layout.rect(Layer::DialsRight);
        const DialsGeometry g = dials_geometry(left, right, layout.m);
        INFO(res.w, "x", res.h);
        REQUIRE(left.w > 0);
        REQUIRE(right.w > 0);
        CHECK(g.main.r >= 8);
        CHECK(g.secondary.r >= 8);
        CHECK(g.distance.r >= 8);
        // Izquierda.
        const UiRect left_parts[] = {g.side_label, box(g.main), g.erase, g.last_line,
                                     g.pending_line};
        for (std::size_t i = 0; i < 5; ++i) {
            CHECK(within(left, left_parts[i]));
            for (std::size_t j = i + 1; j < 5; ++j) {
                CHECK_FALSE(overlaps(left_parts[i], left_parts[j]));
            }
        }
        // Derecha: la rueda de distancias y el contador comparten sitio (solo se ve una).
        const UiRect right_parts[] = {g.form_button, g.secondary_title, box(g.secondary),
                                      g.distance_title, box(g.distance)};
        for (std::size_t i = 0; i < 5; ++i) {
            CHECK(within(right, right_parts[i]));
            for (std::size_t j = i + 1; j < 5; ++j) {
                CHECK_FALSE(overlaps(right_parts[i], right_parts[j]));
            }
        }
        const UiRect counter[] = {g.counter.display, g.counter.minus, g.counter.plus,
                                  g.counter.sing};
        for (std::size_t i = 0; i < 4; ++i) {
            CHECK(within(right, counter[i]));
            CHECK_FALSE(overlaps(counter[i], g.distance_title));
            CHECK_FALSE(overlaps(counter[i], box(g.secondary)));
            for (std::size_t j = i + 1; j < 4; ++j) {
                CHECK_FALSE(overlaps(counter[i], counter[j]));
            }
        }
        // Los textos fijos caben a lo ancho de su sitio (con la fuente de la pantalla); las líneas
        // de «última» y «pendientes» se recortan con «...» (text_fit) si la nota es larga.
        const int font = layout.m.font;
        CHECK(text_width("dist.: contador", font) <= g.form_button.w);
        CHECK(text_width("peligros y marcas", font) <= g.secondary_title.w);
        CHECK(text_width("distancia (contador)", font) <= g.distance_title.w);
        CHECK(text_width("pendientes: 888", font) <= g.pending_line.w);
        CHECK(text_width("cantar", font) <= g.counter.sing.w);
        CHECK(text_width("vaciando...", font) <= g.erase.w);
        for (const char* label : {"cresta", "salto", "cuidado", "cierra", "abre", "no cort.",
                                  "larga", "y", "300+", "100", "150"}) {
            CHECK(text_width(label, font) <= right.w - 2);
        }
    }
}

TEST_CASE(
    "dials layout: the dial columns are exclusive with the help and absent in the game screen") {
    CHECK(layers_exclusive(Layer::Help, Layer::DialsLeft));
    CHECK(layers_exclusive(Layer::Help, Layer::DialsRight));
    CHECK(layers_overdraw(Layer::DialsRight, Layer::Perf));
    const ScreenLayout game_screen = layout_screen(1280, 720);
    CHECK(game_screen.rect(Layer::DialsLeft).w == 0);
    CHECK(game_screen.rect(Layer::DialsRight).w == 0);
    const ScreenLayout copilot = layout_screen(1280, 720, ScreenKind::Copilot);
    CHECK_FALSE(overlaps(copilot.rect(Layer::DialsLeft), copilot.rect(Layer::Debug)));
    CHECK_FALSE(overlaps(copilot.rect(Layer::DialsLeft), copilot.rect(Layer::Messages)));
    CHECK_FALSE(overlaps(copilot.rect(Layer::DialsRight), copilot.rect(Layer::Messages)));
    CHECK_FALSE(overlaps(copilot.rect(Layer::DialsRight), copilot.rect(Layer::HelpHint)));
    CHECK_FALSE(overlaps(copilot.rect(Layer::DialsLeft), copilot.rect(Layer::Indicator)));
}

namespace {

template <typename Done>
bool pump(net::Host& host, net::Copilot& copilot, Done done) {
    const auto start = std::chrono::steady_clock::now();
    const auto now = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
    while (now() < 5.0) {
        host.service();
        copilot.service(now());
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return done();
}

} // namespace

TEST_CASE("dials host: the notes session applies, logs and publishes what the copilot sings") {
    net::Host host;
    std::string error;
    REQUIRE_MESSAGE(host.start(0, error), error);
    net::Copilot copilot;
    REQUIRE_MESSAGE(copilot.connect("127.0.0.1", host.port(), error), error);
    host.set_world("mundo", "coche", 1);
    std::optional<net::WorldInfo> info;
    REQUIRE(pump(host, copilot, [&] {
        if (!info) {
            info = copilot.take_world();
        }
        return info.has_value();
    }));

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "rally_diales_test";
    {
        std::error_code first;
        std::filesystem::remove_all(dir, first);
    }
    const std::string log_path = (dir / "pasada-notas.csv").string();
    NotesSession session;
    session.start_stage(2000.0, three_changes(), "prueba", log_path);
    CHECK_FALSE(session.log_open()); // sin órdenes no hay archivo
    CHECK_FALSE(std::filesystem::exists(log_path));

    CHECK(copilot.send_note_order(
        game::corner_order(100.0, game::NoteGrade::G4, game::NoteSide::Right)));
    CHECK(copilot.send_note_order(game::distance_order(game::counter_distance(80))));
    CHECK(copilot.send_note_order(game::erase_last_order()));
    CHECK(copilot.send_note_order(
        game::corner_order(300.0, game::NoteGrade::G2, game::NoteSide::Left)));
    CHECK(copilot.send_note_order(game::distance_order(game::wheel_distance(3))));
    int applied = 0;
    REQUIRE(pump(host, copilot, [&] {
        applied += session.pump(host, 777, 10.0);
        return applied == 5 && copilot.notes().revision == 5;
    }));
    CHECK(session.queue().size() == 1);
    CHECK(copilot.notes().has_last);
    CHECK(copilot.notes().count == 1);
    CHECK(net::notes_has_anchor_near(copilot.notes(), 300.0, 0.002));
    CHECK(net::notes_ahead(copilot.notes(), 10.0) == 1);
    session.close();

    std::string log;
    {
        std::ifstream file(log_path, std::ios::binary);
        std::ostringstream text;
        text << file.rdbuf();
        log = text.str();
    }
    CHECK(log.find("contador") != std::string::npos);
    CHECK(log.find("rueda") != std::string::npos);
    CHECK(log.find("4 derecha") != std::string::npos);
    CHECK(log.find("777,") != std::string::npos);

    // Un mundo sin tramo descarta las órdenes y no escribe nada.
    session.start_without_stage();
    CHECK(copilot.send_note_order(game::erase_last_order()));
    int dropped = 0;
    int polls = 0;
    pump(host, copilot, [&] {
        dropped += session.pump(host, 0, 0.0);
        return ++polls >= 200;
    });
    CHECK(dropped == 0);
    CHECK(session.queue().size() == 0);
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
}

TEST_CASE("dials: the logic of a full frame is cheap (measured)") {
    CopilotDials d;
    PadDialMapper m;
    PadState p;
    p.active = true;
    p.shoulder_right = true;
    p.pick_x = 0.7f;
    p.pick_y = 0.7f;
    const auto t0 = std::chrono::steady_clock::now();
    constexpr int kFrames = 200000;
    int orders = 0;
    for (int i = 0; i < kFrames; ++i) {
        p.shoulder_right = (i / 30) % 2 == 0;
        orders += d.update(m.map(p, DistanceForm::Wheel), kDt, 10.0, true, false).count;
    }
    const double per_frame_us =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() /
        kFrames;
    MESSAGE("lógica de los diales por fotograma: ", per_frame_us, " us; órdenes: ", orders);
    CHECK(orders > 0);
}
