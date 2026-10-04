#include "platform/player_settings.h"

#include <array>
#include <charconv>
#include <system_error>

namespace rally::platform {

namespace {

// Claves enteras, todas del asiento por ahora.
struct IntKey {
    std::string_view name;
    int SeatOffsetCm::* seat_member;
    int min;
    int max;
    const char* unit;
};

constexpr std::array kIntKeys = {
    IntKey{"asiento_alto_cm", &SeatOffsetCm::up, -kSeatLimitCm, kSeatLimitCm, " cm"},
    IntKey{"asiento_adelante_cm", &SeatOffsetCm::forward, -kSeatLimitCm, kSeatLimitCm, " cm"},
    IntKey{"asiento_derecha_cm", &SeatOffsetCm::right, -kSeatLimitCm, kSeatLimitCm, " cm"},
};

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
    std::array<bool, kIntKeys.size()> seen{};
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
        while (index < kIntKeys.size() && kIntKeys[index].name != key) {
            ++index;
        }
        if (index == kIntKeys.size()) {
            warnings.push_back({line_number, "clave desconocida " + quoted(key) + " (se ignora)"});
            continue;
        }
        const IntKey& spec = kIntKeys[index];
        if (seen[index]) {
            warnings.push_back(
                {line_number, std::string(key) + " repetida (vale la primera; se ignora esta)"});
            continue;
        }
        seen[index] = true;
        int parsed = 0;
        const std::from_chars_result result =
            std::from_chars(value.data(), value.data() + value.size(), parsed);
        const std::string fallback =
            " (se usa " + std::to_string(settings.seat.*spec.seat_member) + spec.unit + ")";
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
        settings.seat.*spec.seat_member = parsed;
    }
    return settings;
}

std::string format_settings(const PlayerSettings& settings) {
    std::string out = "# Ajustes del jugador de Rally Coop. El juego los guarda solo; a mano, con "
                      "el juego cerrado.\n"
                      "# Asiento: desplazamiento del ojo del conductor en cm, de -15 a 15 "
                      "(alto: arriba; adelante: hacia delante; derecha: hacia la derecha).\n";
    for (const IntKey& spec : kIntKeys) {
        std::array<char, 16> buffer{};
        const std::to_chars_result result = std::to_chars(
            buffer.data(), buffer.data() + buffer.size(), settings.seat.*spec.seat_member);
        out += spec.name;
        out += " = ";
        out.append(buffer.data(), result.ptr);
        out += '\n';
    }
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
