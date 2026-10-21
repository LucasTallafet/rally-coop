#include "platform/demo_actions_keys.h"

namespace rally::platform {

namespace {

constexpr std::uint8_t a(ActionsAction action) { return static_cast<std::uint8_t>(action); }

// order: el puesto de la fila en el bloque Depuración de la ayuda y del documento, detrás de las
// de las demás partes (de 10 en 10).
constexpr KeyBinding kRows[] = {
    {a(ActionsAction::StowTablet),
     {chord(Key::Seven), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::Debug,
     200,
     "Copiloto: guardar la tablet",
     "hace de copiloto (no hay tablet hasta la fase 3): acción de 0,8 s con anillo de progreso; "
     "solo con la tablet fuera; se graba en la grabación"},
    {a(ActionsAction::TakeTablet),
     {chord(Key::Eight), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::Debug,
     210,
     "Copiloto: sacar la tablet",
     "solo con la tablet guardada; el motivo de un rechazo sale en los mensajes"},
    {a(ActionsAction::CancelAction),
     {chord(Key::Nine), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::Debug,
     220,
     "Copiloto: cancelar la acción",
     "al preparar no cuesta nada; al ejecutar se pierde lo avanzado"},
};

} // namespace

const KeyTable kActionsKeys{"acciones", kRows, static_cast<std::uint8_t>(ActionsAction::Count)};

} // namespace rally::platform
