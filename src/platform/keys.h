#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace rally::platform {

// La única tabla de teclas del juego (sin menú de reasignación hasta los menús, fase 5). De ella
// salen el mapeo del teclado y del mando, la ayuda en pantalla (H) y la tabla de
// docs/07-entorno-y-portabilidad.md §8 (keys_markdown; un test la compara con el documento).
// Sin raylib: Key lleva los valores de KeyboardKey de raylib, y platform/keys_raylib.cpp
// comprueba cada uno con un static_assert sobre la misma lista.

// KEY(nombre, sufijo de KEY_ en raylib, valor, etiqueta, etiqueta corta de la ayuda). Etiquetas en
// Latin-1: es lo que tiene la fuente por defecto de raylib.
#define RALLY_KEYS(KEY)                                                                            \
    KEY(Space, SPACE, 32, "Espacio", "Espacio")                                                    \
    KEY(B, B, 66, "B", "B")                                                                        \
    KEY(C, C, 67, "C", "C")                                                                        \
    KEY(D, D, 68, "D", "D")                                                                        \
    KEY(E, E, 69, "E", "E")                                                                        \
    KEY(F, F, 70, "F", "F")                                                                        \
    KEY(G, G, 71, "G", "G")                                                                        \
    KEY(H, H, 72, "H", "H")                                                                        \
    KEY(I, I, 73, "I", "I")                                                                        \
    KEY(J, J, 74, "J", "J")                                                                        \
    KEY(K, K, 75, "K", "K")                                                                        \
    KEY(L, L, 76, "L", "L")                                                                        \
    KEY(M, M, 77, "M", "M")                                                                        \
    KEY(N, N, 78, "N", "N")                                                                        \
    KEY(O, O, 79, "O", "O")                                                                        \
    KEY(P, P, 80, "P", "P")                                                                        \
    KEY(Q, Q, 81, "Q", "Q")                                                                        \
    KEY(R, R, 82, "R", "R")                                                                        \
    KEY(S, S, 83, "S", "S")                                                                        \
    KEY(T, T, 84, "T", "T")                                                                        \
    KEY(U, U, 85, "U", "U")                                                                        \
    KEY(V, V, 86, "V", "V")                                                                        \
    KEY(W, W, 87, "W", "W")                                                                        \
    KEY(X, X, 88, "X", "X")                                                                        \
    KEY(Y, Y, 89, "Y", "Y")                                                                        \
    KEY(A, A, 65, "A", "A")                                                                        \
    KEY(Escape, ESCAPE, 256, "Esc", "Esc")                                                         \
    KEY(Enter, ENTER, 257, "Intro", "Intro")                                                       \
    KEY(Tab, TAB, 258, "Tab", "Tab")                                                               \
    KEY(Backspace, BACKSPACE, 259, "Retroceso", "Retroceso")                                       \
    KEY(Insert, INSERT, 260, "Insert", "Insert")                                                   \
    KEY(Delete, DELETE, 261, "Supr", "Supr")                                                       \
    KEY(Right, RIGHT, 262, "Derecha", "Der.")                                                      \
    KEY(Left, LEFT, 263, "Izquierda", "Izq.")                                                      \
    KEY(Down, DOWN, 264, "Abajo", "Abajo")                                                         \
    KEY(Up, UP, 265, "Arriba", "Arriba")                                                           \
    KEY(PageUp, PAGE_UP, 266, "Re Pág", "Re Pág")                                                  \
    KEY(PageDown, PAGE_DOWN, 267, "Av Pág", "Av Pág")                                              \
    KEY(Home, HOME, 268, "Inicio", "Inicio")                                                       \
    KEY(End, END, 269, "Fin", "Fin")                                                               \
    KEY(F1, F1, 290, "F1", "F1")                                                                   \
    KEY(F2, F2, 291, "F2", "F2")                                                                   \
    KEY(F3, F3, 292, "F3", "F3")                                                                   \
    KEY(F5, F5, 294, "F5", "F5")                                                                   \
    KEY(F6, F6, 295, "F6", "F6")                                                                   \
    KEY(F7, F7, 296, "F7", "F7")                                                                   \
    KEY(F8, F8, 297, "F8", "F8")                                                                   \
    KEY(F11, F11, 300, "F11", "F11")

enum class Key : int {
    None = 0,
#define RALLY_KEY_ENUM(name, raylib, value, label, short_label) name = value,
    RALLY_KEYS(RALLY_KEY_ENUM)
#undef RALLY_KEY_ENUM
};

// Etiqueta de la tecla ("" para None): la del documento y la corta de la ayuda en pantalla.
std::string_view key_label(Key key);
std::string_view key_label_short(Key key);

// Una tecla con o sin Alt. Una pulsación sin Alt no cuenta si Alt está pulsado (Alt+Intro no es
// Intro); las teclas que se mantienen (conducir, asiento) no miran Alt.
struct KeyChord {
    Key key = Key::None;
    bool alt = false;
};

// Mando, por posición (nunca por letra).
enum class PadControl : std::uint8_t {
    None,
    LeftStickX,
    RightTrigger,
    LeftTrigger,
    FaceSouth, // botón de abajo de la cara derecha: A Xbox, Cruz PlayStation, B Nintendo
    FaceNorth, // el de arriba: Y Xbox, Triángulo PlayStation, X Nintendo
    LeftShoulder,
    RightShoulder,
};

enum class Action : std::uint8_t {
    None,
    // Conducir
    Throttle,
    Brake,
    SteerLeft,
    SteerRight,
    Handbrake,
    ShiftDown,
    ShiftUp,
    Reset,
    RecenterWheel,
    SteerAssist,
    SteerDevice,
    Gearbox,
    // Cámara y asiento
    Camera,
    SeatUp,
    SeatDown,
    SeatForward,
    SeatBack,
    SeatRight,
    SeatLeft,
    // Zona
    NextSurface,
    NextTrial,
    BlindMode,
    RoadSurface,
    ZoneStart,
    // Grabación
    Capture,
    FrameLog,
    // Depuración
    DebugPanel,
    Perf,
    Graphs,
    FarOrigin,
    Forest,
    WorldFloat,
    FrameRate,
    Vsync,
    Interpolation,
    Snap,
    // General
    Help,
    Pause,
    ReloadCar,
    NextCar,
    ResetWorld,
    ToggleArena,
    Fullscreen,
    Exit,
    Count,
};
inline constexpr int kActionCount = static_cast<int>(Action::Count);

// Contextos en los que vale una tecla. Solo uno está activo a la vez: la zona sin el modo a
// ciegas, la zona a ciegas o el recinto. Una tecla puede repetirse solo en contextos disjuntos.
enum KeyContext : std::uint8_t {
    kContextZone = 1,
    kContextBlind = 2,
    kContextArena = 4,
    kContextAll = kContextZone | kContextBlind | kContextArena,
};

enum class KeyBlock : std::uint8_t { Drive, CameraSeat, Zone, Recording, Debug, General, Count };
inline constexpr int kKeyBlockCount = static_cast<int>(KeyBlock::Count);
std::string_view key_block_name(KeyBlock block);

// Una fila de la tabla. Las acciones en pareja (girar, asiento) van en una sola fila: `action`
// con `keys` y `pair_action` con `pair_keys` (la tecla i de cada una forma la pareja i).
struct KeyBinding {
    Action action = Action::None;
    std::array<KeyChord, 2> keys{};
    Action pair_action = Action::None;
    std::array<KeyChord, 2> pair_keys{};
    PadControl pad = PadControl::None;
    std::uint8_t contexts = kContextAll;
    KeyBlock block = KeyBlock::General;
    const char* text = "";   // corto: la ayuda y la columna Acción
    const char* detail = ""; // solo en el documento, detrás de text y ": "
};

std::span<const KeyBinding> key_table();

// Teclas, mando y contextos de una acción (en la fila de su pareja, si la tiene).
std::array<KeyChord, 2> chords_for(Action action, std::span<const KeyBinding> table = key_table());
PadControl pad_for(Action action, std::span<const KeyBinding> table = key_table());
std::uint8_t contexts_for(Action action, std::span<const KeyBinding> table = key_table());

// Pares de (tecla con Alt o sin él) repetidos en contextos que se cruzan.
int count_key_conflicts(std::span<const KeyBinding> table);

// Etiquetas de una fila: "A/D o Izquierda/Derecha", "F11 o Alt+Intro"... (short: las cortas).
std::string keys_text(const KeyBinding& binding, bool short_labels = false);
// Mando: en el documento, completo; en la ayuda, corto.
std::string_view pad_text_doc(PadControl pad);
std::string_view pad_text_short(PadControl pad);
// "teclas · mando" (sin mando, solo las teclas).
std::string binding_label_doc(const KeyBinding& binding);
std::string binding_label_short(const KeyBinding& binding);

// Tablas de docs/07 §8, una por bloque con teclas, en el orden de la tabla.
std::string keys_markdown(std::span<const KeyBinding> table);

} // namespace rally::platform
