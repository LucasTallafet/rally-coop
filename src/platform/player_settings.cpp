#include "platform/player_settings.h"

#include <array>
#include <charconv>
#include <system_error>

namespace rally::platform {

namespace {

// Una clave del archivo. Enteras en [min, max] o, con `words`, una palabra de la lista (su índice
// es el valor). Se leen y se escriben a través de get y set.
struct Key {
    std::string_view name;
    int (*get)(const PlayerSettings&);
    void (*set)(PlayerSettings&, int);
    int min;
    int max;
    const char* unit;
    std::array<std::string_view, 2> words{}; // vacías: entera
};

constexpr std::array kKeys = {
    Key{"asiento_alto_cm", [](const PlayerSettings& p) { return p.seat.up; },
        [](PlayerSettings& p, int v) { p.seat.up = v; }, -kSeatLimitCm, kSeatLimitCm, " cm"},
    Key{"asiento_adelante_cm", [](const PlayerSettings& p) { return p.seat.forward; },
        [](PlayerSettings& p, int v) { p.seat.forward = v; }, -kSeatLimitCm, kSeatLimitCm, " cm"},
    Key{"asiento_derecha_cm", [](const PlayerSettings& p) { return p.seat.right; },
        [](PlayerSettings& p, int v) { p.seat.right = v; }, -kSeatLimitCm, kSeatLimitCm, " cm"},
    Key{"ayuda_direccion", [](const PlayerSettings& p) { return p.steer_assist ? 1 : 0; },
        [](PlayerSettings& p, int v) { p.steer_assist = v != 0; }, 0, 1, ""},
    Key{"dispositivo_direccion",
        [](const PlayerSettings& p) { return static_cast<int>(p.steer_device); },
        [](PlayerSettings& p, int v) { p.steer_device = static_cast<SteerDevice>(v); },
        0,
        1,
        "",
        {"teclado", "raton"}},
    Key{"caja",
        [](const PlayerSettings& p) { return p.sequential ? 1 : 0; },
        [](PlayerSettings& p, int v) { p.sequential = v != 0; },
        0,
        1,
        "",
        {"automatica", "secuencial"}},
    Key{"raton_px_tope", [](const PlayerSettings& p) { return p.mouse_full_lock_px; },
        [](PlayerSettings& p, int v) { p.mouse_full_lock_px = v; }, kMouseFullLockPxMin,
        kMouseFullLockPxMax, " px"},
};

bool has_words(const Key& key) { return !key.words[0].empty(); }

std::string value_text(const Key& key, int value) {
    if (has_words(key)) {
        return std::string(key.words[static_cast<std::size_t>(value)]);
    }
    return std::to_string(value) + key.unit;
}

std::string_view trim(std::string_view s) {
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (!s.empty() && space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

std::string quoted(std::string_view s) { return "'" + std::string(s) + "'"; }

} // namespace

PlayerSettings parse_settings(std::string_view text, std::vector<SettingsWarning>& warnings) {
    PlayerSettings settings;
    std::array<bool, kKeys.size()> seen{};
    int line_number = 0;
    while (!text.empty()) {
        ++line_number;
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            warnings.push_back({line_number, "falta '=' en " + quoted(line) + " (se ignora)"});
            continue;
        }
        const std::string_view key = trim(line.substr(0, equals));
        const std::string_view value = trim(line.substr(equals + 1));
        std::size_t index = 0;
        while (index < kKeys.size() && kKeys[index].name != key) {
            ++index;
        }
        if (index == kKeys.size()) {
            warnings.push_back({line_number, "clave desconocida " + quoted(key) + " (se ignora)"});
            continue;
        }
        const Key& spec = kKeys[index];
        if (seen[index]) {
            warnings.push_back(
                {line_number, std::string(key) + " repetida (vale la primera; se ignora esta)"});
            continue;
        }
        seen[index] = true;
        const std::string fallback = " (se usa " + value_text(spec, spec.get(settings)) + ")";
        if (has_words(spec)) {
            std::size_t word = 0;
            while (word < spec.words.size() && spec.words[word] != value) {
                ++word;
            }
            if (word == spec.words.size()) {
                warnings.push_back({line_number, std::string(key) + ": " + quoted(value) +
                                                     " no es " + std::string(spec.words[0]) +
                                                     " ni " + std::string(spec.words[1]) +
                                                     fallback});
                continue;
            }
            spec.set(settings, static_cast<int>(word));
            continue;
        }
        int parsed = 0;
        const std::from_chars_result result =
            std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (value.empty() || result.ec != std::errc{} ||
            result.ptr != value.data() + value.size()) {
            warnings.push_back({line_number, std::string(key) + ": " + quoted(value) +
                                                 " no es un número entero" + fallback});
            continue;
        }
        if (parsed < spec.min || parsed > spec.max) {
            warnings.push_back({line_number, std::string(key) + ": " + std::to_string(parsed) +
                                                 " fuera de " + std::to_string(spec.min) + ".." +
                                                 std::to_string(spec.max) + fallback});
            continue;
        }
        spec.set(settings, parsed);
    }
    return settings;
}

std::string format_settings(const PlayerSettings& settings) {
    std::string out = "# Ajustes del jugador de Rally Coop. El juego los guarda solo; a mano, con "
                      "el juego cerrado.\n"
                      "# Asiento: desplazamiento del ojo del conductor en cm, de -15 a 15 "
                      "(alto: arriba; adelante: hacia delante; derecha: hacia la derecha).\n"
                      "# Dirección: ayuda_direccion 1 o 0 (F6); dispositivo_direccion teclado o "
                      "raton (F7); raton_px_tope, píxeles del ratón para el tope, de 50 a 5000.\n"
                      "# Caja: caja automatica o secuencial (F2).\n";
    for (const Key& spec : kKeys) {
        out += spec.name;
        out += " = ";
        const int value = spec.get(settings);
        if (has_words(spec)) {
            out += spec.words[static_cast<std::size_t>(value)];
        } else {
            std::array<char, 16> buffer{};
            const std::to_chars_result result =
                std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
            out.append(buffer.data(), result.ptr);
        }
        out += '\n';
    }
    return out;
}

std::string recording_settings_text(const PlayerSettings& settings) {
    std::string out = "dispositivo_direccion = ";
    out += settings.steer_device == SteerDevice::Mouse ? "raton" : "teclado";
    out += "\nraton_px_tope = " + std::to_string(settings.mouse_full_lock_px) + "\n";
    return out;
}

void SettingsSaver::changed(std::int64_t now_ns) {
    dirty_ = true;
    last_change_ns_ = now_ns;
}

bool SettingsSaver::due(std::int64_t now_ns) const {
    return dirty_ && now_ns - last_change_ns_ >= kSettingsSaveDelayNs;
}

} // namespace rally::platform
