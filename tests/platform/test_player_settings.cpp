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

TEST_CASE("player_settings: head motion and volume are read, written and cycled") {
    std::vector<SettingsWarning> warnings;
    PlayerSettings settings = parse_settings("cabeza = 0\nvolumen_pct = 40\n", warnings);
    CHECK(warnings.empty());
    CHECK_FALSE(settings.head_motion);
    CHECK(settings.volume_pct == 40);
    CHECK(parse_settings(format_settings(settings), warnings) == settings);
    // Por defecto: con cabeza y al 100 %.
    CHECK(PlayerSettings{}.head_motion);
    CHECK(PlayerSettings{}.volume_pct == 100);
    // Fuera de rango: aviso y el valor por defecto.
    settings = parse_settings("volumen_pct = 140\n", warnings);
    CHECK(warnings.size() == 1);
    CHECK(settings.volume_pct == 100);
    // F4: 100 → 70 → 40 → 0 → 100; desde 55 (escrito a mano), 40.
    CHECK(rally::platform::next_volume_pct(100) == 70);
    CHECK(rally::platform::next_volume_pct(70) == 40);
    CHECK(rally::platform::next_volume_pct(40) == 0);
    CHECK(rally::platform::next_volume_pct(0) == 100);
    CHECK(rally::platform::next_volume_pct(55) == 40);
    // No van en la cabecera de la grabación.
    CHECK(rally::platform::recording_settings_text(settings).find("cabeza") == std::string::npos);
}

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

TEST_CASE("player settings: steer sensitivity reads, writes and defaults to 100") {
    std::vector<SettingsWarning> warnings;
    // Falta: 100, sin avisos.
    const PlayerSettings missing = parse_settings("asiento_alto_cm = 2\n", warnings);
    CHECK(missing.steer_sensitivity_pct == 100);
    CHECK(warnings.empty());
    // Se lee y vuelve (130 no es la de por defecto).
    const PlayerSettings parsed = parse_settings("sensibilidad_direccion = 130\n", warnings);
    CHECK(parsed.steer_sensitivity_pct == 130);
    CHECK(warnings.empty());
    PlayerSettings settings;
    settings.steer_sensitivity_pct = 130;
    CHECK(parse_settings(format_settings(settings), warnings) == settings);
    CHECK(warnings.empty());
    CHECK(format_settings(settings).find("sensibilidad_direccion = 130\n") != std::string::npos);
    // Los dos extremos valen.
    CHECK(parse_settings("sensibilidad_direccion = 50\n", warnings).steer_sensitivity_pct == 50);
    CHECK(parse_settings("sensibilidad_direccion = 150\n", warnings).steer_sensitivity_pct == 150);
    CHECK(warnings.empty());
}

TEST_CASE("player settings: steer sensitivity out of range warns with its line") {
    for (const char* bad : {"49", "151", "0", "-100", "abc", ""}) {
        INFO(bad);
        std::vector<SettingsWarning> warnings;
        const PlayerSettings parsed = parse_settings(
            std::string("# ajustes\nsensibilidad_direccion = ") + bad + "\n", warnings);
        CHECK(parsed.steer_sensitivity_pct == 100);
        REQUIRE(warnings.size() == 1);
        CHECK(warnings[0].line == 2);
        CHECK(warnings[0].message.find("sensibilidad_direccion") != std::string::npos);
        CHECK(warnings[0].message.find("100 %") != std::string::npos);
    }
}

TEST_CASE("player settings: the recording header carries the steer sensitivity") {
    PlayerSettings settings;
    settings.steer_sensitivity_pct = 70;
    const std::string text = rally::platform::recording_settings_text(settings);
    CHECK(text.find("sensibilidad_direccion = 70\n") != std::string::npos);
    // Y se lee con el mismo lector que ajustes.txt.
    std::vector<SettingsWarning> warnings;
    CHECK(parse_settings(text, warnings).steer_sensitivity_pct == 70);
    CHECK(warnings.empty());
}

TEST_CASE("player settings: next_sensitivity_pct steps by 10 and stops at 50 and 150") {
    using rally::platform::next_sensitivity_pct;
    CHECK(next_sensitivity_pct(100, 1) == 110);
    CHECK(next_sensitivity_pct(100, -1) == 90);
    CHECK(next_sensitivity_pct(75, 1) == 80);
    CHECK(next_sensitivity_pct(75, -1) == 70);
    CHECK(next_sensitivity_pct(130, 1) == 140);
    CHECK(next_sensitivity_pct(150, 1) == 150);
    CHECK(next_sensitivity_pct(50, -1) == 50);
    CHECK(next_sensitivity_pct(55, -1) == 50);
    CHECK(next_sensitivity_pct(145, 1) == 150);
    // De 50 a 150 en 10 pasos, y vuelta.
    int pct = 50;
    for (int i = 0; i < 10; ++i) {
        pct = next_sensitivity_pct(pct, 1);
    }
    CHECK(pct == 150);
    for (int i = 0; i < 10; ++i) {
        pct = next_sensitivity_pct(pct, -1);
    }
    CHECK(pct == 50);
}

TEST_CASE("player_settings: ABS, traction control and suspension default, read, write and warn") {
    using rally::sim::SuspensionSetting;
    using rally::sim::TractionControl;
    std::vector<SettingsWarning> warnings;
    // Faltan (un ajustes.txt de antes de la 2.8): ABS activado, control no, suspensión media.
    const PlayerSettings old = parse_settings("asiento_alto_cm = 3\n", warnings);
    CHECK(warnings.empty());
    CHECK(old.abs);
    CHECK(old.traction == TractionControl::Off);
    CHECK(old.suspension == SuspensionSetting::Medium);
    // Se leen y vuelven exactos.
    PlayerSettings parsed =
        parse_settings("abs = 0\ncontrol_traccion = alto\nsuspension = blanda\n", warnings);
    CHECK(warnings.empty());
    CHECK_FALSE(parsed.abs);
    CHECK(parsed.traction == TractionControl::High);
    CHECK(parsed.suspension == SuspensionSetting::Soft);
    const std::string text = format_settings(parsed);
    CHECK(text.find("abs = 0\n") != std::string::npos);
    CHECK(text.find("control_traccion = alto\n") != std::string::npos);
    CHECK(text.find("suspension = blanda\n") != std::string::npos);
    CHECK(parse_settings(text, warnings) == parsed);
    CHECK(warnings.empty());
    CHECK(parse_settings("control_traccion = bajo\nsuspension = dura\n", warnings).traction ==
          TractionControl::Low);
    // Valores malos: aviso con la línea y el valor por defecto.
    parsed = parse_settings("abs = 2\ncontrol_traccion = medio\nsuspension = rigida\n", warnings);
    CHECK(parsed == PlayerSettings{});
    REQUIRE(warnings.size() == 3);
    CHECK(warnings[0].line == 1);
    CHECK(warnings[1].line == 2);
    CHECK(warnings[1].message == "control_traccion: 'medio' no es no, bajo ni alto (se usa no)");
    CHECK(warnings[2].line == 3);
    CHECK(warnings[2].message.find("blanda, media ni dura") != std::string::npos);
    // El aviso de dos palabras sigue igual.
    warnings.clear();
    parse_settings("dispositivo_direccion = mando\n", warnings);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].message.find("no es teclado ni raton") != std::string::npos);
}

TEST_CASE("player_settings: keys 2 and 3 cycle and the settings become the world's aids") {
    using rally::sim::GearboxMode;
    using rally::sim::SuspensionSetting;
    using rally::sim::TractionControl;
    CHECK(rally::platform::next_traction(TractionControl::Off) == TractionControl::Low);
    CHECK(rally::platform::next_traction(TractionControl::Low) == TractionControl::High);
    CHECK(rally::platform::next_traction(TractionControl::High) == TractionControl::Off);
    CHECK(rally::platform::next_suspension(SuspensionSetting::Soft) == SuspensionSetting::Medium);
    CHECK(rally::platform::next_suspension(SuspensionSetting::Medium) == SuspensionSetting::Hard);
    CHECK(rally::platform::next_suspension(SuspensionSetting::Hard) == SuspensionSetting::Soft);
    // Los cinco campos de DriverAids.
    const rally::sim::DriverAids defaults = rally::platform::driver_aids(PlayerSettings{});
    CHECK(defaults.steer_assist);
    CHECK(defaults.gearbox == GearboxMode::Automatic);
    CHECK(defaults.abs);
    CHECK(defaults.traction == TractionControl::Off);
    CHECK(defaults.suspension == SuspensionSetting::Medium);
    PlayerSettings custom;
    custom.steer_assist = false;
    custom.sequential = true;
    custom.abs = false;
    custom.traction = TractionControl::High;
    custom.suspension = SuspensionSetting::Hard;
    const rally::sim::DriverAids aids = rally::platform::driver_aids(custom);
    CHECK_FALSE(aids.steer_assist);
    CHECK(aids.gearbox == GearboxMode::Sequential);
    CHECK_FALSE(aids.abs);
    CHECK(aids.traction == TractionControl::High);
    CHECK(aids.suspension == SuspensionSetting::Hard);
    // La línea de F3.
    CHECK(rally::platform::aids_summary(PlayerSettings{}) ==
          "ayudas: dirección sí, ABS sí, control no, suspensión media");
    CHECK(rally::platform::aids_summary(custom) ==
          "ayudas: dirección no, ABS no, control alto, suspensión dura");
}
