#include "io/telemetry_csv.h"

#include "sim/sim_version.h"
#include "sim/sim_world.h"

#include <array>
#include <charconv>
#include <filesystem>
#include <system_error>

namespace rally::io {

namespace {

using sim::CarTelemetry;

constexpr std::size_t kFlushBytes = 1u << 20;

std::filesystem::path to_path(const std::string& utf8) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

constexpr double input_axis(std::int16_t value) {
    return static_cast<double>(value) / sim::kAxisMax;
}

const char* surface_text(const CarTelemetry& t) {
    return t.surface < 0 ? "none" : sim::surface_key(static_cast<sim::Surface>(t.surface));
}

template <int W>
const char* wheel_surface_text(const CarTelemetry& t) {
    const std::int8_t surface = t.wheels[W].surface;
    return surface < 0 ? "none" : sim::surface_key(static_cast<sim::Surface>(surface));
}

using C = TelemetryColumn;
constexpr ColumnKind kInt = ColumnKind::Integer;
constexpr ColumnKind kReal = ColumnKind::Real;
constexpr double kPiDouble = 3.14159265358979323846;

// Precisión: posición, velocidad e impacto a 0,1 mm(/s); ángulos a 1e-5 rad (0,0006°);
// aceleración a 1 mm/s²; entradas a 1e-5 (el paso de un eje es 3e-5); tiempo a 1 µs.
// clang-format off
// Columnas de una rueda W (0 FL, 1 FR, 2 RL, 3 RR) con el sufijo de su nombre.
#define RALLY_WHEEL_COLUMNS(W, SUFFIX) \
    C{"contact_" SUFFIX, "", "0 o 1", "Rueda en el suelo", kInt, 0, \
      [](const CarTelemetry& t) { return static_cast<double>(t.wheels[W].contact); }, \
      nullptr}, \
    C{"surface_" SUFFIX, "", "texto", \
      "Superficie del material del contacto (la que usa el neumático); none en el aire", \
      ColumnKind::Text, 0, nullptr, wheel_surface_text<W>}, \
    C{"load_" SUFFIX, "N", "≥ 0", "Carga normal (suspensión, barra y tope del paso anterior)", kReal, 1, \
      [](const CarTelemetry& t) { return static_cast<double>(t.wheels[W].load); }, \
      nullptr}, \
    C{"compression_" SUFFIX, "m", "≥ 0", "Compresión de la suspensión desde la extensión máxima", kReal, 4, \
      [](const CarTelemetry& t) { return static_cast<double>(t.wheels[W].compression); }, \
      nullptr}, \
    C{"kappa_" SUFFIX, "", "+ tracción", "Deslizamiento relajado κ_r = u/σ_x (deformación longitudinal de la huella)", kReal, 5, \
      [](const CarTelemetry& t) { return static_cast<double>(t.wheels[W].kappa); }, \
      nullptr}, \
    C{"alpha_" SUFFIX, "rad", "− la rueda va hacia su derecha", "Ángulo de deriva relajado α_r = atan(w/σ_y) (deformación lateral de la huella)", kReal, 5, \
      [](const CarTelemetry& t) { return static_cast<double>(t.wheels[W].alpha); }, \
      nullptr}, \
    C{"slip_x_" SUFFIX, "m/s", "+ la rueda gira más rápido que el suelo", "Velocidad de deslizamiento longitudinal ω·r − v_x", kReal, 4, \
      [](const CarTelemetry& t) { return static_cast<double>(t.wheels[W].slip_x); }, \
      nullptr}, \
    C{"slip_y_" SUFFIX, "m/s", "+ la rueda va hacia su izquierda", "Velocidad de deslizamiento lateral −v_y", kReal, 4, \
      [](const CarTelemetry& t) { return static_cast<double>(t.wheels[W].slip_y); }, \
      nullptr}, \
    C{"fx_" SUFFIX, "N", "+ hacia delante de la rueda", "Fuerza longitudinal del neumático (media de los subpasos)", kReal, 1, \
      [](const CarTelemetry& t) { return static_cast<double>(t.wheels[W].fx); }, \
      nullptr}, \
    C{"fy_" SUFFIX, "N", "+ hacia la derecha de la rueda", "Fuerza lateral del neumático (media de los subpasos)", kReal, 1, \
      [](const CarTelemetry& t) { return static_cast<double>(t.wheels[W].fy); }, \
      nullptr}, \
    C{"fx_pure_" SUFFIX, "N", "+ hacia delante de la rueda", "Fuerza longitudinal que daría κ_r solo, sin la combinada (media de los subpasos)", kReal, 1, \
      [](const CarTelemetry& t) { return static_cast<double>(t.wheels[W].fx_pure); }, \
      nullptr}, \
    C{"fy_pure_" SUFFIX, "N", "+ hacia la derecha de la rueda", "Fuerza lateral que daría α_r solo, sin la combinada (media de los subpasos)", kReal, 1, \
      [](const CarTelemetry& t) { return static_cast<double>(t.wheels[W].fy_pure); }, \
      nullptr}, \
    C{"mu_" SUFFIX, "", "≥ 0", "μ efectivo: pico de la superficie con la sensibilidad a la carga, μ(F_z); 0 en el aire", kReal, 4, \
      [](const CarTelemetry& t) { return static_cast<double>(t.wheels[W].mu); }, \
      nullptr}, \
    C{"omega_" SUFFIX, "rad/s", "+ rodando hacia delante", "Giro de la rueda al final del paso", kReal, 4, \
      [](const CarTelemetry& t) { return static_cast<double>(t.wheels[W].omega); }, \
      nullptr}, \
    C{"brake_torque_" SUFFIX, "N·m", "≥ 0", "Par de freno pedido (freno y freno de mano)", kReal, 1, \
      [](const CarTelemetry& t) { return static_cast<double>(t.wheels[W].brake_torque); }, \
      nullptr}, \
    C{"h_" SUFFIX, "m", "+ arriba", "Media del microrrelieve h sobre la huella", kReal, 5, \
      [](const CarTelemetry& t) { return static_cast<double>(t.wheels[W].h); }, \
      nullptr}

constexpr std::array kColumns = {
    C{"step", "", "", "Paso desde que se construyó el mundo (1 tras el primero)", kInt, 0,
      [](const CarTelemetry& t) { return static_cast<double>(t.step); }, nullptr},
    C{"time", "s", "", "step / 240", kReal, 6,
      [](const CarTelemetry& t) { return static_cast<double>(t.step) / sim::kStepsPerSecond; }, nullptr},
    C{"x", "m", "", "Centro de masas del coche en el mundo", kReal, 4,
      [](const CarTelemetry& t) { return t.x; }, nullptr},
    C{"y", "m", "+ arriba", "Centro de masas en el mundo", kReal, 4,
      [](const CarTelemetry& t) { return t.y; }, nullptr},
    C{"z", "m", "", "Centro de masas en el mundo", kReal, 4,
      [](const CarTelemetry& t) { return t.z; }, nullptr},
    C{"vx", "m/s", "", "Velocidad del centro de masas, ejes del mundo", kReal, 4,
      [](const CarTelemetry& t) { return static_cast<double>(t.vx); }, nullptr},
    C{"vy", "m/s", "+ arriba", "Velocidad del centro de masas, ejes del mundo", kReal, 4,
      [](const CarTelemetry& t) { return static_cast<double>(t.vy); }, nullptr},
    C{"vz", "m/s", "", "Velocidad del centro de masas, ejes del mundo", kReal, 4,
      [](const CarTelemetry& t) { return static_cast<double>(t.vz); }, nullptr},
    C{"v_forward", "m/s", "+ hacia el morro", "Velocidad del centro de masas, ejes del coche", kReal, 4,
      [](const CarTelemetry& t) { return static_cast<double>(t.v_forward); }, nullptr},
    C{"v_right", "m/s", "+ hacia la derecha", "Velocidad del centro de masas, ejes del coche", kReal, 4,
      [](const CarTelemetry& t) { return static_cast<double>(t.v_right); }, nullptr},
    C{"v_up", "m/s", "+ hacia el techo", "Velocidad del centro de masas, ejes del coche", kReal, 4,
      [](const CarTelemetry& t) { return static_cast<double>(t.v_up); }, nullptr},
    C{"speed", "m/s", "≥ 0", "Módulo de la velocidad del centro de masas", kReal, 4,
      [](const CarTelemetry& t) { return static_cast<double>(t.speed); }, nullptr},
    C{"heading", "rad", "0 mirando a −z; + girando a la derecha", "Rumbo del morro, en (−π, π]", kReal, 5,
      [](const CarTelemetry& t) { return static_cast<double>(t.heading); }, nullptr},
    C{"yaw_rate", "rad/s", "+ girando a la derecha", "Velocidad de guiñada (−ω · arriba del coche)", kReal, 5,
      [](const CarTelemetry& t) { return static_cast<double>(t.yaw_rate); }, nullptr},
    C{"beta", "rad", "+ velocidad a la derecha del morro", "Ángulo de deriva atan2(v_right, v_forward), en (−π, π]; sin suavizar", kReal, 5,
      [](const CarTelemetry& t) { return static_cast<double>(t.beta); }, nullptr},
    C{"ax", "m/s²", "", "Aceleración del centro de masas (dv/dt, sin la gravedad), ejes del mundo", kReal, 3,
      [](const CarTelemetry& t) { return static_cast<double>(t.ax); }, nullptr},
    C{"ay", "m/s²", "+ arriba", "Aceleración del centro de masas, ejes del mundo", kReal, 3,
      [](const CarTelemetry& t) { return static_cast<double>(t.ay); }, nullptr},
    C{"az", "m/s²", "", "Aceleración del centro de masas, ejes del mundo", kReal, 3,
      [](const CarTelemetry& t) { return static_cast<double>(t.az); }, nullptr},
    C{"a_forward", "m/s²", "+ hacia el morro", "Aceleración longitudinal (ejes del coche)", kReal, 3,
      [](const CarTelemetry& t) { return static_cast<double>(t.a_forward); }, nullptr},
    C{"a_right", "m/s²", "+ hacia la derecha", "Aceleración lateral (ejes del coche)", kReal, 3,
      [](const CarTelemetry& t) { return static_cast<double>(t.a_right); }, nullptr},
    C{"steer_target", "", "[−1, 1]; + derecha", "Dirección objetivo (SimInput)", kReal, 5,
      [](const CarTelemetry& t) { return input_axis(t.target.steer); }, nullptr},
    C{"throttle_target", "", "[0, 1]", "Acelerador objetivo (SimInput)", kReal, 5,
      [](const CarTelemetry& t) { return input_axis(t.target.throttle); }, nullptr},
    C{"brake_target", "", "[0, 1]", "Freno objetivo (SimInput)", kReal, 5,
      [](const CarTelemetry& t) { return input_axis(t.target.brake); }, nullptr},
    C{"buttons", "", "máscara", "Botones mantenidos (SimInput; bits abajo); el freno de mano objetivo es el bit 0", kInt, 0,
      [](const CarTelemetry& t) { return static_cast<double>(t.target.buttons); }, nullptr},
    C{"steer", "", "[−1, 1]; + derecha", "Dirección aplicada (ControlState)", kReal, 5,
      [](const CarTelemetry& t) { return static_cast<double>(t.steer); }, nullptr},
    C{"steer_aided", "", "[−1, 1]; + derecha", "Dirección objetivo tras el recorte de la ayuda: la que sigue el limitador de las manos", kReal, 5,
      [](const CarTelemetry& t) { return static_cast<double>(t.steer_aided); }, nullptr},
    C{"steer_digital", "", "0 o 1", "Modo del eje de dirección: 1 digital (teclado), 0 analógico (stick o ratón); el dispositivo no es de la simulación", kInt, 0,
      [](const CarTelemetry& t) { return t.steer_digital ? 1.0 : 0.0; }, nullptr},
    C{"assist_active", "", "0 o 1", "La ayuda de dirección calcula su ventana en el paso (activada, ≥ 15 km/h, hacia delante y con alguna delantera cargada)", kInt, 0,
      [](const CarTelemetry& t) { return t.assist_active ? 1.0 : 0.0; }, nullptr},
    C{"assist_rear_sliding", "", "0 o 1", "La trasera pasa de su pico: el lado hacia el que se va es contravolante y no recorta", kInt, 0,
      [](const CarTelemetry& t) { return t.assist_rear_sliding ? 1.0 : 0.0; }, nullptr},
    C{"assist_right_max", "", "[0, 1]", "Tope de la dirección hacia la derecha que deja la ayuda (1: sin recorte)", kReal, 5,
      [](const CarTelemetry& t) { return static_cast<double>(t.assist_right_max); }, nullptr},
    C{"assist_left_max", "", "[0, 1]", "Tope de la dirección hacia la izquierda (magnitud; 1: sin recorte)", kReal, 5,
      [](const CarTelemetry& t) { return static_cast<double>(t.assist_left_max); }, nullptr},
    C{"steer_speed", "m/s", "≥ 0", "Velocidad horizontal del centro del eje delantero: la de la ayuda y la de las velocidades de giro", kReal, 3,
      [](const CarTelemetry& t) { return static_cast<double>(t.steer_speed); }, nullptr},
    C{"throttle", "", "[0, 1]", "Acelerador aplicado", kReal, 5,
      [](const CarTelemetry& t) { return static_cast<double>(t.throttle); }, nullptr},
    C{"brake", "", "[0, 1]", "Freno aplicado", kReal, 5,
      [](const CarTelemetry& t) { return static_cast<double>(t.brake); }, nullptr},
    C{"handbrake", "", "[0, 1]", "Freno de mano aplicado", kReal, 5,
      [](const CarTelemetry& t) { return static_cast<double>(t.handbrake); }, nullptr},
    C{"pressed", "", "máscara", "Botones que pasan de 0 a 1 en el paso (bits abajo)", kInt, 0,
      [](const CarTelemetry& t) { return static_cast<double>(t.pressed); }, nullptr},
    C{"surface", "", "texto", "Superficie bajo el centro de masas (área de la zona); none sin zona", ColumnKind::Text, 0,
      nullptr, surface_text},
    C{"laps", "", "≥ 0", "Vueltas completas a la pista ciega", kInt, 0,
      [](const CarTelemetry& t) { return static_cast<double>(t.laps); }, nullptr},
    C{"lap_time", "s", "−1 sin vuelta", "Tiempo de la vuelta en curso", kReal, 6,
      [](const CarTelemetry& t) {
          return t.lap_steps < 0 ? -1.0 : static_cast<double>(t.lap_steps) / sim::kStepsPerSecond;
      }, nullptr},
    C{"impact", "m/s", "≥ 0", "Cambio de velocidad del paso que no explican la gravedad ni las fuerzas propias: suspensión y contactos de la carrocería (SimWorld::last_impact)", kReal, 4,
      [](const CarTelemetry& t) { return static_cast<double>(t.impact); }, nullptr},
    C{"gear", "", "−1 R, 0 N, 1..5", "Marcha engranada", kInt, 0,
      [](const CarTelemetry& t) { return static_cast<double>(t.gear); }, nullptr},
    C{"target_gear", "", "−1 R, 0 N, 1..5", "Marcha del cambio en curso (la engranada si no hay cambio)", kInt, 0,
      [](const CarTelemetry& t) { return static_cast<double>(t.target_gear); }, nullptr},
    C{"gearbox", "", "0 automático, 1 secuencial", "Modo de la caja", kInt, 0,
      [](const CarTelemetry& t) { return static_cast<double>(t.gearbox); }, nullptr},
    C{"shifting", "", "0/1", "Cambio en curso: corte de par y embrague abierto", kInt, 0,
      [](const CarTelemetry& t) { return t.shifting ? 1.0 : 0.0; }, nullptr},
    C{"clutch_mode", "", "0 arranque, 1 cerrado", "Modo del embrague automático", kInt, 0,
      [](const CarTelemetry& t) { return static_cast<double>(t.clutch_mode); }, nullptr},
    C{"clutch_open", "", "0/1", "Embrague abierto por el cambio, el freno de mano (D3) o sin marcha", kInt, 0,
      [](const CarTelemetry& t) { return t.clutch_open ? 1.0 : 0.0; }, nullptr},
    C{"rpm", "rpm", "≥ 0", "Giro del motor", kReal, 1,
      [](const CarTelemetry& t) { return static_cast<double>(t.engine_omega) * 30.0 / kPiDouble; }, nullptr},
    C{"boost", "bar", "[0, máx.]", "Presión del turbo sobre la atmosférica", kReal, 4,
      [](const CarTelemetry& t) { return static_cast<double>(t.boost) * 1.0e-5; }, nullptr},
    C{"engine_throttle", "", "[0, 1]", "Acelerador efectivo del motor: el del piloto, el del regulador de ralentí o el del cambio", kReal, 4,
      [](const CarTelemetry& t) { return static_cast<double>(t.engine_throttle); }, nullptr},
    C{"engine_torque", "N·m", "", "Par neto del motor (combustión menos rozamiento), media de los subpasos", kReal, 2,
      [](const CarTelemetry& t) { return static_cast<double>(t.engine_torque); }, nullptr},
    C{"clutch_slip", "rad/s", "", "Deslizamiento del embrague ω_motor − ω_caja (0 sin marcha)", kReal, 3,
      [](const CarTelemetry& t) { return static_cast<double>(t.clutch_slip); }, nullptr},
    C{"clutch_torque", "N·m", "", "Par que pasa el embrague al tren (> 0 empujando el coche)", kReal, 2,
      [](const CarTelemetry& t) { return static_cast<double>(t.clutch_torque); }, nullptr},
    C{"axle_torque_front", "N·m", "", "Par de entrada al eje delantero (caja y viscoacoplador)", kReal, 2,
      [](const CarTelemetry& t) { return static_cast<double>(t.axle_torque_front); }, nullptr},
    C{"axle_torque_rear", "N·m", "", "Par de entrada al eje trasero (caja y viscoacoplador)", kReal, 2,
      [](const CarTelemetry& t) { return static_cast<double>(t.axle_torque_rear); }, nullptr},
    C{"center_slip", "rad/s", "", "Δω del central: giro medio delantero − trasero", kReal, 4,
      [](const CarTelemetry& t) { return static_cast<double>(t.center_slip); }, nullptr},
    C{"viscous_torque", "N·m", "", "Par del viscoacoplador, del eje delantero al trasero (+)", kReal, 2,
      [](const CarTelemetry& t) { return static_cast<double>(t.viscous_torque); }, nullptr},
    C{"lsd_torque_front", "N·m", "≥ 0", "Par de bloqueo del autoblocante delantero", kReal, 2,
      [](const CarTelemetry& t) { return static_cast<double>(t.lsd_torque_front); }, nullptr},
    C{"lsd_torque_rear", "N·m", "≥ 0", "Par de bloqueo del autoblocante trasero", kReal, 2,
      [](const CarTelemetry& t) { return static_cast<double>(t.lsd_torque_rear); }, nullptr},
    C{"drift_hold", "", "0/1", "El automático está retrasando la subida en derrape", kInt, 0,
      [](const CarTelemetry& t) { return t.drift_hold ? 1.0 : 0.0; }, nullptr},
    C{"lcp_iterations", "", "1..8", "Iteraciones del LCP del tren motriz: máximo de los subpasos del paso", kInt, 0,
      [](const CarTelemetry& t) { return static_cast<double>(t.lcp_iterations); }, nullptr},
    C{"lcp_failures", "", "0..4", "Subpasos del paso en que el LCP no convergió (impulsos recortados a su caja)", kInt, 0,
      [](const CarTelemetry& t) { return static_cast<double>(t.lcp_failures); }, nullptr},
    RALLY_WHEEL_COLUMNS(0, "fl"),
    RALLY_WHEEL_COLUMNS(1, "fr"),
    RALLY_WHEEL_COLUMNS(2, "rl"),
    RALLY_WHEEL_COLUMNS(3, "rr"),
};
#undef RALLY_WHEEL_COLUMNS
// clang-format on

// Escribe `value` con `precision` decimales; sin signo si redondea a 0.
void append_real(double value, int precision, std::string& out) {
    char text[64];
    const std::to_chars_result r =
        std::to_chars(text, text + sizeof(text), value, std::chars_format::fixed, precision);
    const char* begin = text;
    if (text[0] == '-') {
        bool zero = true;
        for (const char* p = text + 1; p != r.ptr; ++p) {
            if (*p != '0' && *p != '.') {
                zero = false;
                break;
            }
        }
        if (zero) {
            ++begin;
        }
    }
    out.append(begin, static_cast<std::size_t>(r.ptr - begin));
}

void append_integer(double value, std::string& out) {
    char text[32];
    const std::to_chars_result r =
        std::to_chars(text, text + sizeof(text), static_cast<long long>(value));
    out.append(text, r.ptr);
}

} // namespace

std::span<const TelemetryColumn> telemetry_columns() { return kColumns; }

std::string telemetry_csv_header(std::string_view recording_name, bool zone,
                                 sim::Surface blind_surface) {
    std::string out;
    out += "# rally telemetry\n# format=";
    out += std::to_string(kTelemetryFormatVersion);
    out += "\n# sim_version=";
    out += std::to_string(sim::kSimVersion);
    out += "\n# recording=";
    out += recording_name;
    out += "\n# blind_surface=";
    out += zone ? sim::surface_key(blind_surface) : "none";
    out += '\n';
    bool first = true;
    for (const TelemetryColumn& column : kColumns) {
        if (!first) {
            out += ',';
        }
        out += column.name;
        first = false;
    }
    out += '\n';
    return out;
}

void append_telemetry_row(const sim::CarTelemetry& telemetry, std::string& out) {
    bool first = true;
    for (const TelemetryColumn& column : kColumns) {
        if (!first) {
            out += ',';
        }
        first = false;
        switch (column.kind) {
        case ColumnKind::Integer:
            append_integer(column.number(telemetry), out);
            break;
        case ColumnKind::Real:
            append_real(column.number(telemetry), column.precision, out);
            break;
        case ColumnKind::Text:
            out += column.text(telemetry);
            break;
        }
    }
    out += '\n';
}

std::string telemetry_columns_markdown() {
    std::string md;
    md += "# Columnas de la telemetría\n\n"
          "Generado por `rally_replay columns --md`; no se edita a mano (un test comprueba que "
          "coincide). CSV del formato ";
    md += std::to_string(kTelemetryFormatVersion);
    md += ", escrito por `rally_replay run <grabación> --telemetry <salida.csv>`: una fila por "
          "paso de 1/240 s.\n\n"
          "## Cabecera\n\n"
          "Líneas `# clave=valor` antes de la fila de nombres: `format` (versión del CSV), "
          "`sim_version` (`kSimVersion`), `recording` (nombre de la grabación, sin ruta) y "
          "`blind_surface` (superficie de la pista ciega: `asphalt`, `gravel`, `dirt`, `snow`, "
          "`sand`, o `none` sin zona de pruebas).\n\n"
          "## Convenciones\n\n"
          "- Unidades SI: m, s, m/s, m/s², rad, rad/s. Los scripts muestran km/h y grados.\n"
          "- Números con punto decimal y precisión fija por columna; un valor que redondea a 0 "
          "se escribe sin signo.\n"
          "- Mundo: +y arriba. Coche: adelante = −z local, derecha = +x local, arriba = +y "
          "local.\n"
          "- Posición y velocidad son las del centro de masas del coche (no coincide con el "
          "origen del cuerpo, que está en el suelo entre los ejes).\n"
          "- Ruedas (sufijos `_fl`, `_fr`, `_rl`, `_rr`: delante izquierda, delante derecha, "
          "detrás izquierda, detrás derecha): ejes del contacto, longitudinal hacia delante de "
          "la rueda y lateral hacia su derecha. La carga es la de la suspensión del paso "
          "anterior; las fuerzas, la media de los 4 subpasos. κ y α son los relajados, de la "
          "deformación de la huella (u, w). En el aire, `contact` = 0, "
          "`surface` = none y lo demás a 0 salvo `omega`, `slip_x` y `brake_torque`.\n"
          "- Rumbo: 0 mirando a −z del mundo y crece girando a la derecha (como `psi` de la "
          "zona de pruebas).\n"
          "- Guiñada > 0 girando a la derecha, como `SimInput::steer` > 0.\n"
          "- β = atan2(v_right, v_forward): > 0 cuando la velocidad apunta a la derecha del "
          "morro. Girando a la derecha con el coche deslizando, la velocidad queda a la "
          "izquierda del morro (β < 0). Sin suavizar; a baja velocidad no está definido (los "
          "scripts lo ignoran por debajo de 3 m/s de velocidad horizontal).\n"
          "- Aceleración: dv/dt del centro de masas en el paso, sin la gravedad (apoyado y "
          "quieto, 0). Lateral > 0 hacia la derecha: girando a la derecha es > 0. En el paso "
          "que recoloca, 0.\n"
          "- Bits de `buttons` y `pressed`: 0 freno de mano, 1 subir marcha, 2 bajar marcha, 3 "
          "recolocar, 4 dirección digital, 5 acelerador digital, 6 freno digital, 7 reservado "
          "(era el salto de la caja).\n\n"
          "## Columnas\n\n"
          "| Columna | Unidad | Signo o rango | Descripción |\n"
          "| --- | --- | --- | --- |\n";
    for (const TelemetryColumn& column : kColumns) {
        md += "| `";
        md += column.name;
        md += "` | ";
        md += column.unit[0] != '\0' ? column.unit : "—";
        md += " | ";
        md += column.sign[0] != '\0' ? column.sign : "—";
        md += " | ";
        md += column.description;
        md += " |\n";
    }
    return md;
}

bool TelemetryCsvWriter::open(const std::string& path, std::string_view header,
                              std::string& error) {
    path_ = path;
    const std::filesystem::path target = to_path(path);
    std::error_code ec;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
    }
    file_.open(target, std::ios::binary | std::ios::trunc);
    if (!file_) {
        error = "no se puede crear " + path;
        return false;
    }
    buffer_.reserve(kFlushBytes + kTelemetryRowMaxBytes);
    buffer_.assign(header);
    bytes_ = 0;
    failed_ = false;
    return true;
}

void TelemetryCsvWriter::append(const sim::CarTelemetry& telemetry) {
    append_telemetry_row(telemetry, buffer_);
    if (buffer_.size() >= kFlushBytes) {
        flush();
    }
}

bool TelemetryCsvWriter::flush() {
    file_.write(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
    bytes_ += buffer_.size();
    buffer_.clear();
    if (!file_) {
        failed_ = true;
    }
    return !failed_;
}

bool TelemetryCsvWriter::close(std::string& error) {
    if (file_.is_open()) {
        flush();
        file_.close();
        if (!file_) {
            failed_ = true;
        }
    }
    if (failed_) {
        error = "error al escribir " + path_;
        return false;
    }
    return true;
}

} // namespace rally::io
