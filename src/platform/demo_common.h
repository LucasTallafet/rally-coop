#pragma once

#include <raylib.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

// Lo que usan varias partes de la demo (platform/drive_demo): colores del texto, duraciones de
// los mensajes y pequeñas funciones de rutas y tiempo. Solo para los archivos demo_*.cpp.
namespace rally::platform::demo {

// Texto en pantalla.
inline constexpr Color kText{220, 223, 228, 255};
inline constexpr Color kDim{150, 156, 168, 255};
inline constexpr Color kTitle{240, 200, 120, 255};
inline constexpr Color kStrong{250, 200, 90, 255};
inline constexpr Color kError{255, 110, 90, 255};
inline constexpr Color kUnavailable{105, 110, 120, 255}; // ayuda: teclas que no valen aquí
inline constexpr Color kPanelBackground{20, 22, 26, 215};
// F3, más transparente (la mitad de opacidad; valor de partida, lo juzga Lucas).
inline constexpr Color kDebugBackground{20, 22, 26, 107};
// Duración de los mensajes que no son errores persistentes.
inline constexpr std::int64_t kShortMessageNs = 5'000'000'000;
inline constexpr std::int64_t kSettingsMessageNs = 10'000'000'000;

inline std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Ruta UTF-8 (raylib da la carpeta del exe en UTF-8).
inline std::filesystem::path utf8_path(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

inline std::string file_name(const std::string& path) {
    const std::u8string name = utf8_path(path).filename().u8string();
    return std::string(name.begin(), name.end());
}

} // namespace rally::platform::demo
