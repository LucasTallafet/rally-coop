#include "platform/keys.h"
#include "platform/ui_text.h"

#include <doctest/doctest.h>

#include <array>
#include <fstream>
#include <sstream>
#include <string>

using rally::platform::Action;
using rally::platform::count_key_conflicts;
using rally::platform::Key;
using rally::platform::KeyBinding;
using rally::platform::KeyBlock;
using rally::platform::KeyChord;
using rally::platform::PadControl;

namespace {

KeyBinding row(Action action, KeyChord a, KeyChord b, std::uint8_t contexts) {
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
    CHECK(rally::platform::chords_for(Action::NextSurface)[0].key == Key::G);
    CHECK(rally::platform::chords_for(Action::NextTrial)[0].key == Key::G);
    CHECK((rally::platform::contexts_for(Action::NextSurface) &
           rally::platform::contexts_for(Action::NextTrial)) == 0);
}

TEST_CASE("keys: a repeated key in overlapping contexts is a conflict") {
    using namespace rally::platform;
    const std::array clash = {row(Action::Pause, plain(Key::P), {}, kContextAll),
                              row(Action::Capture, plain(Key::P), {}, kContextArena)};
    CHECK(count_key_conflicts(clash) == 1);
    const std::array apart = {row(Action::NextSurface, plain(Key::G), {}, kContextZone),
                              row(Action::NextTrial, plain(Key::G), {}, kContextBlind)};
    CHECK(count_key_conflicts(apart) == 0);
}

TEST_CASE("keys: Enter and Alt+Enter are different keys") {
    using namespace rally::platform;
    const std::array both = {
        row(Action::Fullscreen, plain(Key::F11), with_alt(Key::Enter), kContextAll),
        row(Action::Pause, plain(Key::Enter), {}, kContextAll)};
    CHECK(count_key_conflicts(both) == 0);
    const std::array twice = {
        row(Action::Fullscreen, plain(Key::F11), with_alt(Key::Enter), kContextAll),
        row(Action::Pause, with_alt(Key::Enter), {}, kContextZone)};
    CHECK(count_key_conflicts(twice) == 1);
}

TEST_CASE("keys: every action has exactly one row") {
    std::array<int, rally::platform::kActionCount> seen{};
    for (const KeyBinding& b : rally::platform::key_table()) {
        ++seen[static_cast<std::size_t>(b.action)];
        if (b.pair_action != Action::None) {
            ++seen[static_cast<std::size_t>(b.pair_action)];
        }
    }
    CHECK(seen[static_cast<std::size_t>(Action::None)] == 0);
    for (int a = 1; a < rally::platform::kActionCount; ++a) {
        INFO("acción ", a);
        CHECK(seen[static_cast<std::size_t>(a)] == 1);
        CHECK(rally::platform::chords_for(static_cast<Action>(a))[0].key != Key::None);
    }
}

TEST_CASE("keys: the seat has no gamepad control") {
    for (const Action a : {Action::SeatUp, Action::SeatDown, Action::SeatForward, Action::SeatBack,
                           Action::SeatRight, Action::SeatLeft}) {
        CHECK(rally::platform::pad_for(a) == PadControl::None);
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
        if (b.action == Action::SteerLeft) {
            CHECK(binding_label_doc(b) == "A/D o Izquierda/Derecha · stick izquierdo");
        }
        if (b.action == Action::SteerLeft) {
            CHECK(binding_label_short(b) == "A/D o Izq./Der. · stick izq.");
        }
        if (b.action == Action::Fullscreen) {
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
