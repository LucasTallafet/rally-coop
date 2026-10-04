#include "sim/car_data.h"

#include "sim/check.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <string>

namespace rally::sim {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Una clave del archivo: sección, nombre, campo, factor a SI y rango válido (en SI).
struct KeySpec {
    std::string_view section;
    std::string_view name;
    float CarData::* field;
    float scale;
    float min;
    float max;
};

constexpr float kDeg = kPi / 180.0f;
constexpr float kBig = 1.0e9f;

// El orden es el de las secciones en el archivo; el lector no depende de él.
constexpr std::array kKeys = {
    KeySpec{"chassis", "mass", &CarData::mass, 1.0f, 100.0f, 10000.0f},
    KeySpec{"chassis", "front_weight", &CarData::front_weight, 1.0f, 0.05f, 0.95f},
    KeySpec{"chassis", "wheelbase", &CarData::wheelbase, 1.0f, 0.5f, 10.0f},
    KeySpec{"chassis", "track", &CarData::track, 1.0f, 0.5f, 5.0f},
    KeySpec{"chassis", "cg_height", &CarData::cg_height, 1.0f, 0.05f, 5.0f},
    KeySpec{"chassis", "inertia_yaw", &CarData::inertia_yaw, 1.0f, 1.0f, kBig},
    KeySpec{"chassis", "inertia_pitch", &CarData::inertia_pitch, 1.0f, 1.0f, kBig},
    KeySpec{"chassis", "inertia_roll", &CarData::inertia_roll, 1.0f, 1.0f, kBig},
    KeySpec{"chassis", "box_length", &CarData::box_length, 1.0f, 0.5f, 20.0f},
    KeySpec{"chassis", "box_width", &CarData::box_width, 1.0f, 0.5f, 5.0f},
    KeySpec{"chassis", "box_height", &CarData::box_height, 1.0f, 0.2f, 5.0f},
    KeySpec{"chassis", "ground_clearance", &CarData::ground_clearance, 1.0f, 0.01f, 2.0f},
    KeySpec{"chassis", "cda", &CarData::cda, 1.0f, 0.0f, 10.0f},
    KeySpec{"wheels", "radius", &CarData::wheel_radius, 1.0f, 0.1f, 2.0f},
    KeySpec{"wheels", "width", &CarData::wheel_width, 1.0f, 0.05f, 1.0f},
    KeySpec{"wheels", "inertia", &CarData::wheel_inertia, 1.0f, 0.05f, 100.0f},
    KeySpec{"wheels", "footprint_length", &CarData::footprint_length, 1.0f, 0.0f, 1.0f},
    KeySpec{"suspension", "travel", &CarData::travel, 1.0f, 0.02f, 1.0f},
    KeySpec{"suspension", "frequency_front", &CarData::frequency_front, 1.0f, 0.3f, 10.0f},
    KeySpec{"suspension", "frequency_rear", &CarData::frequency_rear, 1.0f, 0.3f, 10.0f},
    KeySpec{"suspension", "damping_ratio", &CarData::damping_ratio, 1.0f, 0.0f, 2.0f},
    KeySpec{"suspension", "antiroll_front", &CarData::antiroll_front, 1.0f, 0.0f, kBig},
    KeySpec{"suspension", "antiroll_rear", &CarData::antiroll_rear, 1.0f, 0.0f, kBig},
    KeySpec{"brakes", "max_torque", &CarData::brake_torque, 1.0f, 0.0f, kBig},
    KeySpec{"brakes", "front_share", &CarData::brake_front_share, 1.0f, 0.0f, 1.0f},
    KeySpec{"brakes", "handbrake_torque", &CarData::handbrake_torque, 1.0f, 0.0f, kBig},
    KeySpec{"brakes", "hold_frequency", &CarData::hold_frequency, 1.0f, 0.5f, 20.0f},
    KeySpec{"brakes", "hold_speed", &CarData::hold_speed, 1.0f, 0.001f, 2.0f},
    KeySpec{"steering", "max_steer_deg", &CarData::max_steer, kDeg, 1.0f * kDeg, 60.0f * kDeg},
    KeySpec{"steering", "ackermann", &CarData::ackermann, 1.0f, 0.0f, 1.0f},
    KeySpec{"tire", "long_stiffness", &CarData::long_stiffness, 1.0f, 0.1f, 1000.0f},
    KeySpec{"tire", "lat_stiffness", &CarData::lat_stiffness, 1.0f, 0.1f, 1000.0f},
    KeySpec{"tire", "mu_asphalt", &CarData::mu_asphalt, 1.0f, 0.01f, 5.0f},
    KeySpec{"tire", "mu_dirt", &CarData::mu_dirt, 1.0f, 0.01f, 5.0f},
    KeySpec{"tire", "mu_gravel", &CarData::mu_gravel, 1.0f, 0.01f, 5.0f},
    KeySpec{"tire", "mu_snow", &CarData::mu_snow, 1.0f, 0.01f, 5.0f},
    KeySpec{"tire", "mu_sand", &CarData::mu_sand, 1.0f, 0.01f, 5.0f},
    KeySpec{"tire", "rolling_resistance", &CarData::rolling_resistance, 1.0f, 0.0f, 1.0f},
    KeySpec{"tire", "min_slip_speed", &CarData::min_slip_speed, 1.0f, 0.1f, 20.0f},
    KeySpec{"tire", "low_speed_blend", &CarData::low_speed_blend, 1.0f, 0.1f, 20.0f},
    KeySpec{"drive", "max_torque", &CarData::drive_torque, 1.0f, 0.0f, kBig},
    KeySpec{"drive", "max_power_kw", &CarData::drive_power, 1000.0f, 0.0f, kBig},
    KeySpec{"drive", "front_share", &CarData::drive_front_share, 1.0f, 0.0f, 1.0f},
    KeySpec{"driver", "eye_left", &CarData::eye_left, 1.0f, -5.0f, 5.0f},
    KeySpec{"driver", "eye_height", &CarData::eye_height, 1.0f, 0.1f, 5.0f},
    KeySpec{"driver", "eye_behind_front_axle", &CarData::eye_behind_front_axle, 1.0f, -5.0f, 10.0f},
};

constexpr std::string_view kWhitespace = " \t\r";

std::string_view trim(std::string_view s) {
    const std::size_t first = s.find_first_not_of(kWhitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = s.find_last_not_of(kWhitespace);
    return s.substr(first, last - first + 1);
}

CarParseResult fail(int line, const std::string& message) {
    CarParseResult result;
    result.error = "línea " + std::to_string(line) + ": " + message;
    return result;
}

} // namespace

float CarData::mu(Surface surface) const {
    switch (surface) {
    case Surface::Asphalt:
        return mu_asphalt;
    case Surface::Gravel:
        return mu_gravel;
    case Surface::Dirt:
        return mu_dirt;
    case Surface::Snow:
        return mu_snow;
    case Surface::Sand:
        return mu_sand;
    }
    return mu_dirt;
}

CarParseResult parse_car(std::string_view text) {
    CarData car;
    std::array<int, kKeys.size()> seen_line{}; // línea de cada clave leída; 0 si falta
    std::string_view section;
    int line_number = 0;
    // Última línea de cada sección vista, para el error de una clave que falta.
    constexpr std::array<std::string_view, 8> kSections = {
        "chassis", "wheels", "suspension", "brakes", "steering", "tire", "drive", "driver"};
    std::array<int, kSections.size()> section_end{};

    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t end = text.find('\n', pos);
        std::string_view line =
            text.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
        pos = end == std::string_view::npos ? text.size() + 1 : end + 1;
        ++line_number;

        const std::size_t comment = line.find('#');
        if (comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[') {
            if (line.back() != ']') {
                return fail(line_number, "sección mal escrita: falta ']'");
            }
            section = trim(line.substr(1, line.size() - 2));
            bool known = false;
            for (std::size_t s = 0; s < kSections.size(); ++s) {
                if (kSections[s] == section) {
                    known = true;
                    if (section_end[s] != 0) {
                        return fail(line_number, "sección [" + std::string(section) + "] repetida");
                    }
                    section_end[s] = line_number;
                }
            }
            if (!known) {
                return fail(line_number, "sección desconocida [" + std::string(section) + "]");
            }
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            return fail(line_number, "se esperaba 'clave = valor'");
        }
        const std::string_view key = trim(line.substr(0, equals));
        const std::string_view value = trim(line.substr(equals + 1));
        if (section.empty()) {
            return fail(line_number, "clave '" + std::string(key) + "' fuera de una sección");
        }
        std::size_t index = kKeys.size();
        for (std::size_t k = 0; k < kKeys.size(); ++k) {
            if (kKeys[k].section == section && kKeys[k].name == key) {
                index = k;
                break;
            }
        }
        if (index == kKeys.size()) {
            return fail(line_number, "clave desconocida '" + std::string(key) + "' en [" +
                                         std::string(section) + "]");
        }
        if (seen_line[index] != 0) {
            return fail(line_number, "clave '" + std::string(key) + "' repetida (ya en la línea " +
                                         std::to_string(seen_line[index]) + ")");
        }
        float number = 0.0f;
        const char* first = value.data();
        const char* last = value.data() + value.size();
        const std::from_chars_result parsed = std::from_chars(first, last, number);
        if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != last) {
            return fail(line_number, "número mal escrito en '" + std::string(key) + "': '" +
                                         std::string(value) + "'");
        }
        const KeySpec& spec = kKeys[index];
        const float si = number * spec.scale;
        if (!(si >= spec.min && si <= spec.max)) {
            return fail(line_number,
                        "'" + std::string(key) + "' fuera de rango: " + std::string(value));
        }
        car.*spec.field = si;
        seen_line[index] = line_number;
        for (std::size_t s = 0; s < kSections.size(); ++s) {
            if (kSections[s] == section) {
                section_end[s] = line_number;
            }
        }
    }

    for (std::size_t k = 0; k < kKeys.size(); ++k) {
        if (seen_line[k] != 0) {
            continue;
        }
        int line = line_number;
        for (std::size_t s = 0; s < kSections.size(); ++s) {
            if (kSections[s] == kKeys[k].section && section_end[s] != 0) {
                line = section_end[s];
            }
        }
        return fail(line, "falta la clave '" + std::string(kKeys[k].name) + "' en [" +
                              std::string(kKeys[k].section) + "]");
    }
    CarParseResult result;
    result.ok = true;
    result.car = car;
    return result;
}

CarData load_car_or_abort(std::string_view text, std::string_view name) {
    const CarParseResult result = parse_car(text);
    if (!result.ok) {
        static std::string message; // vive hasta el abort
        message = "coche " + std::string(name) + ": " + result.error;
        RALLY_CHECK(false, message.c_str());
    }
    return result.car;
}

} // namespace rally::sim
