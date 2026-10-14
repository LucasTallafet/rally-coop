#include "platform/demo_screen_keys.h"

namespace rally::platform {

namespace {

constexpr std::uint8_t a(ScreenAction action) { return static_cast<std::uint8_t>(action); }

// order: el puesto de la fila en su bloque de la ayuda y del documento (de 10 en 10, como
// estaban en la tabla única antes de la tarea P1a).
constexpr KeyBinding kRows[] = {
    {a(ScreenAction::DebugPanel),
     {chord(Key::F3), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::Debug,
     10,
     "Panel de depuración",
     "mando, entradas, impacto, coche, grabación, zona, cámara, asiento y ritmo de fotogramas; en "
     "el modo a ciegas, sin superficie ni configuración"},
    {a(ScreenAction::Perf),
     {chord(Key::F1), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::Debug,
     20,
     "Contador de rendimiento",
     "CPU, GPU, draw calls, memoria y gráfica de fotogramas"},
    {a(ScreenAction::Graphs),
     {chord(Key::T), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextZone | kContextArena,
     KeyBlock::Debug,
     30,
     "Gráficas de la telemetría",
     "últimos 10 s, por páginas: 1 · manejo (beta, guiñada, velocidad y entradas aplicadas), 2 · "
     "tren motriz, 3 · suspensión (carga y compresión por rueda) y ocultas; las páginas sin "
     "canales se saltan; en el modo a ciegas no hace nada"},
    {a(ScreenAction::Help),
     {chord(Key::H), {}},
     kNoAction,
     {},
     PadControl::None,
     kContextAll,
     KeyBlock::General,
     10,
     "Ayuda de teclas",
     "por páginas: página siguiente y, tras la última, oculta"},
};

} // namespace

const KeyTable kScreenKeys{"pantalla", kRows, static_cast<std::uint8_t>(ScreenAction::Count)};

} // namespace rally::platform
