#include "platform/copilot_args.h"
#include "platform/copilot_keys.h"
#include "platform/keys.h"
#include "platform/player_settings.h"
#include "platform/ui_text.h"
#include "platform/wheel_spin.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace rally::platform;

namespace {

// Lee argumentos como los da main: argv[0] es el programa.
bool parse(std::vector<const char*> args, CopilotArgs& out, std::string& error) {
    args.insert(args.begin(), "rally.exe");
    return parse_copilot_args(static_cast<int>(args.size()), const_cast<char**>(args.data()), out,
                              error);
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

} // namespace

TEST_CASE("copilot args: no flag means the normal game") {
    CopilotArgs args;
    std::string error;
    CHECK(parse({}, args, error));
    CHECK_FALSE(args.enabled);
    CHECK(parse({"--car", "x.car"}, args, error));
    CHECK_FALSE(args.enabled);
}

TEST_CASE("copilot args: --copiloto alone, with an address and with address and port") {
    CopilotArgs args;
    std::string error;
    REQUIRE(parse({"--copiloto"}, args, error));
    CHECK(args.enabled);
    CHECK(args.address == "127.0.0.1");
    CHECK(args.port == 29470);

    REQUIRE(parse({"--copiloto", "127.0.0.1"}, args, error));
    CHECK(args.enabled);
    CHECK(args.address == "127.0.0.1");
    CHECK(args.port == 29470);

    REQUIRE(parse({"--copiloto", "127.0.0.1:29471"}, args, error));
    CHECK(args.enabled);
    CHECK(args.address == "127.0.0.1");
    CHECK(args.port == 29471);

    // Otra opción detrás no es la dirección.
    REQUIRE(parse({"--copiloto", "--frame-log"}, args, error));
    CHECK(args.enabled);
    CHECK(args.port == 29470);
}

TEST_CASE("copilot args: a badly written one gives its error") {
    CopilotArgs args;
    std::string error;
    for (const char* bad : {"127.0.0.1:abc", "127.0.0.1:0", "127.0.0.1:70000", "127.0.0.1:",
                            "127.0.0.1:1:2", "localhost", "1.2.3", "300.1.1.1", ":29470"}) {
        INFO(bad);
        error.clear();
        CHECK_FALSE(parse({"--copiloto", bad}, args, error));
        CHECK_FALSE(error.empty());
        CHECK(error.find(bad) != std::string::npos);
    }
    CHECK_FALSE(parse({"--copiloto", "--copiloto"}, args, error));
    CHECK(parse({"--copiloto", "127.0.0.1:29471", "--copiloto"}, args, error) == false);
}

TEST_CASE("copilot settings: the copilot instance never writes ajustes.txt, the host does") {
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / "rally_copilot_settings_test";
    fs::create_directories(folder);
    const fs::path file = folder / "ajustes.txt";
    const std::string path = file.string();
    const std::string original = "# ajustes del conductor\nabs = no\n";
    {
        std::ofstream out(file, std::ios::binary);
        out << original;
    }
    PlayerSettings changed;
    changed.abs = true;
    changed.volume_pct = 40;
    std::string error = "no vacío";

    // Copiloto: no escribe nada, ni con otros ajustes ni sin ellos; sin error.
    CHECK_FALSE(save_settings_file(path, changed, InstanceRole::Copilot, error));
    CHECK(error.empty());
    CHECK(read_file(file) == original);
    CHECK_FALSE(fs::exists(folder / "ajustes.txt.tmp"));

    // Juego: sí.
    CHECK(save_settings_file(path, changed, InstanceRole::Host, error));
    CHECK(error.empty());
    const std::string written = read_file(file);
    CHECK(written != original);
    CHECK(written == format_settings(changed));

    // Y con el copiloto, otra vez, no lo toca.
    CHECK_FALSE(save_settings_file(path, PlayerSettings{}, InstanceRole::Copilot, error));
    CHECK(read_file(file) == written);
    fs::remove_all(folder);
}

TEST_CASE("copilot keys: Z is free in every context and only fires in the copilot's") {
    const ActionRef z = action(CopilotAction::SimulatedNet);
    CHECK(chords_for(z)[0].key == Key::Z);
    CHECK(contexts_for(z) == kContextCopilot);
    CHECK((contexts_for(z) & kContextAll) == 0);
    CHECK((contexts_for(z) & kCopilotKeyContext) != 0);
    CHECK(count_key_conflicts(key_table()) == 0);
    // Ninguna otra fila usa Z.
    int uses = 0;
    for (const KeyBinding& b : key_table()) {
        for (const auto* keys : {&b.keys, &b.pair_keys}) {
            for (const KeyChord& chord : *keys) {
                uses += chord.key == Key::Z ? 1 : 0;
            }
        }
    }
    CHECK(uses == 1);
}

TEST_CASE("copilot keys: the copilot help shows only its seven keys") {
    const std::span<const KeyBinding> table = copilot_help_table();
    REQUIRE(table.size() == 7);
    std::vector<Key> keys;
    for (const KeyBinding& b : table) {
        keys.push_back(b.keys[0].key);
        CHECK((b.contexts & kCopilotKeyContext) != 0);
        CHECK(in_default_font(binding_label_short(b)));
        CHECK(in_default_font(b.text));
    }
    for (const Key key : {Key::Z, Key::Tab, Key::F1, Key::F3, Key::H, Key::F11, Key::Escape}) {
        CHECK(std::count(keys.begin(), keys.end(), key) == 1);
    }
}

TEST_CASE("copilot keys: Z cycles the simulated network no, media, mala and back") {
    SimulatedNet level = SimulatedNet::Off;
    level = next_simulated_net(level);
    CHECK(level == SimulatedNet::Medium);
    level = next_simulated_net(level);
    CHECK(level == SimulatedNet::Bad);
    level = next_simulated_net(level);
    CHECK(level == SimulatedNet::Off);

    const rally::net::Impairment none = simulated_net_params(SimulatedNet::Off);
    CHECK(none.delay_s == 0.0);
    CHECK(none.jitter_s == 0.0);
    CHECK(none.loss == 0.0);
    const rally::net::Impairment medium = simulated_net_params(SimulatedNet::Medium);
    CHECK(medium.delay_s == doctest::Approx(0.100));
    CHECK(medium.jitter_s == doctest::Approx(0.030));
    CHECK(medium.loss == doctest::Approx(0.05));
    const rally::net::Impairment bad = simulated_net_params(SimulatedNet::Bad);
    CHECK(bad.delay_s == doctest::Approx(0.250));
    CHECK(bad.jitter_s == doctest::Approx(0.080));
    CHECK(bad.loss == doctest::Approx(0.20));
    for (const SimulatedNet n : {SimulatedNet::Off, SimulatedNet::Medium, SimulatedNet::Bad}) {
        CHECK(in_default_font(simulated_net_name(n)));
    }
}

TEST_CASE("wheel spin: the visual spin follows the advance and ignores jumps") {
    CHECK(wheel_spin_delta(1.0, 0.3) == doctest::Approx(1.0 / 0.3));
    CHECK(wheel_spin_delta(-1.0, 0.3) == doctest::Approx(-1.0 / 0.3));
    CHECK(wheel_spin_delta(0.0, 0.3) == 0.0);
    // Un recolocado o un mundo nuevo (salto de más de 5 m) no hace girar las ruedas.
    CHECK(wheel_spin_delta(40.0, 0.3) == 0.0);
    CHECK(wheel_spin_delta(-6.0, 0.3) == 0.0);
    CHECK(wheel_spin_delta(1.0, 0.0) == 0.0);
    // El giro se envuelve en (-π, π].
    CHECK(wrap_angle(3.5) == doctest::Approx(3.5 - 6.283185307179586));
    CHECK(wrap_angle(-3.5) == doctest::Approx(-3.5 + 6.283185307179586));
    // En reposo, el centro de la rueda a su radio del suelo: anclaje menos radio.
    CHECK(rest_suspension_length(0.55, 0.32) == doctest::Approx(0.23));
}
