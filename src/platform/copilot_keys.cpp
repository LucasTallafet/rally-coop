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
};

bool same_action(const KeyBinding& b, ActionRef ref) {
    return b.table == ref.table && b.action == ref.index;
}

std::vector<KeyBinding> make_help_table() {
    const ActionRef wanted[] = {
        action(CameraAction::Camera),       action(ScreenAction::DebugPanel),
        action(ScreenAction::Perf),         action(ScreenAction::Help),
        action(WindowAction::Fullscreen),   action(WindowAction::Exit),
        action(CopilotAction::SimulatedNet)};
    std::vector<KeyBinding> rows;
    for (const KeyBinding& b : key_table()) {
        for (const ActionRef& ref : wanted) {
            if (same_action(b, ref)) {
                KeyBinding row = b;
                if (same_action(b, action(CameraAction::Camera))) {
                    row.text = "Cámara: persecución / toda la zona";
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
