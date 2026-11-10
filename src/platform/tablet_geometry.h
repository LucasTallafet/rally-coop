#pragma once

#include "game/direction_changes.h"
#include "platform/screen_layout.h"
#include "platform/tablet_view.h"
#include "sim/stage_track.h"
#include "sim/surface.h"

#include <array>
#include <cstdint>
#include <vector>

namespace rally::platform {

// La geometría de la tablet del copiloto (lote 6, tarea tablet): todo lo que se calcula para
// dibujar el mapa, el perfil y la cinta, en funciones puras sin raylib ni SDL. El dibujo
// (platform/tablet_draw) es una capa fina encima. Los vectores de salida se reutilizan de un
// fotograma al siguiente: tras el primero no reservan memoria.

// --- Sitio de cada parte dentro del panel ---

struct TabletLayout {
    UiRect panel;
    UiRect map;     // la vista cenital
    UiRect profile; // franja de abajo
    UiRect ribbon;  // franja de la derecha
};
// El panel se divide con su relleno: perfil abajo (18 % del alto del panel), cinta a la derecha
// (11 % del ancho) y el mapa en el resto.
TabletLayout tablet_layout(const UiRect& panel, const UiMetrics& m);

// --- Trazos de la carretera por superficie ---

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb&) const = default;
};

// Color y trazo: el color nunca va solo (docs/01 §4). `dash`: {raya, hueco, raya, hueco} en px
// (múltiplos de `unit`); todo a 0 es continuo. `lines`: 1 o 2 líneas paralelas.
struct SurfaceStroke {
    Rgb color;
    std::array<float, 4> dash{};
    int lines = 1;
    bool operator==(const SurfaceStroke&) const = default;
};
// El de cada superficie, con las longitudes de raya a escala `unit` (≥ 1).
SurfaceStroke surface_stroke(sim::Surface surface, float unit);

// --- Mapa ---

struct MapPoint {
    float x = 0.0f, y = 0.0f; // px
    float phase = 0.0f;       // s · ppm: px a lo largo de la carretera, anclado a ella
};
// Un tramo de la carretera con una sola superficie: puntos [first, first + count) de `points`.
struct MapRun {
    sim::Surface surface = sim::Surface::Gravel;
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};
// Una marca de 50 m: su sitio y la normal a la carretera en pantalla (unitaria).
struct MapMark {
    float x = 0.0f, y = 0.0f;
    float nx = 1.0f, ny = 0.0f;
    double s = 0.0;
};
struct MapSegment {
    float x0, y0, x1, y1;
};

struct MapGeometry {
    std::vector<MapPoint> points;
    std::vector<MapRun> runs;
    std::vector<MapMark> marks;
    // Referencia de escala: una barra de `scale_m` metros que mide `scale_px`.
    double scale_m = 50.0;
    float scale_px = 0.0f;
    // Rango de `s` muestreado y paso de la rejilla (m).
    double s_lo = 0.0, s_hi = 0.0, step = 1.0;
};

inline constexpr double kMarkSpacing = 50.0; // m de `s` entre marcas

// El eje del tramo muestreado cada kSpacing m desde s_begin() hasta s_end() (con las
// prolongaciones), calculado una vez al cargar el tramo, y agrupado en bloques con su círculo
// envolvente. Así el mapa dibuja todo el eje que cae en su rectángulo (también los brazos de una
// horquilla que quedan al lado) transformando solo los bloques cuyo círculo toca la vista.
class TabletRoad {
public:
    static constexpr double kSpacing = 0.5; // m entre muestras
    static constexpr int kBlock = 64;       // muestras por bloque

    struct Block {
        double cx = 0.0, cz = 0.0, radius = 0.0;
    };

    // Al cargar el tramo (reserva memoria).
    void build(const sim::StageTrack& track);
    bool empty() const { return x_.empty(); }
    std::size_t size() const { return x_.size(); }
    double s_at(std::size_t i) const { return s_begin_ + kSpacing * static_cast<double>(i); }
    double x(std::size_t i) const { return x_[i]; }
    double z(std::size_t i) const { return z_[i]; }
    sim::Surface surface(std::size_t i) const { return surface_[i]; }
    const std::vector<Block>& blocks() const { return blocks_; }

private:
    double s_begin_ = 0.0;
    std::vector<double> x_, z_;
    std::vector<sim::Surface> surface_;
    std::vector<Block> blocks_;
};

// Paso de la rejilla de `s` para `ppm` px por metro: el mayor de {0,5; 1; 2; 5} m con a lo sumo 2
// px por tramo. Todos dividen a 50: las marcas caen en puntos de la polilínea.
double map_step(double ppm);

// La carretera que cae en el rectángulo del mapa, en la rejilla absoluta de `s` (la polilínea no se
// mueve al avanzar), y sus marcas de 50 m (en s de 0 a la longitud del eje). Todo el eje de
// [s_begin, s_end] que se ve entra, delante y detrás del coche y en cualquier brazo; los huecos
// entre trozos lejanos cortan la polilínea.
void build_map(const TabletRoad& road, const sim::StageTrack& track, const MapView& view,
               MapGeometry& out);

// Un segmento de la carretera en pantalla en `s`: dónde está y su normal (para el cursor y las
// marcas).
bool map_point_at(const sim::StageTrack& track, const MapView& view, double s, MapMark& out);

// La polilínea de una superficie convertida en segmentos según su trazo, con la fase anclada a la
// carretera. Con 2 líneas, cada una a ±`line_offset` px de la polilínea.
void dash_segments(const MapPoint* points, std::uint32_t count, const SurfaceStroke& stroke,
                   float line_offset, std::vector<MapSegment>& out);

// El `s` del eje más cercano al punto (px, py) de la pantalla, desde s_car (nunca por detrás del
// coche) hasta el final. Falso si el punto queda fuera del mapa, si el punto del eje elegido no se
// ve en él o si queda a más de `max_px` píxeles del clic.
bool pick_s(const sim::StageTrack& track, const MapView& view, double s_car, double px, double py,
            double max_px, double& s_out);

// --- Perfil de altura ---

struct ProfilePoint {
    float x = 0.0f, y = 0.0f;
};
struct ProfileGeometry {
    std::vector<ProfilePoint> points;
    double y_lo = 0.0, y_hi = 0.0;             // m: el rango dibujado (≥ kProfileMinSpan)
    double min_height = 0.0, max_height = 0.0; // lo que hay en lo que se ve
    bool ok = false;
};
inline constexpr double kProfileMinSpan = 10.0; // m

// La altura del eje desde `s_car` hasta `s_car + distance`, con el coche a la izquierda del
// rectángulo (con `pad` px de margen). El rango vertical se ajusta a lo que se ve, con un mínimo
// de kProfileMinSpan metros (centrado) para que un tramo llano no parezca una sierra.
void build_profile(const sim::StageTrack& track, const UiRect& rect, int pad, double s_car,
                   double distance, ProfileGeometry& out);

// --- Cinta de ruta ---

inline constexpr double kRibbonLength = 500.0; // m por delante del coche

struct RibbonMark {
    float y = 0.0f;
    int direction = 0; // +1 derecha, −1 izquierda
    double s = 0.0;
};
struct RibbonGeometry {
    std::vector<RibbonMark> marks;
    // Rayas cada 100 m (y de px) y el cursor.
    std::array<float, 5> ticks{};
    int tick_count = 0;
    bool cursor_visible = false; // hay cursor
    bool cursor_beyond = false;  // pasa de kRibbonLength: se enseña pegado al borde de arriba
    float cursor_y = 0.0f;
    float x = 0.0f; // el eje de la cinta
    float bottom = 0.0f, top = 0.0f;
};

// Los cambios de dirección con s_start en [s_car, s_car + 500]; el coche abajo y los 500 m hacia
// arriba. `cursor_s` solo se mira con `cursor_valid`.
void build_ribbon(const std::vector<game::DirectionChange>& changes, const UiRect& rect, int pad,
                  double s_car, bool cursor_valid, double cursor_s, RibbonGeometry& out);

} // namespace rally::platform
