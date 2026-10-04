#pragma once

#include "sim/surface.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rally::sim {

// Zona de pruebas de la fase 2 (tarea 2.2): un suelo de 1535 × 1535 m con una superficie por
// área, construido en código a partir de las constantes de este archivo. Solo existe en la
// fase 2; los tramos (fase 3) tienen su propio formato.
//
// Ejes: x hacia el este y z hacia el sur, en metros, con el origen en el centro del suelo.
// "Adelante" de la caja (y del coche) es -z, el norte. Rumbo psi: 0 hacia -z, pi/2 hacia +x;
// positivo gira a la derecha. Orientación de un cuerpo (yaw, alrededor de +y): yaw = -psi.
//
// Áreas (separadas al menos 30 m de tierra):
//   recta de asfalto de 1000 × 12 m (x = 0, z de 650 a -350), seguida de los baches: 150 m de
//   asfalto (ondulaciones y dos baches sueltos, en la geometría) y 150 m de grava
//   (ondulaciones de 0,8 m, en h). Al oeste: círculo, explanada, eslalon, horquilla y rampa;
//   al este: parches de tierra, nieve y arena, pista ciega y cresta.
namespace zone {

// Suelo de Jolt: un HeightFieldShape de kGroundSamples² muestras cada kGroundSpacing metros,
// centrado en el origen.
inline constexpr int kGroundSamples = 2048;
inline constexpr double kGroundSpacing = 0.75;
inline constexpr double kGroundOrigin = -0.5 * (kGroundSamples - 1) * kGroundSpacing;

// Talud perimetral: en los últimos kBermWidth metros antes del borde del suelo, el terreno
// sube a 45° hasta kBermHeight y sigue llano hasta el borde, para que nada caiga al vacío.
// La zona útil es el cuadrado dentro de su pie.
inline constexpr double kBermWidth = 10.0;
inline constexpr double kBermHeight = 2.0;
inline constexpr double kUsableHalfSize = -kGroundOrigin - kBermWidth;

// Semilla fija del microrrelieve (PCG32; un stream por superficie).
inline constexpr std::uint64_t kMicroSeed = 0x2a2a'7e57'2026'1003ull;
// Banda del microrrelieve: seis ondas planas por superficie con longitudes de onda en
// [kMicroMinWavelength, kMicroMaxWavelength], repartidas en seis franjas logarítmicas.
inline constexpr int kMicroWaves = 6;
inline constexpr double kMicroMinWavelength = 2.0;
inline constexpr double kMicroMaxWavelength = 8.0;
// La amplitud de cada superficie se mezcla en esta anchura en los bordes entre áreas.
inline constexpr double kBlendWidth = 2.0;

// Ondulaciones de la grava de los baches (en h): longitud de onda y amplitud.
inline constexpr double kRippleWavelength = 0.8;
inline constexpr double kRippleAmplitude = 0.02;

// Círculo de la prueba de derrape y círculos de los parches (pintados).
inline constexpr double kCircleRadius = 40.0;
inline constexpr double kPatchCircleRadius = 25.0;
// Horquilla del área de horquilla: radio en el eje, calzada y rectas.
inline constexpr double kHairpinRadius = 10.0;
inline constexpr double kHairpinRoadWidth = 6.0;
inline constexpr double kHairpinStraight = 100.0;
// Rampa: pendiente, subida horizontal, rellano, ancho y transiciones parabólicas.
inline constexpr double kRampGrade = 0.15;
inline constexpr double kRampRun = 40.0;
inline constexpr double kRampLanding = 20.0;
inline constexpr double kRampWidth = 8.0;
inline constexpr double kRampTransition = 4.0;
// Cresta: altura y longitud del perfil (un coseno elevado).
inline constexpr double kCrestHeight = 1.2;
inline constexpr double kCrestLength = 25.0;
// Baches de asfalto: ondulaciones (longitud de onda, amplitud y número de periodos) y dos
// baches sueltos (altura y largo, coseno elevado).
inline constexpr double kWaveWavelength = 6.0;
inline constexpr double kWaveAmplitude = 0.04;
inline constexpr int kWavePeriods = 16;
inline constexpr double kBumpHeight = 0.08;
inline constexpr double kBumpLength = 4.0;
// Pista ciega: calzada y margen de la línea de meta a cada lado de la calzada.
inline constexpr double kBlindRoadWidth = 7.0;
inline constexpr double kFinishMargin = 1.0;
// Eslalon: conos, separación y entrada.
inline constexpr int kSlalomCones = 6;
inline constexpr double kSlalomSpacing = 25.0;
inline constexpr double kSlalomEntry = 150.0;

} // namespace zone

// Áreas de la zona, en orden de prioridad: si dos se tocan, manda la de después.
enum class AreaId : std::uint8_t {
    Straight,
    BumpsAsphalt,
    BumpsGravel,
    Circle,
    Plain,
    Slalom,
    Hairpin,
    Ramp,
    Crest,
    PatchDirt,
    PatchSnow,
    PatchSand,
    BlindTrack,
};
inline constexpr int kAreaCount = 13;

// Tramo del eje de una calzada: recta o arco de circunferencia.
struct AxisSegment {
    double x0 = 0.0; // inicio
    double z0 = 0.0;
    double psi0 = 0.0;   // rumbo al inicio
    double length = 0.0; // a lo largo del eje
    double radius = 0.0; // 0 en las rectas
    double turn = 0.0;   // giro total con signo (positivo a la derecha); 0 en las rectas
    double cx = 0.0;     // centro del arco
    double cz = 0.0;
    // Caja del tramo (solo el eje), para no calcular la distancia a los tramos lejanos.
    double min_x = 0.0;
    double max_x = 0.0;
    double min_z = 0.0;
    double max_z = 0.0;
};

struct AxisPoint {
    double x = 0.0;
    double z = 0.0;
    double psi = 0.0;
};

// Área: un rectángulo alineado con los ejes o una calzada (eje y anchura).
struct ZoneArea {
    AreaId id = AreaId::Straight;
    const char* key = "";  // nombre en el JSON
    const char* name = ""; // nombre en español para el overlay
    Surface surface = Surface::Dirt;
    bool road = false;
    // Rectángulo (si !road).
    double cx = 0.0;
    double cz = 0.0;
    double half_x = 0.0;
    double half_z = 0.0;
    // Calzada (si road).
    std::vector<AxisSegment> axis;
    double half_width = 0.0;
    bool closed = false;
    // Caja que contiene el área.
    double min_x = 0.0;
    double max_x = 0.0;
    double min_z = 0.0;
    double max_z = 0.0;
};

// Punto de recolocación: posición en el suelo y orientación (yaw alrededor de +y).
struct RespawnPoint {
    const char* key = "";
    double x = 0.0;
    double z = 0.0;
    double yaw = 0.0;
};

// Línea de meta: centro, dirección de avance (unitaria) y media longitud.
struct FinishLine {
    double x = 0.0;
    double z = 0.0;
    double forward_x = 0.0;
    double forward_z = -1.0;
    double half_length = 0.0;
};

// Una onda plana del microrrelieve: amplitud, número de onda en x y z, y fase.
struct MicroWave {
    double amplitude = 0.0;
    double kx = 0.0;
    double kz = 0.0;
    double phase = 0.0;
};

class TestZone {
public:
    // La pista ciega tiene la superficie `blind_surface`; el resto de la zona no cambia.
    explicit TestZone(Surface blind_surface = Surface::Gravel);

    Surface blind_surface() const { return blind_surface_; }

    // Altura de la geometría (rampa, cresta, baches de asfalto; 0 en el resto). Es la que va
    // al suelo de Jolt, muestreada cada kGroundSpacing.
    double ground_height(double x, double z) const;

    // Superficie en (x, z): la del área de más prioridad que contiene el punto; tierra fuera.
    Surface surface_at(double x, double z) const;
    // Área que contiene (x, z), o -1 si ninguna (fondo de tierra).
    int area_at(double x, double z) const;

    // Microrrelieve h(x, z) en metros, sobre la geometría: la banda de ruido de cada
    // superficie, mezclada en kBlendWidth en los bordes de las áreas, más las ondulaciones
    // de la grava de los baches. Función pura de la zona; la misma para el dibujo y (desde la
    // tarea 2.4) para la rueda.
    double micro_height(double x, double z) const;
    // h y superficie en una sola pasada por las áreas (el dibujo cercano los pide juntos):
    // los mismos valores que micro_height y surface_at.
    struct MicroSample {
        double micro = 0.0;
        Surface surface = Surface::Dirt;
    };
    MicroSample micro_sample(double x, double z) const;
    // Banda de ruido de una superficie en (x, z), sin mezclar.
    double surface_noise(Surface surface, double x, double z) const;
    // Amplitud RMS de partida de la banda de cada superficie.
    static double surface_rms(Surface surface);

    const std::vector<ZoneArea>& areas() const { return areas_; }
    const ZoneArea& area(AreaId id) const { return areas_[static_cast<std::size_t>(id)]; }
    const std::vector<RespawnPoint>& respawns() const { return respawns_; }
    const std::array<std::array<MicroWave, zone::kMicroWaves>, kSurfaceCount>& waves() const {
        return waves_;
    }
    // Índice del punto de recolocación más cercano en el plano (el primero si empatan).
    int nearest_respawn(double x, double z) const;
    const FinishLine& finish_line() const { return finish_; }
    // Salida del cuerpo controlado: el primer punto de recolocación (inicio de la recta).
    const RespawnPoint& start() const { return respawns_.front(); }
    // Punto de recolocación de la pista ciega (50 m antes de la meta): salida de las pruebas
    // a ciegas.
    const RespawnPoint& blind_start() const { return respawns_.back(); }

    // Longitud del eje de una calzada y punto a una distancia s a lo largo de él.
    static double axis_length(const ZoneArea& area);
    static AxisPoint axis_point(const ZoneArea& area, double s);

    // Distancia con signo al borde de un área (negativa dentro).
    static double signed_distance(const ZoneArea& area, double x, double z);

    // Conos del eslalon (solo se dibujan).
    std::vector<AxisPoint> slalom_cones() const;

    // Muestras del suelo de Jolt en orden de filas (índice iz * kGroundSamples + ix, con
    // x = kGroundOrigin + ix * kGroundSpacing) y la superficie de cada celda
    // ((kGroundSamples - 1)² celdas, la de su centro).
    void fill_ground(std::vector<float>& heights, std::vector<std::uint8_t>& surfaces) const;

    // Trazado en JSON para los scripts de Python (rally_replay zone --json).
    std::string to_json() const;

private:
    Surface blind_surface_;
    std::vector<ZoneArea> areas_;
    std::vector<RespawnPoint> respawns_;
    FinishLine finish_;
    std::array<std::array<MicroWave, zone::kMicroWaves>, kSurfaceCount> waves_{};
};

// Hash FNV-1a de las alturas (sus bits) y las superficies de las celdas del suelo.
std::uint64_t ground_hash(const std::vector<float>& heights,
                          const std::vector<std::uint8_t>& surfaces);
// Hash de la zona: lo mismo que ground_hash seguido de la tabla de ondas de h (amplitud,
// números de onda y fase de cada onda de cada superficie, sus bits) y los parámetros de las
// ondulaciones de la grava (longitud de onda y amplitud).
std::uint64_t zone_hash(const std::vector<float>& heights,
                        const std::vector<std::uint8_t>& surfaces, const TestZone& zone);

// Serie de pruebas a ciegas: las cinco superficies tres veces cada una, barajadas con PCG32
// desde `seed` (Fisher-Yates).
inline constexpr int kBlindTrials = 15;
std::array<Surface, kBlindTrials> blind_series(std::uint64_t seed);

} // namespace rally::sim
