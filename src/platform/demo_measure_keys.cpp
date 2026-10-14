#include "platform/demo_measure_keys.h"

namespace rally::platform {

namespace {

constexpr std::uint8_t a(MeasureAction action) { return static_cast<std::uint8_t>(action); }

// order: el puesto de la fila en su bloque de la ayuda y del documento (de 10 en 10, como
// estaban en la tabla única antes de la tarea P1a).
constexpr KeyBinding kRows[] = {
    {a(MeasureAction::Capture),
     {chord(Key::C), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::Recording,
     10,
     "Captura del fotograma",
     "captura_NNN.png junto al exe"},
    {a(MeasureAction::FrameLog),
     {chord(Key::L), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::Recording,
     20,
     "Registro por fotograma",
     "fotogramas_NNN.csv junto al exe"},
    {a(MeasureAction::Forest),
     {chord(Key::B), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextArena,
     KeyBlock::Debug,
     50,
     "Bosque de medida (recinto)",
     "escena de medida del bosque, por turnos: apagado, uno a uno, DrawMeshInstanced y "
     "persistente"},
    {a(MeasureAction::FrameRate),
     {chord(Key::F), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::Debug,
     70,
     "Monitor / 30 fps",
     "a la frecuencia del monitor o a 30 fps"},
    {a(MeasureAction::Vsync),
     {chord(Key::V), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::Debug,
     80,
     "Sincronización vertical",
     ""},
    {a(MeasureAction::Interpolation),
     {chord(Key::I), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::Debug,
     90,
     "Interpolación",
     ""},
    {a(MeasureAction::Snap),
     {chord(Key::N), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::Debug,
     100,
     "Ajuste del dt al refresco",
     ""},
    {a(MeasureAction::Pause),
     {chord(Key::P), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::General,
     20,
     "Pausa",
     ""},
};

} // namespace

const KeyTable kMeasureKeys{"medidas", kRows, static_cast<std::uint8_t>(MeasureAction::Count)};

} // namespace rally::platform
