#pragma once

#include "platform/seat.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rally::platform {

// Ajustes del jugador: ajustes.txt junto al exe (lo crea la 2.4b; la 2.5b y la 2.8 añaden
// claves). Texto `clave = valor`, comentarios con '#', enteros con std::from_chars (no depende
// del idioma de Windows). Es un archivo del jugador, no un dato de la simulación: una clave
// desconocida, repetida o un valor mal escrito o fuera de su rango no abortan; dan un aviso con
// la línea y la clave se queda con su valor por defecto (la repetida, con el primero). Una clave
// que falta toma su valor por defecto sin avisar.
struct PlayerSettings {
    SeatOffsetCm seat;
    bool operator==(const PlayerSettings&) const = default;
};

struct SettingsWarning {
    int line = 0; // 1 la primera
    std::string message;
};

inline constexpr std::string_view kSettingsFileName = "ajustes.txt";

PlayerSettings parse_settings(std::string_view text, std::vector<SettingsWarning>& warnings);
std::string format_settings(const PlayerSettings& settings);

// Cuándo guardar: tras kSettingsSaveDelayNs sin cambios, para no escribir en cada repetición de
// una tecla mantenida. Al salir se guarda si queda algo pendiente (dirty). Tiempos en ns de
// cualquier reloj monótono.
inline constexpr std::int64_t kSettingsSaveDelayNs = 500'000'000;

class SettingsSaver {
public:
    void changed(std::int64_t now_ns);
    // Hay un cambio sin guardar y lleva kSettingsSaveDelayNs quieto.
    bool due(std::int64_t now_ns) const;
    bool dirty() const { return dirty_; }
    // Tras intentar guardar (bien o mal): un fallo no se reintenta hasta el siguiente cambio.
    void saved() { dirty_ = false; }

private:
    bool dirty_ = false;
    std::int64_t last_change_ns_ = 0;
};

} // namespace rally::platform
