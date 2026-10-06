#pragma once

#include "sim/sim_input.h"
#include "sim/sim_version.h"
#include "sim/sim_world.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rally::io {

// Grabación de entradas por paso (.rrec). Todo en little-endian y escrito campo a campo,
// sin depender del relleno de los structs:
//
//   cabecera  "RALLYREC" (8 B)
//             u32 versión del formato, u32 kSimVersion
//             commit, mundo, texto del coche, ajustes del jugador: u32 longitud + bytes cada uno
//             u64 hash de la zona de pruebas (desde la versión 2; 0 sin zona)
//   cuerpo    u32 número de tramos
//             por tramo (12 B): u32 repeticiones (> 0), i16 steer, i16 throttle, i16 brake,
//             u16 buttons
//   pie       u32 indicadores (bit 0: completa), u64 pasos totales, u64 hash del estado
//             final (SimWorld::state_hash; 0 si incompleta), i64 tiempo del recorrido
//             cronometrado en pasos (la mejor vuelta completa; -1 si no hay), "REND" (4 B)
//
// Una grabación incompleta es la que se guarda al saltar un RALLY_CHECK: lleva los pasos
// terminados hasta entonces y no tiene hash final.
//
// Versiones del formato: 1 (tarea 2.1); 2 (tarea 2.2): la descripción del mundo lleva la zona
// de pruebas y la superficie de la pista ciega, y la cabecera el hash de la zona
// (SimWorld::zone_hash). 3 (tarea 2.4): el coche sustituye a la caja controlada; cada caja lleva
// su giro, su superficie y su h, y la descripción lleva la salida del coche (sus datos van en
// el texto del coche de la cabecera). Se siguen leyendo las de las versiones 1 y 2 (de la
// versión 1 de la simulación, con la caja): se repiten avisando, sin coche y sin comparar el
// hash. 4 (tarea 2.5b): la descripción del mundo lleva al final las ayudas del jugador (u8
// ayuda de dirección) y los ajustes, texto de la plataforma; las de la versión 3 se leen con la
// ayuda desactivada (no existía). 5 (tarea 2.6): detrás de las ayudas, el modo de la caja (u8)
// y la marcha de salida de los tests (i8); las de la versión 4 se leen con el cambio automático
// y la marcha que da la velocidad.

inline constexpr std::uint32_t kRecordingFormatVersion = 5;
inline constexpr std::uint32_t kOldestRecordingFormatVersion = 1;
inline constexpr std::size_t kRunBytes = 12;
inline constexpr std::size_t kFooterBytes = 32;

struct InputRun {
    std::uint32_t count = 0; // pasos seguidos con la misma entrada
    sim::SimInput input;
};

struct RecordingHeader {
    std::uint32_t format_version = kRecordingFormatVersion;
    std::uint32_t sim_version = sim::kSimVersion;
    std::string commit;   // del build que grabó; "-dirty" si tenía cambios sin commit
    std::string world;    // descripción del mundo (serialize_world)
    std::string car_text; // archivo del coche entero (desde la 2.4; vacío sin coche)
    // Ajustes de la plataforma al empezar la grabación, en texto `clave = valor` (desde la
    // 2.5b: dispositivo de dirección y sensibilidad del ratón). No son de la simulación ni entran
    // en el hash: cambiarlos a mitad de pasada no cambia la cabecera. Las ayudas que cambian la
    // simulación van en la descripción del mundo.
    std::string settings;
    // Hash de la zona de pruebas con la que se grabó (SimWorld::zone_hash; 0 sin zona). Si la
    // zona cambia (suelo, h u ondulaciones), la repetición avisa y no compara el hash final.
    std::uint64_t zone_hash = 0;
};

struct RecordingFooter {
    bool complete = false;
    std::uint64_t total_steps = 0;
    std::uint64_t final_hash = 0;
    std::int64_t timed_steps = -1;
};

struct Recording {
    RecordingHeader header;
    std::vector<InputRun> runs;
    RecordingFooter footer;
};

// Bytes de la cabecera serializada, con el u32 del número de tramos.
std::size_t header_bytes(const RecordingHeader& header);

std::string serialize(const Recording& recording);

struct ParseResult {
    bool ok = false;
    std::string error; // si !ok: qué falla y en qué byte
    Recording recording;
};

// Comprueba firma, versión del formato, longitudes, rangos de los ejes, que las
// repeticiones sumen los pasos del pie y que no sobre nada. Nunca lee fuera de `bytes`.
ParseResult parse(std::string_view bytes);

// Descripción del mundo en bytes (u32 cuerpos; por cuerpo u8 movimiento, f64 x, y, z y f32
// medias medidas, como sus bits, y desde la versión 3 f32 giro, u8 superficie y u8 h; hasta
// la versión 2, i32 cuerpo controlado; desde la 3, u8 coche, f64 x, y, z y f32 rumbo y
// velocidad de su salida; desde la 2, u8 zona de pruebas y u8 superficie de la pista ciega;
// desde la 4, u8 ayuda de dirección).
// Se lee con la versión del formato de la grabación de la que sale. Los datos del coche no
// van aquí (RecordingHeader::car_text).
std::string serialize_world(const sim::WorldDesc& desc);
bool parse_world(std::string_view bytes, sim::WorldDesc& desc, std::string& error,
                 std::uint32_t format_version = kRecordingFormatVersion);

// Archivos. Las rutas van en UTF-8. write_file crea la carpeta si falta.
bool read_file(const std::string& path, std::string& bytes, std::string& error);
bool write_file(const std::string& path, std::string_view bytes, std::string& error);

// Pasos de una grabación de `minutes` minutos: lo que el Recorder reserva al empezar.
constexpr std::size_t steps_for_minutes(int minutes) {
    return static_cast<std::size_t>(minutes) * 60u * sim::kStepsPerSecond;
}

// Graba la entrada de cada paso, comprimida por repetición. append() no reserva memoria si
// hay sitio: ensure_capacity() se llama antes de los pasos del fotograma, fuera del paso.
class Recorder {
public:
    // Empieza una grabación nueva con sitio para `reserve_runs` tramos.
    void begin(RecordingHeader header, std::size_t reserve_runs);
    bool active() const { return active_; }

    // Deja sitio para `steps` pasos más (cada uno puede abrir un tramo).
    void ensure_capacity(int steps);
    // Tras cada paso simulado, con su entrada.
    void append(const sim::SimInput& input);

    std::uint64_t steps() const { return recording_.footer.total_steps; }
    // Lo grabado hasta ahora, con el pie de una grabación incompleta.
    const Recording& recording() const { return recording_; }

    // Termina la grabación con el hash del estado final y la devuelve; el Recorder queda
    // inactivo.
    Recording finish(std::uint64_t final_hash, std::int64_t timed_steps);

private:
    Recording recording_;
    bool active_ = false;
};

} // namespace rally::io
