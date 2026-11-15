#include "platform/demo_zone_modes_keys.h"

namespace rally::platform {

namespace {

constexpr std::uint8_t a(ZoneModeAction action) { return static_cast<std::uint8_t>(action); }

// order: el puesto de la fila en su bloque de la ayuda y del documento (de 10 en 10, como
// estaban en la tabla única antes de la tarea P1a).
constexpr KeyBinding kRows[] = {
    {a(ZoneModeAction::NextSurface),
     {chord(Key::G), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextZone,
     KeyBlock::Zone,
     10,
     "Superficie de la pista ciega",
     "la siguiente; reconstruye el mundo"},
    {a(ZoneModeAction::NextTrial),
     {chord(Key::G), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextBlind,
     KeyBlock::Zone,
     20,
     "A ciegas: siguiente prueba",
     "de la serie de 15 (--blind-seed)"},
    {a(ZoneModeAction::BlindMode),
     {chord(Key::M), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextZone | kContextBlind,
     KeyBlock::Zone,
     30,
     "Modo a ciegas sí / no",
     "todas las superficies del mismo color, sin microrrelieve ni nombres; en pantalla, «Prueba "
     "k/15»; cada prueba sale en la pista ciega"},
    {a(ZoneModeAction::RoadSurface),
     {chord(Key::U), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextZone,
     KeyBlock::Zone,
     40,
     "Superficie de la carretera de prueba",
     "grava / asfalto; reconstruye el mundo con el coche en la salida de la carretera"},
    {a(ZoneModeAction::ZoneStart),
     {chord(Key::Y), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextZone,
     KeyBlock::Zone,
     50,
     "Salida: recta / carretera",
     "dónde sale el coche (también al reconstruir el mundo con G, F2, F6 o Retroceso); "
     "reconstruye el mundo"},
    {a(ZoneModeAction::FarOrigin),
     {chord(Key::O), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextArena,
     KeyBlock::Debug,
     40,
     "Recinto a 20 km del origen",
     "y de vuelta"},
    {a(ZoneModeAction::ToggleArena),
     {chord(Key::K), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::General,
     70,
     "Zona de pruebas / tramo / recinto",
     "recorre los tres; el tramo es data/stages/lazadas_irregular.stage (o lazadas.stage si no "
     "está) o el de --tramo; el recinto es el de la fase 1; reconstruye el mundo y empieza otra "
     "grabación"},
    {a(ZoneModeAction::StageIrregularity),
     {chord(Key::U), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextStage,
     KeyBlock::Zone,
     60,
     "Tramo: irregularidad con / sin",
     "juega el mismo tramo sin su irregularidad y, otra pulsación, con ella; el coche vuelve a la "
     "salida y empieza otra grabación con el texto del tramo usado; el indicador del tramo dice "
     "cuál es; en un tramo sin irregularidad no hay nada que quitar"},
};

} // namespace

const KeyTable kZoneModeKeys{"modos de la zona", kRows,
                             static_cast<std::uint8_t>(ZoneModeAction::Count)};

} // namespace rally::platform
