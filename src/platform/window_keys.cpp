#include "platform/window_keys.h"

namespace rally::platform {

namespace {

constexpr std::uint8_t a(WindowAction action) { return static_cast<std::uint8_t>(action); }

// order: el puesto de la fila en su bloque de la ayuda y del documento (de 10 en 10, como
// estaban en la tabla única antes de la tarea P1a).
constexpr KeyBinding kRows[] = {
    {a(WindowAction::Fullscreen),
     {chord(Key::F11), chord(Key::Enter, true)},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::General,
     80,
     "Pantalla completa sin bordes",
     ""},
    {a(WindowAction::Exit),
     {chord(Key::Escape), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::General,
     90,
     "Salir",
     ""},
};

} // namespace

const KeyTable kWindowKeys{"ventana", kRows, static_cast<std::uint8_t>(WindowAction::Count)};

} // namespace rally::platform
