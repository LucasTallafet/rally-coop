#include "io/atomic_file.h"
#include "platform/player_settings.h"

#include <doctest/doctest.h>

#include <clocale>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

using rally::platform::format_settings;
using rally::platform::kSettingsSaveDelayNs;
using rally::platform::parse_settings;
using rally::platform::PlayerSettings;
using rally::platform::SeatOffsetCm;
using rally::platform::SettingsSaver;
using rally::platform::SettingsWarning;

namespace {

std::string read_all(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

void write_all(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

// Carpeta propia del test, vacía al empezar.
std::filesystem::path scratch(const char* name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "rally_tests" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    return dir;
}

void check_round_trip() {
    PlayerSettings settings;
    settings.seat = SeatOffsetCm{5, -15, 15};
    std::vector<SettingsWarning> warnings;
    CHECK(parse_settings(format_settings(settings), warnings) == settings);
    CHECK(warnings.empty());
}

void check_bad_values() {
    for (const char* bad : {"5,5", "abc", "16", "-16", "", "5 cm", "1e1"}) {
        INFO(bad);
        std::vector<SettingsWarning> warnings;
        const PlayerSettings parsed = parse_settings(
            std::string("# ajustes\nasiento_adelante_cm = 2\nasiento_alto_cm = ") + bad + "\n",
            warnings);
        CHECK(parsed.seat.up == 0);
        CHECK(parsed.seat.forward == 2);
        REQUIRE(warnings.size() == 1);
        CHECK(warnings[0].line == 3);
    }
}

} // namespace

TEST_CASE("player_settings: round trip is exact") { check_round_trip(); }

TEST_CASE("player_settings: an unknown key warns with its line and the rest is read") {
    std::vector<SettingsWarning> warnings;
    const PlayerSettings parsed = parse_settings(
        "asiento_alto_cm = 5\n\nvolante = 900   # de otro juego\nasiento_derecha_cm=-2\n",
        warnings);
    CHECK(parsed.seat == SeatOffsetCm{5, 0, -2});
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].line == 3);
    CHECK(warnings[0].message.find("volante") != std::string::npos);
}

TEST_CASE("player_settings: a badly written or out of range number warns and uses the default") {
    check_bad_values();
}

TEST_CASE("player_settings: a repeated key keeps the first and a line without = warns") {
    std::vector<SettingsWarning> warnings;
    const PlayerSettings parsed =
        parse_settings("asiento_alto_cm = 4\r\nasiento_alto_cm = 9\r\nasiento\r\n", warnings);
    CHECK(parsed.seat.up == 4);
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0].line == 2);
    CHECK(warnings[1].line == 3);
}

TEST_CASE("player_settings: missing keys and empty file give the defaults without warnings") {
    std::vector<SettingsWarning> warnings;
    CHECK(parse_settings("", warnings) == PlayerSettings{});
    CHECK(parse_settings("# solo un comentario\n", warnings) == PlayerSettings{});
    CHECK(warnings.empty());
}

TEST_CASE("player_settings: same results with the process locale in Spanish") {
    const std::string previous_c = std::setlocale(LC_ALL, nullptr);
    const std::locale previous = std::locale::global(std::locale::classic());
    const char* spanish = std::setlocale(LC_ALL, "es-ES");
    if (spanish == nullptr) {
        spanish = std::setlocale(LC_ALL, "Spanish_Spain.1252");
    }
    REQUIRE(spanish != nullptr);
    try {
        std::locale::global(std::locale(spanish));
    } catch (...) {
        // Sin el locale de C++, basta con el de C.
    }
    check_round_trip();
    check_bad_values();
    std::locale::global(previous);
    std::setlocale(LC_ALL, previous_c.c_str());
}

TEST_CASE("player_settings: the save waits until the setting is still for 0.5 s") {
    SettingsSaver saver;
    CHECK_FALSE(saver.due(0));
    saver.changed(1'000);
    CHECK_FALSE(saver.due(1'000 + kSettingsSaveDelayNs - 10'000'000)); // 0,49 s
    CHECK(saver.due(1'000 + kSettingsSaveDelayNs));
    saver.saved();
    CHECK_FALSE(saver.dirty());
    CHECK_FALSE(saver.due(10 * kSettingsSaveDelayNs));
}

TEST_CASE("player_settings: holding a key for 2 s saves once, 0.5 s after the last change") {
    SettingsSaver saver;
    int saves = 0;
    std::int64_t last_change = 0;
    std::int64_t saved_at = -1;
    // Un fotograma cada 1/60 s durante 3 s; un cambio cada 0,1 s durante los 2 primeros.
    for (std::int64_t frame = 0; frame <= 180; ++frame) {
        const std::int64_t now = frame * 1'000'000'000 / 60;
        if (now <= 2'000'000'000 && frame % 6 == 0) {
            saver.changed(now);
            last_change = now;
        }
        if (saver.due(now)) {
            saver.saved();
            ++saves;
            saved_at = now;
        }
    }
    CHECK(saves == 1);
    CHECK(saved_at >= last_change + kSettingsSaveDelayNs);
    CHECK(saved_at < last_change + kSettingsSaveDelayNs + 1'000'000'000 / 60 + 1);
}

TEST_CASE("atomic_file: the new content replaces the old one") {
    const std::filesystem::path dir = scratch("atomic_ok");
    const std::filesystem::path path = dir / "ajustes.txt";
    write_all(path, "viejo\n");
    std::string error;
    CHECK(rally::io::write_file_atomic(path, "nuevo\n", error));
    CHECK(read_all(path) == "nuevo\n");
    CHECK_FALSE(std::filesystem::exists(dir / "ajustes.txt.tmp"));
    // También sin archivo anterior.
    const std::filesystem::path fresh = dir / "otro.txt";
    CHECK(rally::io::write_file_atomic(fresh, "uno\n", error));
    CHECK(read_all(fresh) == "uno\n");
}

TEST_CASE("atomic_file: the previous file stays whole if the temporary cannot be written") {
    const std::filesystem::path dir = scratch("atomic_fail");
    const std::filesystem::path path = dir / "ajustes.txt";
    const std::string old_text = format_settings(PlayerSettings{SeatOffsetCm{5, 0, 0}});
    write_all(path, old_text);
    // Un directorio con el nombre del temporal: no se puede abrir para escribir.
    std::filesystem::create_directory(dir / "ajustes.txt.tmp");
    std::string error;
    CHECK_FALSE(rally::io::write_file_atomic(path, "asiento_alto_cm = 9\n", error));
    CHECK_FALSE(error.empty());
    CHECK(read_all(path) == old_text);
    CHECK(std::filesystem::is_directory(dir / "ajustes.txt.tmp")); // no borra lo que no creó
}

TEST_CASE("player_settings: steering keys default to assist on, keyboard and 400 px") {
    const PlayerSettings defaults;
    CHECK(defaults.steer_assist);
    CHECK(defaults.steer_device == rally::platform::SteerDevice::Keyboard);
    CHECK(defaults.mouse_full_lock_px == 400);
    // Un archivo de la 2.4b (solo el asiento) da los valores por defecto sin avisos.
    std::vector<SettingsWarning> warnings;
    const PlayerSettings old = parse_settings("asiento_alto_cm = 3\n", warnings);
    CHECK(warnings.empty());
    CHECK(old.seat.up == 3);
    CHECK(old.steer_assist);
    CHECK(old.steer_device == rally::platform::SteerDevice::Keyboard);
}

TEST_CASE("player_settings: steering keys round-trip and read their words") {
    PlayerSettings settings;
    settings.steer_assist = false;
    settings.steer_device = rally::platform::SteerDevice::Mouse;
    settings.mouse_full_lock_px = 650;
    std::vector<SettingsWarning> warnings;
    const std::string text = format_settings(settings);
    CHECK(text.find("dispositivo_direccion = raton\n") != std::string::npos);
    CHECK(text.find("ayuda_direccion = 0\n") != std::string::npos);
    CHECK(parse_settings(text, warnings) == settings);
    CHECK(warnings.empty());
}

TEST_CASE("player_settings: a bad steering value warns with its line and keeps the default") {
    std::vector<SettingsWarning> warnings;
    const PlayerSettings parsed = parse_settings("dispositivo_direccion = mando\n"
                                                 "ayuda_direccion = 2\n"
                                                 "raton_px_tope = 20\n"
                                                 "raton_px_tope = 900\n",
                                                 warnings);
    CHECK(parsed == PlayerSettings{});
    REQUIRE(warnings.size() == 4);
    CHECK(warnings[0].line == 1);
    CHECK(warnings[0].message.find("teclado") != std::string::npos);
    CHECK(warnings[1].line == 2);
    CHECK(warnings[2].line == 3);
    CHECK(warnings[2].message.find("400 px") != std::string::npos);
    CHECK(warnings[3].line == 4); // repetida: vale la primera (que era mala)
}
