#include "io/recording.h"

#include "sim/check.h"

#include <bit>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace rally::io {

namespace {

constexpr std::string_view kMagic = "RALLYREC";
constexpr std::string_view kEndMagic = "REND";
constexpr std::uint32_t kFlagComplete = 1u << 0;

// Escritura en little-endian, campo a campo.
class Writer {
public:
    explicit Writer(std::string& out) : out_(out) {}

    void u8(std::uint8_t v) { out_.push_back(static_cast<char>(v)); }
    void u16(std::uint16_t v) {
        u8(static_cast<std::uint8_t>(v));
        u8(static_cast<std::uint8_t>(v >> 8));
    }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            u8(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    }
    void u64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            u8(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    }
    void i16(std::int16_t v) { u16(static_cast<std::uint16_t>(v)); }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
    void f32(float v) { u32(std::bit_cast<std::uint32_t>(v)); }
    void f64(double v) { u64(std::bit_cast<std::uint64_t>(v)); }
    void raw(std::string_view bytes) { out_.append(bytes); }
    void str(std::string_view bytes) {
        u32(static_cast<std::uint32_t>(bytes.size()));
        raw(bytes);
    }

private:
    std::string& out_;
};

// Lectura con límites: tras el primer fallo todo devuelve 0 y ok() es false.
class Reader {
public:
    explicit Reader(std::string_view in) : in_(in) {}

    bool ok() const { return ok_; }
    std::size_t offset() const { return pos_; }
    std::size_t remaining() const { return in_.size() - pos_; }

    std::uint8_t u8() {
        if (!take(1)) {
            return 0;
        }
        return static_cast<std::uint8_t>(in_[pos_ - 1]);
    }
    std::uint16_t u16() {
        const std::uint16_t lo = u8();
        return static_cast<std::uint16_t>(lo | (static_cast<std::uint16_t>(u8()) << 8));
    }
    std::uint32_t u32() {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<std::uint32_t>(u8()) << (8 * i);
        }
        return v;
    }
    std::uint64_t u64() {
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<std::uint64_t>(u8()) << (8 * i);
        }
        return v;
    }
    std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
    float f32() { return std::bit_cast<float>(u32()); }
    double f64() { return std::bit_cast<double>(u64()); }
    std::string_view raw(std::size_t size) {
        if (!take(size)) {
            return {};
        }
        return in_.substr(pos_ - size, size);
    }
    std::string str() {
        const std::uint32_t size = u32();
        return std::string(raw(size));
    }

private:
    bool take(std::size_t size) {
        if (!ok_ || size > remaining()) {
            ok_ = false;
            return false;
        }
        pos_ += size;
        return true;
    }

    std::string_view in_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

std::string at(std::string_view what, std::size_t offset) {
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "%.*s (byte %zu)", static_cast<int>(what.size()),
                  what.data(), offset);
    return buffer;
}

bool valid_input(const sim::SimInput& input) {
    return input.steer >= -sim::kAxisMax && input.throttle >= 0 && input.brake >= 0;
}

std::filesystem::path to_path(const std::string& utf8) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

} // namespace

std::size_t header_bytes(const RecordingHeader& header) {
    return kMagic.size() + 4 + 4 + (4 + header.commit.size()) + (4 + header.world.size()) +
           (4 + header.car_text.size()) + (4 + header.settings.size()) +
           (header.format_version >= 2 ? 8 : 0) + 4;
}

std::string serialize(const Recording& recording) {
    std::string out;
    out.reserve(header_bytes(recording.header) + recording.runs.size() * kRunBytes + kFooterBytes);
    Writer w(out);
    const RecordingHeader& h = recording.header;
    w.raw(kMagic);
    w.u32(h.format_version);
    w.u32(h.sim_version);
    w.str(h.commit);
    w.str(h.world);
    w.str(h.car_text);
    w.str(h.settings);
    if (h.format_version >= 2) {
        w.u64(h.zone_hash);
    }
    w.u32(static_cast<std::uint32_t>(recording.runs.size()));
    for (const InputRun& run : recording.runs) {
        w.u32(run.count);
        w.i16(run.input.steer);
        w.i16(run.input.throttle);
        w.i16(run.input.brake);
        w.u16(run.input.buttons);
    }
    const RecordingFooter& f = recording.footer;
    w.u32(f.complete ? kFlagComplete : 0u);
    w.u64(f.total_steps);
    w.u64(f.final_hash);
    w.i64(f.timed_steps);
    w.raw(kEndMagic);
    return out;
}

ParseResult parse(std::string_view bytes) {
    ParseResult result;
    Reader r(bytes);
    if (r.raw(kMagic.size()) != kMagic) {
        result.error = "no es una grabación de Rally Coop (firma distinta de RALLYREC)";
        return result;
    }
    RecordingHeader& h = result.recording.header;
    h.format_version = r.u32();
    if (r.ok() && (h.format_version < kOldestRecordingFormatVersion ||
                   h.format_version > kRecordingFormatVersion)) {
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer),
                      "versión del formato %u, se esperaba de la %u a la %u", h.format_version,
                      kOldestRecordingFormatVersion, kRecordingFormatVersion);
        result.error = buffer;
        return result;
    }
    h.sim_version = r.u32();
    h.commit = r.str();
    h.world = r.str();
    h.car_text = r.str();
    h.settings = r.str();
    if (h.format_version >= 2) {
        h.zone_hash = r.u64();
    }
    const std::uint32_t run_count = r.u32();
    if (!r.ok()) {
        result.error = at("archivo truncado en la cabecera", r.offset());
        return result;
    }
    // Antes de reservar: los tramos que dice tienen que caber en lo que queda.
    if (static_cast<std::uint64_t>(run_count) * kRunBytes + kFooterBytes > r.remaining()) {
        result.error = at("archivo truncado: faltan tramos o el pie", r.offset());
        return result;
    }
    std::vector<InputRun>& runs = result.recording.runs;
    runs.resize(run_count);
    std::uint64_t steps = 0;
    for (InputRun& run : runs) {
        const std::size_t offset = r.offset();
        run.count = r.u32();
        run.input.steer = r.i16();
        run.input.throttle = r.i16();
        run.input.brake = r.i16();
        run.input.buttons = r.u16();
        if (run.count == 0) {
            result.error = at("tramo con 0 repeticiones", offset);
            return result;
        }
        if (!valid_input(run.input)) {
            result.error = at("entrada fuera de rango", offset);
            return result;
        }
        steps += run.count;
    }
    RecordingFooter& f = result.recording.footer;
    const std::uint32_t flags = r.u32();
    f.complete = (flags & kFlagComplete) != 0;
    f.total_steps = r.u64();
    f.final_hash = r.u64();
    f.timed_steps = r.i64();
    const std::size_t end_offset = r.offset();
    if (r.raw(kEndMagic.size()) != kEndMagic || !r.ok()) {
        result.error = at("pie dañado (falta REND)", end_offset);
        return result;
    }
    if (r.remaining() != 0) {
        result.error = at("sobran bytes tras el pie", r.offset());
        return result;
    }
    if (f.total_steps != steps) {
        result.error = "los tramos no suman los pasos del pie";
        return result;
    }
    result.ok = true;
    return result;
}

std::string serialize_world(const sim::WorldDesc& desc) {
    std::string out;
    Writer w(out);
    w.u32(static_cast<std::uint32_t>(desc.bodies.size()));
    for (const sim::BoxDesc& box : desc.bodies) {
        w.u8(static_cast<std::uint8_t>(box.motion));
        w.f64(box.x);
        w.f64(box.y);
        w.f64(box.z);
        w.f32(box.half_x);
        w.f32(box.half_y);
        w.f32(box.half_z);
        w.f32(box.pitch);
        w.u8(static_cast<std::uint8_t>(box.surface));
        w.u8(box.micro ? 1 : 0);
    }
    w.u8(desc.car ? 1 : 0);
    w.f64(desc.car_spawn.x);
    w.f64(desc.car_spawn.y);
    w.f64(desc.car_spawn.z);
    w.f32(desc.car_spawn.yaw);
    w.f32(desc.car_spawn.speed);
    w.u8(desc.test_zone ? 1 : 0);
    w.u8(static_cast<std::uint8_t>(desc.blind_surface));
    w.u8(desc.aids.steer_assist ? 1 : 0);
    w.u8(static_cast<std::uint8_t>(desc.aids.gearbox));
    w.u8(static_cast<std::uint8_t>(desc.car_spawn.gear));
    return out;
}

bool parse_world(std::string_view bytes, sim::WorldDesc& desc, std::string& error,
                 std::uint32_t format_version) {
    // Hasta la versión 2, cada cuerpo son 37 bytes y detrás va el índice del cuerpo
    // controlado (la caja de las tareas 2.1–2.3), que se comprueba y se ignora: esas
    // grabaciones no tienen coche. Desde la 3, cada cuerpo lleva además su giro, su superficie
    // y su h, y detrás va el coche. Desde la 4, al final, las ayudas del jugador (u8 ayuda de
    // dirección); hasta la 3, sin ayudas (no existían). Desde la 5, detrás, el modo de la caja
    // (u8) y la marcha de salida de los tests (i8; 127: la que da la velocidad); hasta la 4,
    // automático y la marcha que da la velocidad.
    const bool v3 = format_version >= 3;
    const std::size_t body_bytes = 1 + 3 * 8 + 3 * 4 + (v3 ? 4 + 1 + 1 : 0);
    const std::size_t car_bytes = 1 + 3 * 8 + 2 * 4;
    const std::size_t tail_bytes = (v3 ? car_bytes : 4) + (format_version >= 2 ? 2 : 0) +
                                   (format_version >= 4 ? 1 : 0) + (format_version >= 5 ? 2 : 0);
    Reader r(bytes);
    const std::uint32_t count = r.u32();
    if (!r.ok() || static_cast<std::uint64_t>(count) * body_bytes + tail_bytes != r.remaining()) {
        error = "descripción del mundo con un tamaño que no cuadra";
        return false;
    }
    desc = sim::WorldDesc{};
    desc.bodies.resize(count);
    for (sim::BoxDesc& box : desc.bodies) {
        const std::uint8_t motion = r.u8();
        if (motion > static_cast<std::uint8_t>(sim::BodyMotion::Dynamic)) {
            error = "descripción del mundo: tipo de movimiento desconocido";
            return false;
        }
        box.motion = static_cast<sim::BodyMotion>(motion);
        box.x = r.f64();
        box.y = r.f64();
        box.z = r.f64();
        box.half_x = r.f32();
        box.half_y = r.f32();
        box.half_z = r.f32();
        if (v3) {
            box.pitch = r.f32();
            const std::uint8_t surface = r.u8();
            const std::uint8_t micro = r.u8();
            if (surface >= sim::kSurfaceCount || micro > 1) {
                error = "descripción del mundo: superficie o h de una caja no válidos";
                return false;
            }
            box.surface = static_cast<sim::Surface>(surface);
            box.micro = micro == 1;
        }
    }
    if (v3) {
        const std::uint8_t car = r.u8();
        if (car > 1) {
            error = "descripción del mundo: coche no válido";
            return false;
        }
        desc.car = car == 1;
        desc.car_spawn.x = r.f64();
        desc.car_spawn.y = r.f64();
        desc.car_spawn.z = r.f64();
        desc.car_spawn.yaw = r.f32();
        desc.car_spawn.speed = r.f32();
    } else {
        const std::int32_t controlled = r.i32();
        if (controlled < -1 || controlled >= static_cast<std::int32_t>(count) ||
            (controlled >= 0 && desc.bodies[static_cast<std::size_t>(controlled)].motion !=
                                    sim::BodyMotion::Dynamic)) {
            error = "descripción del mundo: cuerpo controlado no válido";
            return false;
        }
    }
    if (format_version >= 2) {
        const std::uint8_t zone = r.u8();
        const std::uint8_t surface = r.u8();
        if (zone > 1 || surface >= sim::kSurfaceCount) {
            error = "descripción del mundo: zona de pruebas o superficie no válidas";
            return false;
        }
        desc.test_zone = zone == 1;
        desc.blind_surface = static_cast<sim::Surface>(surface);
    }
    if (format_version >= 4) {
        const std::uint8_t steer_assist = r.u8();
        if (steer_assist > 1) {
            error = "descripción del mundo: ayuda de dirección no válida";
            return false;
        }
        desc.aids.steer_assist = steer_assist == 1;
    }
    if (format_version >= 5) {
        const std::uint8_t gearbox = r.u8();
        const auto gear = static_cast<std::int8_t>(r.u8());
        if (gearbox > 1 ||
            (gear != sim::kSpawnGearBySpeed && (gear < -1 || gear > sim::kGearCount))) {
            error = "descripción del mundo: modo de la caja o marcha de salida no válidos";
            return false;
        }
        desc.aids.gearbox = static_cast<sim::GearboxMode>(gearbox);
        desc.car_spawn.gear = gear;
    }
    return true;
}

bool read_file(const std::string& path, std::string& bytes, std::string& error) {
    std::ifstream file(to_path(path), std::ios::binary);
    if (!file) {
        error = "no se puede abrir " + path;
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (file.bad()) {
        error = "error al leer " + path;
        return false;
    }
    return true;
}

bool write_file(const std::string& path, std::string_view bytes, std::string& error) {
    const std::filesystem::path target = to_path(path);
    std::error_code ec;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
    }
    std::ofstream file(target, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "no se puede crear " + path;
        return false;
    }
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file) {
        error = "error al escribir " + path;
        return false;
    }
    return true;
}

void Recorder::begin(RecordingHeader header, std::size_t reserve_runs) {
    recording_ = Recording{};
    recording_.header = std::move(header);
    recording_.runs.reserve(reserve_runs);
    active_ = true;
}

void Recorder::ensure_capacity(int steps) {
    std::vector<InputRun>& runs = recording_.runs;
    const std::size_t needed = runs.size() + static_cast<std::size_t>(steps > 0 ? steps : 0);
    if (needed > runs.capacity()) {
        runs.reserve(needed > runs.capacity() * 2 ? needed : runs.capacity() * 2);
    }
}

void Recorder::append(const sim::SimInput& input) {
    RALLY_CHECK(active_, "Recorder::append: no hay grabación empezada");
    std::vector<InputRun>& runs = recording_.runs;
    if (!runs.empty() && runs.back().input == input &&
        runs.back().count < std::numeric_limits<std::uint32_t>::max()) {
        ++runs.back().count;
    } else {
        RALLY_CHECK(runs.size() < runs.capacity(),
                    "Recorder::append: sin sitio (falta ensure_capacity antes de los pasos)");
        runs.push_back(InputRun{1, input});
    }
    ++recording_.footer.total_steps;
}

Recording Recorder::finish(std::uint64_t final_hash, std::int64_t timed_steps) {
    RALLY_CHECK(active_, "Recorder::finish: no hay grabación empezada");
    recording_.footer.complete = true;
    recording_.footer.final_hash = final_hash;
    recording_.footer.timed_steps = timed_steps;
    active_ = false;
    return std::move(recording_);
}

} // namespace rally::io
