// dial-teclas (lote 7): choques de la ventana del copiloto, 7 = horquilla, flecha retenida y cursor
// quieto con un dial abierto. Sin ventana ni mando.
#include "platform/copilot_dial_input.h"
#include "platform/copilot_dials.h"
#include "platform/copilot_keys.h"
#include "platform/demo_camera_keys.h"
#include "platform/demo_screen_keys.h"
#include "platform/key_tables.h"
#include "platform/tablet_cursor.h"
#include "platform/window_keys.h"

#include <doctest/doctest.h>

#include <utility>
#include <vector>

using namespace rally::platform;

namespace {
constexpr double kDt = 1.0 / 60.0;
} // namespace

TEST_CASE("dial keys: the copilot context sees the clashes of its window") {
    CHECK(kCopilotKeyContext == kContextCopilot);
    CHECK(count_key_conflicts(key_table()) == 0);
    // Una fila del copiloto que repite la H de la ayuda: el test de choques ya la ve.
    std::vector<KeyBinding> table(key_table().begin(), key_table().end());
    KeyBinding clash;
    clash.keys[0] = chord(Key::H);
    clash.contexts = kContextCopilot;
    table.push_back(clash);
    CHECK(count_key_conflicts(table) == 1);
    // Y una que repite una tecla propia del copiloto (S) también.
    table.back().keys[0] = chord(Key::S);
    CHECK(count_key_conflicts(table) == 1);
}

TEST_CASE("dial keys: exactly the game rows the copilot window reads carry its context") {
    const ActionRef wanted[] = {action(CameraAction::Camera),     action(ScreenAction::DebugPanel),
                                action(ScreenAction::Perf),       action(ScreenAction::Help),
                                action(WindowAction::Fullscreen), action(WindowAction::Exit),
                                action(CameraAction::Look)};
    int both = 0;
    for (const KeyBinding& b : key_table()) {
        if ((b.contexts & kContextCopilot) != 0 && (b.contexts & kContextAll) != 0) {
            ++both;
            bool listed = false;
            for (const ActionRef& w : wanted) {
                listed = listed || (b.table == w.table && b.action == w.index);
            }
            CHECK(listed);
        }
    }
    CHECK(both == 7);
    // La ventana del juego no gana ni pierde teclas: esas filas siguen valiendo en sus cuatro
    // contextos.
    for (const ActionRef& w : wanted) {
        CHECK((contexts_for(w) & kContextAll) == kContextAll);
        CHECK((contexts_for(w) & kContextCopilot) != 0);
    }
}

TEST_CASE("dial keys: 7 picks the hairpin with the main dial open and does nothing alone") {
    // Solo, sin dial: el dial no se abre ni manda nada.
    {
        CopilotDials d;
        KeyDialReading k;
        k.number_pressed[6] = true;
        DialsOutput out = d.update(dials_from_keys(k, d.form()), kDt, 123.0, true, false);
        CHECK(out.count == 0);
        out = d.update(dials_from_keys(KeyDialReading{}, d.form()), kDt, 123.0, true, false);
        CHECK(out.count == 0);
        CHECK(d.view().open == DialKind::None);
    }
    // Con el principal abierto por otra entrada (el mando) y el 7 del teclado.
    DialsInput pad;
    pad.main_right = true;
    KeyDialReading k;
    k.number_pressed[6] = true;
    CHECK(merge_dials(pad, dials_from_keys(k, DistanceForm::Wheel)).direct == kMainHairpinIndex);
    // El 8 no hace nada en el principal; con S es la opción 8; con S el 7 es la opción 7.
    KeyDialReading eight;
    eight.number_pressed[7] = true;
    CHECK(dials_from_keys(eight, DistanceForm::Wheel).direct == -1);
    eight.secondary = true;
    CHECK(dials_from_keys(eight, DistanceForm::Wheel).direct == 7);
    k.secondary = true;
    CHECK(dials_from_keys(k, DistanceForm::Wheel).direct == 6);
}

TEST_CASE("dial keys: an arrow held when another dial closes does not open the main one") {
    KeyDialFilter f;
    KeyDialReading k;
    k.distance = true;
    k.right = true;
    CHECK(f.apply(k, false).right); // con D mantenida, la flecha pasa tal cual (contador)
    k.distance = false;             // se suelta D con → todavía pulsada
    CHECK_FALSE(f.apply(k, false).right);
    CHECK_FALSE(f.apply(k, false).right);
    k.right = false;
    CHECK_FALSE(f.apply(k, false).right);
    k.right = true; // se vuelve a pulsar
    CHECK(f.apply(k, false).right);
}

TEST_CASE("dial keys: an arrow that already held the main dial is not taken by S") {
    KeyDialFilter f;
    KeyDialReading k;
    k.left = true;
    CHECK(f.apply(k, false).left);
    k.secondary = true; // S con ← mantenida: el principal sigue siendo de ←
    CHECK(f.apply(k, false).left);
    k.secondary = false;
    CHECK(f.apply(k, false).left);
}

TEST_CASE("dial keys: a trigger dial closing or a blocked frame also takes the held arrow") {
    {
        KeyDialFilter f;
        KeyDialReading k;
        k.right = true;
        CHECK_FALSE(f.apply(k, true).right); // otro dial (gatillo del mando) abierto
        CHECK_FALSE(f.apply(k, false).right);
        k.right = false;
        f.apply(k, false);
        k.right = true;
        CHECK(f.apply(k, false).right);
    }
    {
        KeyDialFilter f;
        KeyDialReading k;
        k.left = true;
        f.block(k); // ayuda abierta o sin tramo
        CHECK_FALSE(f.apply(k, false).left);
        k.left = false;
        f.apply(k, false);
        k.left = true;
        CHECK(f.apply(k, false).left);
    }
}

TEST_CASE("dial keys: the cursor stays still with a dial open and after it closes until released") {
    const std::vector<rally::game::DirectionChange> changes{{100.0, 150.0, 0.5}};
    for (const std::pair<bool, bool> dir : {std::pair{true, false}, std::pair{false, true}}) {
        // Los tres diales dan lo mismo: la puerta solo mira «hay un dial abierto».
        for (int dial = 0; dial < 3; ++dial) {
            INFO("dial ", dial);
            TabletCursor c;
            c.update(changes, 0.0, 1000.0, 0, kDt);
            CHECK_FALSE(c.manual());
            CursorHoldGate gate;
            for (int i = 0; i < 30; ++i) {
                const int held = gate.update(dir.first, dir.second, true);
                CHECK(held == 0);
                c.update(changes, 0.0, 1000.0, held, kDt);
            }
            // El dial se cierra con la dirección todavía pulsada: sigue valiendo 0.
            for (int i = 0; i < 30; ++i) {
                const int held = gate.update(dir.first, dir.second, false);
                CHECK(held == 0);
                c.update(changes, 0.0, 1000.0, held, kDt);
            }
            CHECK_FALSE(c.manual());
            // Se suelta y se vuelve a pulsar: cuenta.
            CHECK(gate.update(false, false, false) == 0);
            CHECK(gate.update(dir.first, dir.second, false) == (dir.first ? 1 : -1));
        }
    }
    CursorHoldGate g;
    CHECK(g.update(true, false, false) == 1);
    CHECK(g.update(true, true, false) == 0);
}

TEST_CASE("dial keys: H turns the help page only with no dial open") {
    CHECK(next_help_page(-1, 3, true, false) == 0);
    CHECK(next_help_page(0, 3, true, false) == 1);
    CHECK(next_help_page(2, 3, true, false) == -1);
    CHECK(next_help_page(-1, 3, true, true) == -1); // con un dial abierto, H no hace nada
    CHECK(next_help_page(-1, 3, false, false) == -1);
}
