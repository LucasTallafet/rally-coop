#pragma once

#include "sim/surface.h"
#include "sim/telemetry.h"

#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <string_view>

namespace rally::io {

// Telemetría en CSV: una fila por paso, escrita por rally_replay (la partida solo graba
// entradas). Una sola tabla de columnas genera el CSV y su documentación
// (tools/telemetry/columns.md, rally_replay columns --md): no pueden ir por separado.

inline constexpr int kTelemetryFormatVersion = 1;

enum class ColumnKind : std::uint8_t {
    Integer, // número entero
    Real,    // número con `precision` decimales fijos
    Text,    // texto sin comas
};

struct TelemetryColumn {
    const char* name;
    const char* unit;        // "" si no tiene
    const char* sign;        // convención de signo o rango; "" si no aplica
    const char* description; // en español
    ColumnKind kind;
    int precision;                                 // decimales (solo Real)
    double (*number)(const sim::CarTelemetry&);    // Integer y Real
    const char* (*text)(const sim::CarTelemetry&); // Text
};

std::span<const TelemetryColumn> telemetry_columns();

// Cabecera del CSV: líneas "# clave=valor" (formato, versión de la simulación, nombre de la
// grabación sin ruta y superficie de la pista ciega, "none" sin zona) y la fila de nombres.
std::string telemetry_csv_header(std::string_view recording_name, bool zone,
                                 sim::Surface blind_surface);

// Añade la fila de un paso a `out` (terminada en '\n'). Números con std::to_chars (punto
// decimal, sin depender del idioma) y precisión fija por columna; un valor que redondea a 0 se
// escribe sin signo. No reserva memoria si `out` tiene capacidad para la fila
// (kTelemetryRowMaxBytes).
void append_telemetry_row(const sim::CarTelemetry& telemetry, std::string& out);
inline constexpr std::size_t kTelemetryRowMaxBytes = 2048;

// Tabla de columnas en Markdown, con las convenciones (tools/telemetry/columns.md).
std::string telemetry_columns_markdown();

// Escribe el CSV a un archivo por bloques: las filas se acumulan en un búfer reservado al
// abrir y se vuelcan al pasar de 1 MiB.
class TelemetryCsvWriter {
public:
    TelemetryCsvWriter() = default;
    TelemetryCsvWriter(const TelemetryCsvWriter&) = delete;
    TelemetryCsvWriter& operator=(const TelemetryCsvWriter&) = delete;

    bool open(const std::string& path, std::string_view header, std::string& error);
    void append(const sim::CarTelemetry& telemetry);
    // Vuelca lo que quede y cierra. false si algo no se pudo escribir.
    bool close(std::string& error);
    std::uint64_t bytes_written() const { return bytes_; }

private:
    bool flush();

    std::ofstream file_;
    std::string buffer_;
    std::string path_;
    std::uint64_t bytes_ = 0;
    bool failed_ = false;
};

} // namespace rally::io
