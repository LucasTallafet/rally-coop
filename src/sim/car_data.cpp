#include "sim/car_data.h"

#include "sim/check.h"
#include "sim/rmath.h"
#include "sim/tire.h"

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
constexpr float kRpm = kPi / 30.0f; // rad/s por rpm
constexpr float kBar = 1.0e5f;      // Pa por bar
constexpr float kBig = 1.0e9f;
constexpr int kWheelsPerCar = 4;

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
    KeySpec{"steering", "max_steer_deg", &CarData::max_steer, kDeg, 1.0f * kDeg, 60.0f * kDeg},
    KeySpec{"steering", "ackermann", &CarData::ackermann, 1.0f, 0.0f, 1.0f},
    KeySpec{"tire", "carcass_damping_long", &CarData::carcass_damping_long, 1.0f, 0.0f, 5.0f},
    KeySpec{"tire", "carcass_damping_lat", &CarData::carcass_damping_lat, 1.0f, 0.0f, 5.0f},
    KeySpec{"tire", "carcass_damping_fade", &CarData::carcass_damping_fade, 1.0f, 0.1f, 50.0f},
    KeySpec{"engine", "idle_rpm", &CarData::idle_speed, kRpm, 300.0f * kRpm, 3000.0f * kRpm},
    KeySpec{"engine", "limiter_rpm", &CarData::limiter_speed, kRpm, 1000.0f * kRpm,
            20000.0f * kRpm},
    KeySpec{"engine", "limiter_ramp_rpm", &CarData::limiter_ramp, kRpm, 10.0f * kRpm,
            2000.0f * kRpm},
    KeySpec{"engine", "inertia", &CarData::engine_inertia, 1.0f, 0.01f, 10.0f},
    KeySpec{"engine", "friction", &CarData::engine_friction, 1.0f, 0.0f, 10.0f},
    KeySpec{"engine", "torque_scale", &CarData::torque_scale, 1.0f, 0.1f, 3.0f},
    KeySpec{"turbo", "max_boost_bar", &CarData::boost_max, kBar, 0.0f, 5.0f * kBar},
    KeySpec{"turbo", "boost_gain_per_bar", &CarData::boost_gain, 1.0f / kBar, 0.0f, 5.0f / kBar},
    KeySpec{"turbo", "threshold_rpm", &CarData::boost_threshold_speed, kRpm, 0.0f, 20000.0f * kRpm},
    KeySpec{"turbo", "full_rpm", &CarData::boost_full_speed, kRpm, 0.0f, 20000.0f * kRpm},
    KeySpec{"turbo", "rise_time", &CarData::boost_rise_time, 1.0f, 0.01f, 10.0f},
    KeySpec{"turbo", "rise_reference_rpm", &CarData::boost_rise_speed, kRpm, 100.0f * kRpm,
            20000.0f * kRpm},
    KeySpec{"turbo", "fall_time", &CarData::boost_fall_time, 1.0f, 0.01f, 10.0f},
    KeySpec{"clutch", "capacity_factor", &CarData::clutch_capacity_factor, 1.0f, 1.0f, 5.0f},
    KeySpec{"clutch", "bite_fraction", &CarData::bite_fraction, 1.0f, 0.0f, 1.0f},
    KeySpec{"clutch", "engage_rpm", &CarData::engage_speed, kRpm, 300.0f * kRpm, 10000.0f * kRpm},
    KeySpec{"clutch", "launch_rpm", &CarData::launch_speed, kRpm, 300.0f * kRpm, 20000.0f * kRpm},
    KeySpec{"clutch", "launch_ramp_rpm", &CarData::launch_ramp, kRpm, 10.0f * kRpm, 5000.0f * kRpm},
    KeySpec{"clutch", "open_rpm", &CarData::open_speed, kRpm, 300.0f * kRpm, 10000.0f * kRpm},
    KeySpec{"gearbox", "reverse", &CarData::reverse_ratio, 1.0f, 0.5f, 100.0f},
    KeySpec{"gearbox", "gear_1", &CarData::gear_ratio_1, 1.0f, 0.5f, 100.0f},
    KeySpec{"gearbox", "gear_2", &CarData::gear_ratio_2, 1.0f, 0.5f, 100.0f},
    KeySpec{"gearbox", "gear_3", &CarData::gear_ratio_3, 1.0f, 0.5f, 100.0f},
    KeySpec{"gearbox", "gear_4", &CarData::gear_ratio_4, 1.0f, 0.5f, 100.0f},
    KeySpec{"gearbox", "gear_5", &CarData::gear_ratio_5, 1.0f, 0.5f, 100.0f},
    KeySpec{"gearbox", "shift_time", &CarData::shift_time, 1.0f, 0.0f, 2.0f},
    KeySpec{"gearbox", "upshift_rpm_full", &CarData::upshift_full, kRpm, 300.0f * kRpm,
            20000.0f * kRpm},
    KeySpec{"gearbox", "upshift_rpm_closed", &CarData::upshift_closed, kRpm, 300.0f * kRpm,
            20000.0f * kRpm},
    KeySpec{"gearbox", "downshift_rpm_full", &CarData::downshift_full, kRpm, 300.0f * kRpm,
            20000.0f * kRpm},
    KeySpec{"gearbox", "downshift_rpm_closed", &CarData::downshift_closed, kRpm, 300.0f * kRpm,
            20000.0f * kRpm},
    KeySpec{"gearbox", "drift_hold_time", &CarData::drift_hold_time, 1.0f, 0.0f, 1.0f},
    KeySpec{"gearbox", "drift_angle_deg", &CarData::drift_angle, kDeg, 1.0f * kDeg, 90.0f * kDeg},
    KeySpec{"driveline", "front_share", &CarData::drive_front_share, 1.0f, 0.0f, 1.0f},
    KeySpec{"driveline", "viscous", &CarData::viscous, 1.0f, 0.0f, 100000.0f},
    KeySpec{"driveline", "lsd_front_preload", &CarData::lsd_front_preload, 1.0f, 0.0f, 10000.0f},
    KeySpec{"driveline", "lsd_front_drive", &CarData::lsd_front_drive, 1.0f, 0.0f, 1.0f},
    KeySpec{"driveline", "lsd_front_coast", &CarData::lsd_front_coast, 1.0f, 0.0f, 1.0f},
    KeySpec{"driveline", "lsd_rear_preload", &CarData::lsd_rear_preload, 1.0f, 0.0f, 10000.0f},
    KeySpec{"driveline", "lsd_rear_drive", &CarData::lsd_rear_drive, 1.0f, 0.0f, 1.0f},
    KeySpec{"driveline", "lsd_rear_coast", &CarData::lsd_rear_coast, 1.0f, 0.0f, 1.0f},
    KeySpec{"driver", "eye_left", &CarData::eye_left, 1.0f, -5.0f, 5.0f},
    KeySpec{"driver", "eye_height", &CarData::eye_height, 1.0f, 0.1f, 5.0f},
    KeySpec{"driver", "eye_behind_front_axle", &CarData::eye_behind_front_axle, 1.0f, -5.0f, 10.0f},
};

// Claves de cada sección [tire.<superficie>] (las cinco llevan las mismas).
struct TireKeySpec {
    std::string_view name;
    float TireSurface::* field;
    float scale;
    float min;
    float max;
};

constexpr std::array kTireKeys = {
    TireKeySpec{"mu_peak", &TireSurface::mu_peak, 1.0f, 0.05f, 3.0f},
    TireKeySpec{"slip_angle_peak_deg", &TireSurface::slip_angle_peak, kDeg, 1.0f * kDeg,
                45.0f * kDeg},
    TireKeySpec{"slip_ratio_peak", &TireSurface::slip_ratio_peak, 1.0f, 0.01f, 0.5f},
    TireKeySpec{"slide_ratio", &TireSurface::slide_ratio, 1.0f, 0.5f, 0.99f},
    // E >= 1 lo rechaza derive_tire_shape con su propio mensaje.
    TireKeySpec{"curvature_e", &TireSurface::curvature_e, 1.0f, -5.0f, 5.0f},
    TireKeySpec{"load_sensitivity", &TireSurface::load_sensitivity, 1.0f, 0.0f, 0.3f},
    TireKeySpec{"relaxation_long", &TireSurface::relaxation_long, 1.0f, 0.01f, 5.0f},
    TireKeySpec{"relaxation_lat", &TireSurface::relaxation_lat, 1.0f, 0.01f, 5.0f},
    TireKeySpec{"rolling_resistance", &TireSurface::rolling_resistance, 1.0f, 0.0f, 1.0f},
};

// Claves de [cockpit] (tarea 2.7; solo presentación).
struct CockpitKeySpec {
    std::string_view name;
    float CockpitData::* field;
    float scale;
    float min;
    float max;
};

constexpr std::array kCockpitKeys = {
    CockpitKeySpec{"windshield_base_ahead", &CockpitData::windshield_base_ahead, 1.0f, 0.1f, 3.0f},
    CockpitKeySpec{"windshield_base_below", &CockpitData::windshield_base_below, 1.0f, 0.0f, 2.0f},
    CockpitKeySpec{"windshield_top_ahead", &CockpitData::windshield_top_ahead, 1.0f, 0.1f, 3.0f},
    CockpitKeySpec{"windshield_top_above", &CockpitData::windshield_top_above, 1.0f, -1.0f, 2.0f},
    CockpitKeySpec{"pillar_width", &CockpitData::pillar_width, 1.0f, 0.01f, 0.5f},
    CockpitKeySpec{"pillar_left", &CockpitData::pillar_left, 1.0f, 0.0f, 3.0f},
    CockpitKeySpec{"pillar_right", &CockpitData::pillar_right, 1.0f, 0.0f, 3.0f},
    CockpitKeySpec{"roof_above", &CockpitData::roof_above, 1.0f, 0.05f, 2.0f},
    CockpitKeySpec{"roof_behind", &CockpitData::roof_behind, 1.0f, 0.0f, 3.0f},
    CockpitKeySpec{"door_left", &CockpitData::door_left, 1.0f, 0.1f, 3.0f},
    CockpitKeySpec{"door_right", &CockpitData::door_right, 1.0f, 0.1f, 3.0f},
    CockpitKeySpec{"sill_below", &CockpitData::sill_below, 1.0f, 0.0f, 2.0f},
    CockpitKeySpec{"dash_rear_ahead", &CockpitData::dash_rear_ahead, 1.0f, 0.1f, 3.0f},
    CockpitKeySpec{"wheel_ahead", &CockpitData::wheel_ahead, 1.0f, 0.1f, 2.0f},
    CockpitKeySpec{"wheel_diameter", &CockpitData::wheel_diameter, 1.0f, 0.1f, 1.0f},
    CockpitKeySpec{"wheel_center_below", &CockpitData::wheel_center_below, 1.0f, 0.0f, 2.0f},
    CockpitKeySpec{"wheel_tilt_deg", &CockpitData::wheel_tilt, kDeg, 0.0f, 80.0f * kDeg},
    CockpitKeySpec{"steering_ratio", &CockpitData::steering_ratio, 1.0f, 1.0f, 50.0f},
    CockpitKeySpec{"display_ahead", &CockpitData::display_ahead, 1.0f, 0.1f, 3.0f},
    CockpitKeySpec{"display_below", &CockpitData::display_below, 1.0f, -1.0f, 2.0f},
    CockpitKeySpec{"display_right", &CockpitData::display_right, 1.0f, -2.0f, 2.0f},
    CockpitKeySpec{"display_width", &CockpitData::display_width, 1.0f, 0.01f, 1.0f},
    CockpitKeySpec{"display_height", &CockpitData::display_height, 1.0f, 0.01f, 1.0f},
};

// Claves de [settings] (tarea 2.8): multiplicadores de las durezas blanda y dura.
struct SettingsKeySpec {
    std::string_view name;
    bool hard;
    float SuspensionScale::* field;
};

constexpr std::array kSettingsKeys = {
    SettingsKeySpec{"soft_frequency", false, &SuspensionScale::frequency},
    SettingsKeySpec{"soft_damping", false, &SuspensionScale::damping},
    SettingsKeySpec{"soft_antiroll", false, &SuspensionScale::antiroll},
    SettingsKeySpec{"hard_frequency", true, &SuspensionScale::frequency},
    SettingsKeySpec{"hard_damping", true, &SuspensionScale::damping},
    SettingsKeySpec{"hard_antiroll", true, &SuspensionScale::antiroll},
};
constexpr float kMinSettingsScale = 0.5f;
constexpr float kMaxSettingsScale = 2.0f;

// [engine] cylinders (solo presentación): un entero en este rango.
constexpr std::string_view kCylindersKey = "cylinders";
constexpr int kMinCylinders = 1;
constexpr int kMaxCylinders = 16;

// Secciones del archivo. Las de los neumáticos van en el orden de Surface.
constexpr std::size_t kEngineSection = 6;
constexpr std::size_t kCockpitSection = 13;
constexpr std::size_t kSettingsSection = 14;
constexpr std::size_t kFirstTireSection = 15;
constexpr std::array<std::string_view, kFirstTireSection + static_cast<std::size_t>(kSurfaceCount)>
    kSections = {"chassis",      "wheels",      "suspension",    "brakes",    "steering",
                 "tire",         "engine",      "engine.torque", "turbo",     "clutch",
                 "gearbox",      "driveline",   "driver",        "cockpit",   "settings",
                 "tire.asphalt", "tire.gravel", "tire.dirt",     "tire.snow", "tire.sand"};
// [engine.torque]: sus líneas son "rpm = N·m", no claves fijas.
constexpr std::size_t kTorqueSection = 7;
constexpr std::size_t kTurboSection = 8;
constexpr std::size_t kClutchSection = 9;
constexpr std::size_t kGearboxSection = 10;
constexpr std::size_t kDrivelineSection = 11;

// Paso de la simulación para pasar tiempos a pasos (240 Hz, sim/sim_world.h).
constexpr float kStepRate = 240.0f;
// El automático no puede bajar a menos de este margen por debajo de subir (histéresis).
constexpr float kShiftHysteresis = 500.0f * kRpm;

float lerp(float a, float b, float t) { return a + (b - a) * t; }

// Gravedad para F_z0 (la de Jolt por defecto).
constexpr float kGravity = 9.81f;

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

CarParseResult parse_car(std::string_view text, CarKeys keys) {
    CarData car;
    // Línea de cada clave leída (0 si falta): primero las de kKeys, después las de kTireKeys
    // de cada superficie, las de presentación (cylinders y las de kCockpitKeys) y al final las
    // de kSettingsKeys.
    constexpr std::size_t kTireSlots = static_cast<std::size_t>(kSurfaceCount) * kTireKeys.size();
    constexpr std::size_t kCylindersSlot = kKeys.size() + kTireSlots;
    constexpr std::size_t kFirstCockpitSlot = kCylindersSlot + 1;
    constexpr std::size_t kFirstSettingsSlot = kFirstCockpitSlot + kCockpitKeys.size();
    constexpr std::size_t kSlots = kFirstSettingsSlot + kSettingsKeys.size();
    float cylinders = 0.0f;
    std::array<int, kSlots> seen_line{};
    std::string_view section;
    std::size_t section_index = 0;
    int line_number = 0;
    // Primera y última línea de cada sección vista, para los errores.
    std::array<int, kSections.size()> section_start{};
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
                    section_start[s] = line_number;
                    section_end[s] = line_number;
                    section_index = s;
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
        if (section_index == kTorqueSection) {
            // Punto de la curva: "rpm = N·m", con las rpm crecientes.
            float rpm = 0.0f;
            float torque = 0.0f;
            const std::from_chars_result kr =
                std::from_chars(key.data(), key.data() + key.size(), rpm);
            const std::from_chars_result vr =
                std::from_chars(value.data(), value.data() + value.size(), torque);
            if (key.empty() || kr.ec != std::errc{} || kr.ptr != key.data() + key.size() ||
                value.empty() || vr.ec != std::errc{} || vr.ptr != value.data() + value.size()) {
                return fail(line_number, "punto de la curva mal escrito: se esperaba 'rpm = N·m'");
            }
            if (car.torque_points == kMaxTorquePoints) {
                return fail(line_number, "la curva de par tiene más de " +
                                             std::to_string(kMaxTorquePoints) + " puntos");
            }
            if (!(rpm >= 0.0f && rpm <= 20000.0f) || !(torque >= 0.0f && torque <= 10000.0f)) {
                return fail(line_number, "punto de la curva fuera de rango");
            }
            const float speed = rpm * kRpm;
            const auto n = static_cast<std::size_t>(car.torque_points);
            if (n > 0 && !(speed > car.torque_speed[n - 1])) {
                return fail(line_number, "las rpm de la curva de par tienen que ser crecientes");
            }
            car.torque_speed[n] = speed;
            car.torque_atm[n] = torque;
            ++car.torque_points;
            section_end[section_index] = line_number;
            continue;
        }
        // Hueco de la clave en seen_line y su especificación.
        std::size_t index = kSlots;
        float scale = 1.0f;
        float min = 0.0f;
        float max = 0.0f;
        float* target = nullptr;
        if (section_index == kCockpitSection) {
            for (std::size_t k = 0; k < kCockpitKeys.size(); ++k) {
                if (kCockpitKeys[k].name == key) {
                    index = kFirstCockpitSlot + k;
                    scale = kCockpitKeys[k].scale;
                    min = kCockpitKeys[k].min;
                    max = kCockpitKeys[k].max;
                    target = &(car.cockpit.*kCockpitKeys[k].field);
                    break;
                }
            }
        } else if (section_index == kSettingsSection) {
            for (std::size_t k = 0; k < kSettingsKeys.size(); ++k) {
                if (kSettingsKeys[k].name == key) {
                    index = kFirstSettingsSlot + k;
                    min = kMinSettingsScale;
                    max = kMaxSettingsScale;
                    SuspensionScale& setting = kSettingsKeys[k].hard ? car.hard : car.soft;
                    target = &(setting.*kSettingsKeys[k].field);
                    break;
                }
            }
        } else if (section_index == kEngineSection && key == kCylindersKey) {
            index = kCylindersSlot;
            min = static_cast<float>(kMinCylinders);
            max = static_cast<float>(kMaxCylinders);
            target = &cylinders;
        } else if (section_index < kFirstTireSection) {
            for (std::size_t k = 0; k < kKeys.size(); ++k) {
                if (kKeys[k].section == section && kKeys[k].name == key) {
                    index = k;
                    scale = kKeys[k].scale;
                    min = kKeys[k].min;
                    max = kKeys[k].max;
                    target = &(car.*kKeys[k].field);
                    break;
                }
            }
        } else {
            const std::size_t surface = section_index - kFirstTireSection;
            for (std::size_t k = 0; k < kTireKeys.size(); ++k) {
                if (kTireKeys[k].name == key) {
                    index = kKeys.size() + surface * kTireKeys.size() + k;
                    scale = kTireKeys[k].scale;
                    min = kTireKeys[k].min;
                    max = kTireKeys[k].max;
                    target = &(car.tires[surface].*kTireKeys[k].field);
                    break;
                }
            }
        }
        if (index == kSlots) {
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
        const float si = number * scale;
        if (!(si >= min && si <= max)) {
            return fail(line_number,
                        "'" + std::string(key) + "' fuera de rango: " + std::string(value));
        }
        if (index == kCylindersSlot && si != static_cast<float>(static_cast<int>(si))) {
            return fail(line_number, "'cylinders' tiene que ser un entero: " + std::string(value));
        }
        *target = si;
        seen_line[index] = line_number;
        section_end[section_index] = line_number;
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
    for (std::size_t surface = 0; surface < static_cast<std::size_t>(kSurfaceCount); ++surface) {
        const std::size_t s = kFirstTireSection + surface;
        for (std::size_t k = 0; k < kTireKeys.size(); ++k) {
            if (seen_line[kKeys.size() + surface * kTireKeys.size() + k] != 0) {
                continue;
            }
            const int line = section_end[s] != 0 ? section_end[s] : line_number;
            return fail(line, "falta la clave '" + std::string(kTireKeys[k].name) + "' en [" +
                                  std::string(kSections[s]) + "]");
        }
        std::string error;
        if (!derive_tire_shape(car.tires[surface], error)) {
            return fail(section_start[s], "[" + std::string(kSections[s]) + "]: " + error);
        }
    }
    // Presentación: con CarKeys::Full se exigen todas; con CarKeys::Sim pueden faltar.
    const auto missing_line = [&](std::size_t s) {
        return section_end[s] != 0 ? section_end[s] : line_number;
    };
    bool presentation = seen_line[kCylindersSlot] != 0;
    if (!presentation && keys == CarKeys::Full) {
        return fail(missing_line(kEngineSection), "falta la clave 'cylinders' en [engine]");
    }
    for (std::size_t k = 0; k < kCockpitKeys.size(); ++k) {
        if (seen_line[kFirstCockpitSlot + k] != 0) {
            continue;
        }
        presentation = false;
        if (keys == CarKeys::Full) {
            return fail(missing_line(kCockpitSection),
                        "falta la clave '" + std::string(kCockpitKeys[k].name) + "' en [cockpit]");
        }
    }
    // [settings]: con CarKeys::Full, obligatoria; con Sim puede faltar entera, pero si está
    // lleva sus seis claves.
    const bool settings_section = section_start[kSettingsSection] != 0;
    if (settings_section || keys == CarKeys::Full) {
        for (std::size_t k = 0; k < kSettingsKeys.size(); ++k) {
            if (seen_line[kFirstSettingsSlot + k] == 0) {
                return fail(missing_line(kSettingsSection), "falta la clave '" +
                                                                std::string(kSettingsKeys[k].name) +
                                                                "' en [settings]");
            }
        }
    }
    car.has_settings = settings_section;
    car.cylinders = static_cast<int>(cylinders);
    car.has_presentation = presentation;
    car.nominal_load = car.mass * kGravity / static_cast<float>(kWheelsPerCar);

    // Tren motriz: comprobaciones entre claves y derivados.
    const auto line_of = [&](std::size_t s) {
        return section_start[s] != 0 ? section_start[s] : line_number;
    };
    if (car.torque_points < 2) {
        return fail(line_of(kTorqueSection), "[engine.torque] necesita al menos 2 puntos");
    }
    if (!(car.idle_speed < car.engage_speed && car.engage_speed < car.open_speed &&
          car.engage_speed < car.launch_speed &&
          car.launch_speed < car.limiter_speed - car.limiter_ramp)) {
        return fail(line_of(kClutchSection),
                    "[clutch]: hace falta idle_rpm < engage_rpm < open_rpm y engage_rpm < "
                    "launch_rpm < limiter_rpm − limiter_ramp_rpm");
    }
    if (car.boost_max > 0.0f && !(car.boost_threshold_speed < car.boost_full_speed)) {
        return fail(line_of(kTurboSection),
                    "[turbo]: threshold_rpm tiene que ser menor que full_rpm");
    }
    const std::array<float, kGearCount> ratios = {
        car.gear_ratio_1, car.gear_ratio_2, car.gear_ratio_3, car.gear_ratio_4, car.gear_ratio_5};
    float step_ratio = 0.0f; // la mayor relación entre marchas seguidas
    for (std::size_t g = 1; g < ratios.size(); ++g) {
        if (!(ratios[g] < ratios[g - 1])) {
            return fail(line_of(kGearboxSection),
                        "[gearbox]: las relaciones tienen que bajar de gear_1 a gear_5");
        }
        step_ratio = rmath::max(step_ratio, ratios[g - 1] / ratios[g]);
    }
    // Histéresis del automático: tras bajar, las rpm quedan al menos kShiftHysteresis por debajo
    // de las de subir (lineal en el acelerador: basta con los extremos).
    for (const float t : {0.0f, 1.0f}) {
        const float up = lerp(car.upshift_closed, car.upshift_full, t);
        const float down = lerp(car.downshift_closed, car.downshift_full, t);
        if (!(down * step_ratio <= up - kShiftHysteresis)) {
            return fail(line_of(kGearboxSection),
                        "[gearbox]: tras bajar, las rpm tienen que quedar 500 rpm por debajo de "
                        "las de subir (downshift × la mayor relación entre marchas)");
        }
    }
    if (!(car.upshift_full < car.limiter_speed - car.limiter_ramp)) {
        return fail(
            line_of(kGearboxSection),
            "[gearbox]: upshift_rpm_full tiene que quedar por debajo del corte del limitador");
    }
    if (car.drive_front_share == 0.0f &&
        (car.viscous != 0.0f || car.lsd_front_preload != 0.0f || car.lsd_front_drive != 0.0f ||
         car.lsd_front_coast != 0.0f)) {
        return fail(line_of(kDrivelineSection),
                    "[driveline]: con front_share = 0 (propulsión), viscous y el autoblocante "
                    "delantero tienen que ser 0");
    }
    float peak = 0.0f;
    for (std::size_t i = 0; i < static_cast<std::size_t>(car.torque_points); ++i) {
        peak = rmath::max(peak, car.torque_atm[i]);
    }
    car.clutch_capacity = car.clutch_capacity_factor * peak *
                          (1.0f + car.boost_gain * car.boost_max) * car.torque_scale;
    // Ralentí sin carga: el acelerador que iguala el rozamiento con el par sin presión.
    float idle_torque = car.torque_atm[0];
    for (std::size_t i = 1; i < static_cast<std::size_t>(car.torque_points); ++i) {
        if (car.idle_speed <= car.torque_speed[i]) {
            const float t = rmath::clamp((car.idle_speed - car.torque_speed[i - 1]) /
                                             (car.torque_speed[i] - car.torque_speed[i - 1]),
                                         0.0f, 1.0f);
            idle_torque = lerp(car.torque_atm[i - 1], car.torque_atm[i], t);
            break;
        }
    }
    // θ·T = (1 − θ)·b·ω al ralentí: θ = b·ω/(T + b·ω).
    const float idle_friction = car.engine_friction * car.idle_speed;
    car.idle_throttle =
        rmath::clamp(idle_friction / (car.torque_scale * idle_torque + idle_friction), 0.0f, 1.0f);
    car.shift_steps = static_cast<int>(car.shift_time * kStepRate + 0.5f);
    car.drift_hold_steps = static_cast<int>(car.drift_hold_time * kStepRate + 0.5f);

    CarParseResult result;
    result.ok = true;
    result.car = car;
    return result;
}

CarData load_car_or_abort(std::string_view text, std::string_view name, CarKeys keys) {
    const CarParseResult result = parse_car(text, keys);
    if (!result.ok) {
        static std::string message; // vive hasta el abort
        message = "coche " + std::string(name) + ": " + result.error;
        RALLY_CHECK(false, message.c_str());
    }
    return result.car;
}

} // namespace rally::sim
