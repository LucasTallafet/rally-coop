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
                                action(CopilotAction::Pointer)};
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
