#include "platform/drive_keys.h"

namespace rally::platform {

namespace {

constexpr std::uint8_t a(DriveAction action) { return static_cast<std::uint8_t>(action); }

// order: el puesto de la fila en su bloque de la ayuda y del documento (de 10 en 10, como
// estaban en la tabla única antes de la tarea P1a).
constexpr KeyBinding kRows[] = {
    {a(DriveAction::Throttle),
     {chord(Key::W), chord(Key::Up)},
     kNoAction,
     {},
     PadControl::RightTrigger,
     kContextAll,
     KeyBlock::Drive,
     10,
     "Acelerador",
     ""},
    {a(DriveAction::Brake),
     {chord(Key::S), chord(Key::Down)},
     kNoAction,
     {},
     PadControl::LeftTrigger,
     kContextAll,
     KeyBlock::Drive,
     20,
     "Freno",
     ""},
    {a(DriveAction::SteerLeft),
     {chord(Key::A), chord(Key::Left)},
     a(DriveAction::SteerRight),
     {chord(Key::D), chord(Key::Right)},
     PadControl::LeftStickX,
     kContextAll,
     KeyBlock::Drive,
     30,
     "Girar",
     ""},
    {a(DriveAction::Handbrake),
     {chord(Key::Space), {}},
     kNoAction,
     {},
     PadControl::FaceSouth,
     kContextAll,
     KeyBlock::Drive,
     40,
     "Freno de mano",
     "ruedas traseras; mientras se tira, sin par motor"},
    {a(DriveAction::ShiftDown),
     {chord(Key::Q), {}},
     kNoAction,
     {},
     PadControl::LeftShoulder,
     kContextAll,
     KeyBlock::Drive,
     50,
     "Bajar marcha",
     "caja R, N, 1 a 5: de 1 a N (en automático, solo parado) y de N a R parado (menos de "
     "2 km/h); en secuencial, una marcha (no si pasaría del limitador)"},
    {a(DriveAction::ShiftUp),
     {chord(Key::E), {}},
     kNoAction,
     {},
     PadControl::RightShoulder,
     kContextAll,
     KeyBlock::Drive,
     60,
     "Subir marcha",
     "de R a N y de N a 1 (no si pasaría del limitador); en secuencial, una marcha; en "
     "automático, desde 1 cambia solo"},
    {a(DriveAction::Reset),
     {chord(Key::R), {}},
     kNoAction,
     {},
     PadControl::FaceNorth,
     kContextAll,
     KeyBlock::Drive,
     70,
     "Recolocar",
     "en la zona, al punto de recolocación más cercano, parado y con su orientación; en el "
     "recinto, a la salida mirando al frente"},
    {a(DriveAction::RecenterWheel),
     {chord(Key::X), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::Drive,
     80,
     "Recentrar volante (ratón)",
     "también el botón central del ratón; solo con la dirección con ratón"},
};

} // namespace

const KeyTable kDriveKeys{"conducir", kRows, static_cast<std::uint8_t>(DriveAction::Count)};

} // namespace rally::platform
