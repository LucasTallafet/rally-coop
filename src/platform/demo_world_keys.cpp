#include "platform/demo_world_keys.h"

namespace rally::platform {

namespace {

constexpr std::uint8_t a(WorldAction action) { return static_cast<std::uint8_t>(action); }

// order: el puesto de la fila en su bloque de la ayuda y del documento (de 10 en 10, como
// estaban en la tabla única antes de la tarea P1a).
constexpr KeyBinding kRows[] = {
    {a(WorldAction::SteerAssist),
     {chord(Key::F6), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::Drive,
     90,
     "Ayuda de dirección",
     "la activa o la quita; limita las ruedas delanteras al ángulo de máximo agarre desde "
     "30 km/h; reconstruye el "
     "mundo y empieza otra grabación; se guarda en ajustes.txt"},
    {a(WorldAction::Gearbox),
     {chord(Key::F2), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::Drive,
     100,
     "Caja: automática / secuencial",
     "reconstruye el mundo y empieza otra grabación; se guarda en ajustes.txt"},
    {a(WorldAction::SteerDevice),
     {chord(Key::F7), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::Drive,
     110,
     "Dirección: teclado / ratón",
     "con ratón, el desplazamiento horizontal es el volante (cursor capturado; se suelta en "
     "pausa o al perder el foco y el volante vuelve al centro); A/D no giran; no reconstruye el "
     "mundo; se guarda en ajustes.txt"},
    {a(WorldAction::SensitivityDown),
     {chord(Key::Four), {}},
     a(WorldAction::SensitivityUp),
     {chord(Key::Five), {}},
     PadControl::None,
     kContextAll,
     KeyBlock::Drive,
     120,
     "Sensibilidad del stick: menos / más",
     "10 % por pulsación, de 50 a 150 % (100 % es la curva de siempre; menos, más fina al "
     "centro; más, más lineal); solo el stick (el ratón y el teclado no la usan); no reconstruye "
     "el mundo; se guarda en ajustes.txt"},
    {a(WorldAction::ReloadCar),
     {chord(Key::F5), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::General,
     40,
     "Releer el archivo del coche",
     "reconstruye el mundo con el coche en el punto de recolocación más cercano (en el recinto, "
     "en su salida); si el archivo tiene un error, lo muestra con su línea y sigue el coche "
     "anterior"},
    {a(WorldAction::NextCar),
     {chord(Key::F8), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::General,
     50,
     "Configuración siguiente",
     "mid_awd_turbo, front_awd_turbo_5cyl, front_rwd_na y front_awd_turbo_groupa, por turnos, "
     "de la carpeta del coche; reconstruye el mundo; a ciegas o con --hide-config, la pantalla "
     "no dice cuál"},
    {a(WorldAction::ResetWorld),
     {chord(Key::Backspace), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::General,
     60,
     "Reiniciar el mundo",
     "cierra la grabación y empieza otra"},
};

} // namespace

const KeyTable kWorldKeys{"mundo", kRows, static_cast<std::uint8_t>(WorldAction::Count)};

} // namespace rally::platform
