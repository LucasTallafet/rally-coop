#include "platform/keys_raylib.h"

#include <raylib.h>

namespace rally::platform {

namespace {

// Cada tecla de la tabla tiene el valor de su KeyboardKey en raylib (el mensaje es el nombre de la
// tecla que no coincide).
#define RALLY_KEY_CHECK(name, raylib, value, label, short_label)                                   \
    static_assert(static_cast<int>(Key::name) == KEY_##raylib, #name);
RALLY_KEYS(RALLY_KEY_CHECK)
#undef RALLY_KEY_CHECK

bool alt_down() { return IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT); }

template <typename Test>
bool any_chord(Action action, std::uint8_t context, Test test) {
    if ((contexts_for(action) & context) == 0) {
        return false;
    }
    for (const KeyChord& chord : chords_for(action)) {
        if (chord.key != Key::None && test(chord)) {
            return true;
        }
    }
    return false;
}

} // namespace

int raylib_key(Key key) { return static_cast<int>(key); }

bool action_pressed(Action action, std::uint8_t context) {
    const bool alt = alt_down();
    return any_chord(action, context, [alt](const KeyChord& chord) {
        return chord.alt == alt && IsKeyPressed(raylib_key(chord.key));
    });
}

bool action_pressed_repeat(Action action, std::uint8_t context) {
    const bool alt = alt_down();
    return any_chord(action, context, [alt](const KeyChord& chord) {
        const int key = raylib_key(chord.key);
        return chord.alt == alt && (IsKeyPressed(key) || IsKeyPressedRepeat(key));
    });
}

bool action_down(Action action) {
    return any_chord(action, kContextAll,
                     [](const KeyChord& chord) { return IsKeyDown(raylib_key(chord.key)); });
}

bool action_down_or_pressed(Action action) {
    return any_chord(action, kContextAll, [](const KeyChord& chord) {
        const int key = raylib_key(chord.key);
        return IsKeyDown(key) || IsKeyPressed(key);
    });
}

} // namespace rally::platform
