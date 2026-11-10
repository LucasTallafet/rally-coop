#pragma once

#include "platform/screen_layout.h"
#include "sim/stage_track.h"

#include <cstdint>

namespace rally::platform {

// La vista del mapa de la tablet del copiloto (lote 6, tarea tablet): rumbo suavizado, velocidad de
// avance, nivel de zoom y la transformación del mundo a píxeles. Solo lee la `s` y la `t` que manda
// el host (net::SampledState) y el eje del tramo; nunca una proyección propia. Es presentación:
// sin raylib ni SDL, sin tocar la simulación, y se prueba sin ventana.

// Cómo se orienta el mapa. A: por la tangente del eje en `s` (no depende del morro ni de la
// velocidad: un derrape o un trompo no la mueven). B: por la dirección del coche al punto del eje
// kAheadDistance por delante (en las horquillas el mapa no da media vuelta).
enum class MapOrientation : std::uint8_t { Axis, Ahead };

inline constexpr double kAheadDistance = 40.0; // m (docs/01 §4: 30–50 m)

// Zoom: tres niveles fijos para ver unos 9 s por delante de la flecha. Distancia (m) de la flecha
// al borde de arriba del mapa en cada uno, y la velocidad (de avance por la carretera) hasta la que
// vale cada uno: 40 y 80 km/h. Histéresis del 10 % en los cambios de nivel.
inline constexpr int kZoomLevels = 3;
inline constexpr double kZoomDistance[kZoomLevels] = {100.0, 200.0, 400.0};
inline constexpr double kZoomSpeed[kZoomLevels - 1] = {40.0 / 3.6, 80.0 / 3.6}; // m/s
inline constexpr double kZoomHysteresis = 0.10;
// Nivel que toca a `speed_mps` (m/s) viniendo del nivel `current`: sube al pasar el umbral +10 %
// y baja al pasar el umbral −10 %; puede saltar más de un nivel.
int zoom_level_for(double speed_mps, int current);

// Constantes de suavizado (primer orden, exacto en el tiempo del fotograma: la misma trayectoria a
// 30 y a 60 fps). Provisionales: se ajustan con la prueba de Lucas.
inline constexpr double kHeadingTau = 0.08; // s: rumbo del mapa
inline constexpr double kSpeedTau = 0.4;    // s: velocidad de avance
inline constexpr double kZoomTau = 0.15;    // s: ln de la distancia (~95 % en 0,45 s)
// Un salto de `s` mayor que esto (más lo que se avanza a kMaxSpeed en el fotograma) es una
// recolocación: la velocidad de ese fotograma no cuenta; el rumbo lo sigue suavizando el filtro (no
// salta: solo la primera muestra lo fija).
inline constexpr double kRelocationJump = 30.0; // m
inline constexpr double kMaxSpeed = 100.0;      // m/s

// Dónde está la flecha: a esta fracción del alto del mapa desde arriba (el tercio inferior es
// ≥ 0,667).
inline constexpr double kArrowFraction = 0.70;

// Transformación del mundo al mapa. Ejes del mundo: x al este, z al sur; rumbo 0 hacia −z,
// positivo a la derecha. La flecha (el coche) está en (arrow_x, arrow_y) y apunta hacia arriba.
struct MapView {
    UiRect rect;
    double arrow_x = 0.0, arrow_y = 0.0; // px
    double ppm = 1.0;                    // px por metro
    double cx = 0.0, cz = 0.0;           // el coche, en el mundo
    double heading = 0.0;                // rad: hacia donde mira «arriba» del mapa
    double distance = kZoomDistance[0];  // m de la flecha al borde de arriba
    // px por metro del nivel de zoom de destino: la fase de los trazos discontinuos se ancla a él
    // y no al ppm del momento, para que las rayas no resbalen por la carretera durante la
    // transición de zoom. 0: el ppm del momento.
    double phase_ppm = 0.0;

    void to_screen(double x, double z, double& px, double& py) const;
    void to_world(double px, double py, double& x, double& z) const;
};

class TabletView {
public:
    // Una vez por fotograma, con la `s` y la `t` interpoladas del host. `now_s` es el reloj del
    // copiloto (solo para dt; lo que no sea creciente da dt = 0). La primera llamada y una
    // recolocación fijan el rumbo de golpe.
    void update(const sim::StageTrack& track, double s, double t, double now_s,
                MapOrientation orientation);
    // Olvida todo (mundo nuevo): la siguiente update fija el rumbo.
    void reset() { *this = TabletView(); }

    bool initialized() const { return initialized_; }
    double s() const { return s_; }
    double t() const { return t_; }
    double heading() const { return heading_; }
    double speed() const { return speed_; }
    int zoom_level() const { return level_; }
    // Distancia mostrada (m): la del nivel, suavizada.
    double distance() const;
    // El coche en el mundo (en el eje a `s`, desplazado `t` a la derecha).
    double car_x() const { return car_x_; }
    double car_z() const { return car_z_; }
    // Se produjo una recolocación en la última update.
    bool relocated() const { return relocated_; }

    // La transformación para el rectángulo del mapa con el estado actual.
    MapView map_view(const UiRect& map_rect) const;

private:
    bool initialized_ = false;
    bool relocated_ = false;
    double now_s_ = 0.0;
    double s_ = 0.0;
    double t_ = 0.0;
    double speed_ = 0.0;
    double heading_ = 0.0;
    int level_ = 0;
    double log_distance_ = 0.0; // ln de la distancia mostrada
    double car_x_ = 0.0, car_z_ = 0.0;
};

// El rumbo objetivo del mapa: A o B (función pura, sin filtro). `ahead_s` ya acotado por quien
// llama a [s_begin, s_end] si hace falta; aquí se acota de nuevo.
double target_heading(const sim::StageTrack& track, double s, double t, MapOrientation orientation);

// Posición del coche en el mundo: el eje a `s` más `t` a su derecha.
void car_position(const sim::StageTrack& track, double s, double t, double& x, double& z);

} // namespace rally::platform
