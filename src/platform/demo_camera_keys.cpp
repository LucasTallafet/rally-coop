#include "platform/demo_camera_keys.h"

namespace rally::platform {

namespace {

constexpr std::uint8_t a(CameraAction action) { return static_cast<std::uint8_t>(action); }

// order: el puesto de la fila en su bloque de la ayuda y del documento (de 10 en 10, como
// estaban en la tabla única antes de la tarea P1a).
constexpr KeyBinding kRows[] = {
    {a(CameraAction::Camera),
     {chord(Key::Tab), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::CameraSeat,
     10,
     "Cámara",
     "ojo del conductor (el del archivo del coche más el asiento), persecución y fija (recinto) "
     "o toda la zona, por turnos"},
    {a(CameraAction::HeadMotion),
     {chord(Key::F9), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::CameraSeat,
     20,
     "Movimiento de la cabeza",
     "sí / no: el ojo se mueve con las aceleraciones del coche (solo la cámara del ojo); se "
     "guarda en ajustes.txt"},
    {a(CameraAction::SeatUp),
     {chord(Key::PageUp), {}},
     a(CameraAction::SeatDown),
     {chord(Key::PageDown), {}},
     PadControl::None,
     kContextAll,
     KeyBlock::CameraSeat,
     30,
     "Asiento: arriba / abajo",
     "1 cm por pulsación, se repite al mantener; solo la vista del ojo, no la simulación ni la "
     "grabación; límite ± 15 cm en cada eje; se guarda en ajustes.txt junto al exe"},
    {a(CameraAction::SeatForward),
     {chord(Key::Home), {}},
     a(CameraAction::SeatBack),
     {chord(Key::End), {}},
     PadControl::None,
     kContextAll,
     KeyBlock::CameraSeat,
     40,
     "Asiento: adelante / atrás",
     ""},
    {a(CameraAction::SeatRight),
     {chord(Key::Insert), {}},
     a(CameraAction::SeatLeft),
     {chord(Key::Delete), {}},
     PadControl::None,
     kContextAll,
     KeyBlock::CameraSeat,
     50,
     "Asiento: a los lados",
     "derecha / izquierda"},
    {a(CameraAction::WorldFloat),
     {chord(Key::J), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::Debug,
     60,
     "Dibujar en float del mundo",
     "depuración del render relativo"},
};

} // namespace

const KeyTable kCameraKeys{"cámara", kRows, static_cast<std::uint8_t>(CameraAction::Count)};

} // namespace rally::platform
