#include "platform/copilot_keys.h"

#include "platform/demo_camera_keys.h"
#include "platform/demo_screen_keys.h"
#include "platform/window_keys.h"

#include <vector>

namespace rally::platform {

namespace {

constexpr std::uint8_t a(CopilotAction action) { return static_cast<std::uint8_t>(action); }

constexpr KeyBinding kRows[] = {
    {a(CopilotAction::SimulatedNet),
     {chord(Key::Z), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextCopilot,
     KeyBlock::Debug,
     40,
     "Red simulada: no / media / mala",
     "solo en la ventana del copiloto (rally.exe --copiloto): media son 100 ± 30 ms con un 5 % de "
     "pérdida y mala, 250 ± 80 ms con un 20 %; solo afecta a los estados que recibe"},
    {a(CopilotAction::MapOrientation),
     {chord(Key::O), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextCopilot,
     KeyBlock::Tablet,
     10,
     "Orientación del mapa: carretera / hacia delante",
     "solo en la ventana del copiloto: carretera orienta el mapa por la tangente del eje (no "
     "depende del morro ni de la velocidad); hacia delante, por la dirección del coche al punto "
     "del eje 40 m por delante (en las horquillas el mapa no da media vuelta)"},
    {a(CopilotAction::CursorNext),
     {chord(Key::Up), {}},
     a(CopilotAction::CursorPrevious),
     {chord(Key::Down), {}},
     PadControl::DPadVertical,
     kContextCopilot,
     KeyBlock::Tablet,
     20,
     "Cursor: siguiente / anterior cambio de dirección",
     "solo en la ventana del copiloto: salta al cambio de dirección siguiente o al anterior, nunca "
     "por detrás del coche; mantenida más de 0,3 s, desliza por la carretera de 20 a 200 m/s en "
     "2 s; por defecto el cursor está en el siguiente cambio de dirección por delante del coche"},
    {a(CopilotAction::Pointer),
     {chord(Key::Space), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextCopilot,
     KeyBlock::Tablet,
     30,
     "Puntero: mantener y pulsar en el mapa",
     "solo en la ventana del copiloto: mientras se mantiene, el ratón deja de mirar (se suelta el "
     "cursor y la cabeza se queda quieta) y un clic en la carretera del mapa coloca el cursor en "
     "el punto más cercano del eje; al soltar, el ratón vuelve a mirar desde donde estaba la "
     "cabeza"},
    {a(CopilotAction::DialLeft),
     {chord(Key::Left), {}},
     a(CopilotAction::DialRight),
     {chord(Key::Right), {}},
     PadControl::Shoulders,
     kContextCopilot,
     KeyBlock::Tablet,
     40,
     "Dial principal: mantener el lado y elegir la nota",
     "solo en la ventana del copiloto: la curva a la izquierda (Izquierda o L1) o a la derecha "
     "(Derecha o R1); "
     "con el teclado se elige con 1-6 o H (la última pulsada), con el mando con el stick izquierdo "
     "(6 arriba, H abajo; el centro cancela) y con el ratón manteniendo el botón sobre la rueda; "
     "al soltar se canta la nota en el cursor. Mientras se mantiene el lado, H elige la horquilla "
     "y no abre la ayuda"},
    {a(CopilotAction::DialSecondary),
     {chord(Key::S), {}},
     kNoAction,
     {},
     PadControl::RightTrigger,
     kContextCopilot,
     KeyBlock::Tablet,
     41,
     "Dial secundario: mantener y elegir",
     "solo en la ventana del copiloto: cresta, salto, cuidado, se cierra, se abre, no cortes, "
     "larga y «y»; con el teclado, S mantenida y 1-8 en ese orden; con el mando, el gatillo "
     "derecho y el stick izquierdo; con el ratón, el botón izquierdo sobre la rueda secundaria; al "
     "soltar se canta. Los modificadores y la «y» van a la última nota; cresta, salto y cuidado, "
     "solos en el cursor"},
    {a(CopilotAction::DialDistance),
     {chord(Key::D), {}},
     kNoAction,
     {},
     PadControl::LeftTrigger,
     kContextCopilot,
     KeyBlock::Tablet,
     42,
     "Distancia: mantener y elegir (rueda o contador)",
     "solo en la ventana del copiloto: la distancia después de la última nota. Rueda: y, 30, 50, "
     "80, 100, 150, 200 y 300+ (teclado: D mantenida y 1-8; mando: gatillo izquierdo y stick; "
     "ratón: botón derecho sobre la rueda). Contador: parte de 50 m y cambia de 10 en 10, de 0 a "
     "990 (teclado: D mantenida y Izquierda y Derecha; mando: gatillo izquierdo y cruceta "
     "izquierda y derecha; "
     "ratón: los botones [-], [cantar] y [+]); al soltar se canta. Nunca se rellena con la "
     "distancia real"},
    {a(CopilotAction::DistanceForm),
     {chord(Key::F), {}},
     kNoAction,
     {},
     PadControl::Back,
     kContextCopilot,
     KeyBlock::Tablet,
     43,
     "Forma de la distancia: rueda / contador",
     "solo en la ventana del copiloto: cambia la forma de elegir la distancia; la activa se ve en "
     "pantalla y se escribe en el registro de notas"},
    {a(CopilotAction::Erase),
     {chord(Key::Backspace), {}},
     kNoAction,
     {},
     PadControl::FaceWest,
     kContextCopilot,
     KeyBlock::Tablet,
     44,
     "Borrar: la última nota / las pendientes",
     "solo en la ventana del copiloto: una pulsación borra la última nota enviada; mantenida 1 s "
     "vacía las pendientes (las de por delante del coche). Con el ratón, el botón «borrar»"},
    {a(CopilotAction::Pick1),
     {chord(Key::One), {}},
     a(CopilotAction::Pick2),
     {chord(Key::Two), {}},
     PadControl::None,
     kContextCopilot,
     KeyBlock::Tablet,
     50,
     "Elegir 1 / 2",
     "solo en la ventana del copiloto, con un dial mantenido: la nota 1 o 2 del dial principal; la "
     "opción 1 o 2 del secundario o de la rueda de distancias"},
    {a(CopilotAction::Pick3),
     {chord(Key::Three), {}},
     a(CopilotAction::Pick4),
     {chord(Key::Four), {}},
     PadControl::None,
     kContextCopilot,
     KeyBlock::Tablet,
     51,
     "Elegir 3 / 4",
     "solo en la ventana del copiloto, con un dial mantenido: la nota 3 o 4; la opción 3 o 4"},
    {a(CopilotAction::Pick5),
     {chord(Key::Five), {}},
     a(CopilotAction::Pick6),
     {chord(Key::Six), {}},
     PadControl::None,
     kContextCopilot,
     KeyBlock::Tablet,
     52,
     "Elegir 5 / 6",
     "solo en la ventana del copiloto, con un dial mantenido: la nota 5 o 6; la opción 5 o 6"},
    {a(CopilotAction::Pick7),
     {chord(Key::Seven), {}},
     a(CopilotAction::Pick8),
     {chord(Key::Eight), {}},
     PadControl::None,
     kContextCopilot,
     KeyBlock::Tablet,
     53,
     "Elegir 7 / 8",
     "solo en la ventana del copiloto, con el secundario o la rueda de distancias mantenidos: la "
     "opción 7 u 8 (el dial principal no tiene notas 7 y 8)"},
    {a(CopilotAction::PickHairpin),
     {chord(Key::H), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextCopilot,
     KeyBlock::Tablet,
     54,
     "Elegir la horquilla (H)",
     "solo en la ventana del copiloto, con el lado del dial principal mantenido: la nota H; sin "
     "lado mantenido, H abre la ayuda como siempre"},
};

bool same_action(const KeyBinding& b, ActionRef ref) {
    return b.table == ref.table && b.action == ref.index;
}

std::vector<KeyBinding> make_help_table() {
    const ActionRef wanted[] = {action(CameraAction::Camera),
                                action(ScreenAction::DebugPanel),
                                action(ScreenAction::Perf),
                                action(ScreenAction::Help),
                                action(WindowAction::Fullscreen),
                                action(WindowAction::Exit),
                                action(CopilotAction::SimulatedNet),
                                action(CameraAction::Look),
                                action(CopilotAction::MapOrientation),
                                action(CopilotAction::CursorNext),
                                action(CopilotAction::Pointer),
                                action(CopilotAction::DialLeft),
                                action(CopilotAction::DialSecondary),
                                action(CopilotAction::DialDistance),
                                action(CopilotAction::DistanceForm),
                                action(CopilotAction::Erase),
                                action(CopilotAction::Pick1),
                                action(CopilotAction::Pick3),
                                action(CopilotAction::Pick5),
                                action(CopilotAction::Pick7),
                                action(CopilotAction::PickHairpin)};
    std::vector<KeyBinding> rows;
    for (const KeyBinding& b : key_table()) {
        for (const ActionRef& ref : wanted) {
            if (same_action(b, ref)) {
                KeyBinding row = b;
                if (same_action(b, action(CameraAction::Camera))) {
                    row.text = "Cámara: asiento / persecución / toda la zona";
                }
                if (same_action(b, action(CameraAction::Look))) {
                    row.text = "Mirar: stick o ratón (central: al frente)";
                }
                rows.push_back(row);
            }
        }
    }
    return rows;
}

} // namespace

const KeyTable kCopilotKeys{"copiloto", kRows, static_cast<std::uint8_t>(CopilotAction::Count)};

std::span<const KeyBinding> copilot_help_table() {
    static const std::vector<KeyBinding> rows = make_help_table();
    return rows;
}

SimulatedNet next_simulated_net(SimulatedNet level) {
    switch (level) {
    case SimulatedNet::Off:
        return SimulatedNet::Medium;
    case SimulatedNet::Medium:
        return SimulatedNet::Bad;
    case SimulatedNet::Bad:
        break;
    }
    return SimulatedNet::Off;
}

std::string_view simulated_net_name(SimulatedNet level) {
    switch (level) {
    case SimulatedNet::Off:
        return "no";
    case SimulatedNet::Medium:
        return "media (100 ± 30 ms, 5 %)";
    case SimulatedNet::Bad:
        return "mala (250 ± 80 ms, 20 %)";
    }
    return "";
}

net::Impairment simulated_net_params(SimulatedNet level) {
    switch (level) {
    case SimulatedNet::Off:
        return {0.0, 0.0, 0.0};
    case SimulatedNet::Medium:
        return {0.100, 0.030, 0.05};
    case SimulatedNet::Bad:
        return {0.250, 0.080, 0.20};
    }
    return {};
}

} // namespace rally::platform
