#include "platform/demo_camera_keys.h"
#include "platform/demo_zone_modes_keys.h"
#include "platform/drive_keys.h"
#include "platform/key_tables.h"
#include "platform/keys.h"
#include "platform/ui_text.h"
#include "platform/window_keys.h"

#include <doctest/doctest.h>

#include <array>
#include <fstream>
#include <sstream>
#include <string>

using rally::platform::ActionRef;
using rally::platform::count_key_conflicts;
using rally::platform::Key;
using rally::platform::KeyBinding;
using rally::platform::KeyBlock;
using rally::platform::KeyChord;
using rally::platform::PadControl;

namespace {

KeyBinding row(std::uint8_t action, KeyChord a, KeyChord b, std::uint8_t contexts) {
    KeyBinding binding;
    binding.action = action;
    binding.keys = {a, b};
    binding.contexts = contexts;
    return binding;
}

constexpr KeyChord plain(Key key) { return KeyChord{key, false}; }
constexpr KeyChord with_alt(Key key) { return KeyChord{key, true}; }

std::string read_text(const char* path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    std::string out;
    for (const char c : text.str()) {
        if (c != '\r') {
            out += c; // git puede dejarlo con CRLF
        }
    }
    return out;
}

std::string trim_newlines(std::string s) {
    while (!s.empty() && s.front() == '\n') {
        s.erase(s.begin());
    }
    while (!s.empty() && s.back() == '\n') {
        s.pop_back();
    }
    return s;
}

} // namespace

TEST_CASE("keys: no key is repeated within a context") {
    CHECK(count_key_conflicts(rally::platform::key_table()) == 0);
}

TEST_CASE("keys: G is shared only by contexts that are never active together") {
    // G: superficie en la zona y siguiente prueba a ciegas.
    using rally::platform::action;
    using rally::platform::ZoneModeAction;
    CHECK(rally::platform::chords_for(action(ZoneModeAction::NextSurface))[0].key == Key::G);
    CHECK(rally::platform::chords_for(action(ZoneModeAction::NextTrial))[0].key == Key::G);
    CHECK((rally::platform::contexts_for(action(ZoneModeAction::NextSurface)) &
           rally::platform::contexts_for(action(ZoneModeAction::NextTrial))) == 0);
}

TEST_CASE("keys: a repeated key in overlapping contexts is a conflict") {
    using namespace rally::platform;
    const std::array clash = {row(0, plain(Key::P), {}, kContextAll),
                              row(1, plain(Key::P), {}, kContextArena)};
    CHECK(count_key_conflicts(clash) == 1);
    const std::array apart = {row(0, plain(Key::G), {}, kContextZone),
                              row(1, plain(Key::G), {}, kContextBlind)};
    CHECK(count_key_conflicts(apart) == 0);
}

TEST_CASE("keys: Enter and Alt+Enter are different keys") {
    using namespace rally::platform;
    const std::array both = {row(0, plain(Key::F11), with_alt(Key::Enter), kContextAll),
                             row(1, plain(Key::Enter), {}, kContextAll)};
    CHECK(count_key_conflicts(both) == 0);
    const std::array twice = {row(0, plain(Key::F11), with_alt(Key::Enter), kContextAll),
                              row(1, with_alt(Key::Enter), {}, kContextZone)};
    CHECK(count_key_conflicts(twice) == 1);
}

TEST_CASE("keys: every action has exactly one row") {
    // En cada tabla, cada acción de su enum en una fila (como acción o como pareja) y con tecla.
    using namespace rally::platform;
    for (const KeyTable* t : key_tables()) {
        INFO("tabla ", t->name);
        std::array<int, 256> seen{};
        for (const KeyBinding& b : key_table()) {
            if (b.table != t) {
                continue;
            }
            REQUIRE(b.action != kNoAction);
            ++seen[b.action];
            if (b.pair_action != kNoAction) {
                ++seen[b.pair_action];
            }
        }
        for (int a = 0; a < 256; ++a) {
            INFO("acción ", a);
            CHECK(seen[static_cast<std::size_t>(a)] == (a < t->action_count ? 1 : 0));
            if (a < t->action_count) {
                CHECK(chords_for(ActionRef{t, static_cast<std::uint8_t>(a)})[0].key != Key::None);
            }
        }
    }
    // Todas las filas de todas las tablas están en la junta.
    std::size_t rows = 0;
    for (const KeyTable* t : key_tables()) {
        rows += t->rows.size();
    }
    CHECK(key_table().size() == rows);
}

TEST_CASE("keys: a key repeated in two component tables is a conflict") {
    // Las tablas de los componentes se escriben por separado: la junta es la que ve el choque.
    using namespace rally::platform;
    const std::array<KeyBinding, 1> a_rows = {row(0, plain(Key::P), {}, kContextAll)};
    const std::array<KeyBinding, 1> b_rows = {row(0, plain(Key::P), {}, kContextZone)};
    const std::array<KeyBinding, 1> c_rows = {row(0, plain(Key::K), {}, kContextAll)};
    const KeyTable a{"a", a_rows, 1};
    const KeyTable b{"b", b_rows, 1};
    const KeyTable c{"c", c_rows, 1};
    const std::array<const KeyTable*, 2> clash = {&a, &b};
    CHECK(count_key_conflicts(join_key_tables(clash)) == 1);
    const std::array<const KeyTable*, 2> apart = {&a, &c};
    CHECK(count_key_conflicts(join_key_tables(apart)) == 0);
}

TEST_CASE("keys: the seat has no gamepad control") {
    using rally::platform::action;
    using rally::platform::CameraAction;
    for (const CameraAction a :
         {CameraAction::SeatUp, CameraAction::SeatDown, CameraAction::SeatForward,
          CameraAction::SeatBack, CameraAction::SeatRight, CameraAction::SeatLeft}) {
        CHECK(rally::platform::pad_for(action(a)) == PadControl::None);
    }
}

TEST_CASE("keys: every label and text is drawable with the default font") {
    using namespace rally::platform;
    for (const KeyBinding& b : key_table()) {
        INFO(b.text);
        CHECK(in_default_font(binding_label_doc(b)));
        CHECK(in_default_font(binding_label_short(b)));
        CHECK(in_default_font(b.text));
        CHECK(in_default_font(b.detail));
    }
    for (int block = 0; block < kKeyBlockCount; ++block) {
        CHECK(in_default_font(key_block_name(static_cast<KeyBlock>(block))));
    }
}

TEST_CASE("keys: pair rows join their keys") {
    using namespace rally::platform;
    for (const KeyBinding& b : key_table()) {
        const ActionRef ref{b.table, b.action};
        const ActionRef steer = action(DriveAction::SteerLeft);
        const ActionRef fullscreen = action(WindowAction::Fullscreen);
        if (ref.table == steer.table && ref.index == steer.index) {
            CHECK(binding_label_doc(b) == "A/D o Izquierda/Derecha · stick izquierdo");
            CHECK(binding_label_short(b) == "A/D o Izq./Der. · stick izq.");
        }
        if (ref.table == fullscreen.table && ref.index == fullscreen.index) {
            CHECK(keys_text(b) == "F11 o Alt+Intro");
        }
    }
}

TEST_CASE("keys: docs/07 section 8 matches the generated table") {
    const std::string doc = read_text(RALLY_SOURCE_DIR "/docs/07-entorno-y-portabilidad.md");
    REQUIRE_FALSE(doc.empty());
    const std::string begin_marker = "<!-- teclas:";
    const std::string end_marker = "<!-- fin de teclas -->";
    const std::size_t begin = doc.find(begin_marker);
    REQUIRE(begin != std::string::npos);
    const std::size_t content = doc.find('\n', begin);
    const std::size_t end = doc.find(end_marker, content);
    REQUIRE(end != std::string::npos);
    const std::string in_doc = trim_newlines(doc.substr(content, end - content));
    const std::string generated =
        trim_newlines(rally::platform::keys_markdown(rally::platform::key_table()));
    if (in_doc != generated) {
        // Para copiarla al documento.
        MESSAGE("tabla generada:\n", generated);
    }
    CHECK(in_doc == generated);
}
