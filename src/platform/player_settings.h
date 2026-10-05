#pragma once

#include "platform/seat.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rally::platform {

// Ajustes del jugador: ajustes.txt junto al exe (lo crea la 2.4b; la 2.5b y la 2.8 añaden
// claves). Texto `clave = valor`, comentarios con '#', enteros con std::from_chars (no depende
// del idioma de Windows) o, en las claves con nombres, una palabra de su lista. Es un archivo del
// jugador, no un dato de la simulación: una clave desconocida, repetida o un valor mal escrito o
// fuera de su rango no abortan; dan un aviso con la línea y la clave se queda con su valor por
// defecto (la repetida, con el primero). Una clave que falta toma su valor por defecto sin avisar.
// Dispositivo de dirección en PC (tarea 2.5b). El mando, si hay uno conectado, se mezcla igual con
// los dos. No es de la simulación: la simulación solo ve el modo del eje (digital o analógico).
enum class SteerDevice : int { Keyboard = 0, Mouse = 1 };

// Píxeles de desplazamiento horizontal del ratón para llevar el volante al tope: de partida, 400
// (se ajusta en la 2.10; la 2.8 añade la sensibilidad en pantalla).
inline constexpr int kMouseFullLockPxDefault = 400;
inline constexpr int kMouseFullLockPxMin = 50;
inline constexpr int kMouseFullLockPxMax = 5000;

struct PlayerSettings {
    SeatOffsetCm seat;
    // Ayuda de dirección (D1: activada por defecto). Cambia la simulación: va en la descripción
    // del mundo y cambiarla lo reconstruye.
    bool steer_assist = true;
    SteerDevice steer_device = SteerDevice::Keyboard;
    int mouse_full_lock_px = kMouseFullLockPxDefault;
    bool operator==(const PlayerSettings&) const = default;
};

struct SettingsWarning {
    int line = 0; // 1 la primera
    std::string message;
};

inline constexpr std::string_view kSettingsFileName = "ajustes.txt";

PlayerSettings parse_settings(std::string_view text, std::vector<SettingsWarning>& warnings);
// Ajustes de la plataforma que van en la cabecera de la grabación (RecordingHeader::settings):
// los de la dirección que no son de la simulación (dispositivo y sensibilidad del ratón), en el
// mismo formato que ajustes.txt. La ayuda va en la descripción del mundo, no aquí.
std::string recording_settings_text(const PlayerSettings& settings);
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
