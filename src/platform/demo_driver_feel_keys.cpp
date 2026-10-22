#include "platform/demo_driver_feel_keys.h"

namespace rally::platform {

namespace {

constexpr std::uint8_t a(DriverFeelAction action) { return static_cast<std::uint8_t>(action); }

// order: el puesto de la fila en su bloque de la ayuda y del documento (de 10 en 10, como
// estaban en la tabla única antes de la tarea P1a).
constexpr KeyBinding kRows[] = {
    {a(DriverFeelAction::Volume),
     {chord(Key::F4), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::General,
     30,
     "Volumen",
     "100, 70, 40 y 0 %, por turnos; se guarda en ajustes.txt"},
    {a(DriverFeelAction::SkidSound),
     {chord(Key::Six), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::Debug,
     190,
     "Sonido del deslizamiento: nuevo / el de la 2.7",
     "A/B del sonido de la rueda que desliza o va bloqueada; empieza siempre en el nuevo; no se "
     "guarda en ajustes.txt ni va en la grabación"},
};

} // namespace

const KeyTable kDriverFeelKeys{"conductor en el coche", kRows,
                               static_cast<std::uint8_t>(DriverFeelAction::Count)};

} // namespace rally::platform
