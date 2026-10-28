#pragma once

#include "platform/head_motion.h"
#include "platform/interpolation.h"

namespace rally::platform {

// Mirar con la cabeza (lote 4, mirar-cabeza): el conductor y el copiloto giran la cabeza sin
// cambiar el campo de visión. Es presentación: no llega a la simulación ni a la grabación. Sin
// raylib ni SDL; se prueba sin ventana.
//
// Entradas: el stick derecho con forma (platform/input_shaping, zona muerta radial) y el
// desplazamiento del ratón. Salidas: la guiñada (> 0 a la izquierda, como la vista del copiloto)
// y el cabeceo (> 0 arriba), que la cámara (platform/demo_camera) usa con el giro del cuello.

// Topes: guiñada ±90°; cabeceo de +15° (arriba) a −25° (abajo). El stick a fondo llega al tope.
inline constexpr double kLookYawMax = 90.0 * kDegree;
inline constexpr double kLookPitchUp = 15.0 * kDegree;
inline constexpr double kLookPitchDown = 25.0 * kDegree;
// Seguimiento de primer orden hacia el objetivo, exacto en el tiempo del fotograma (la misma
// trayectoria a 30 y a 60 fps): y' = y + (objetivo − y)·(1 − e^(−dt/τ)).
inline constexpr double kLookTau = 0.05; // s
// Ratón: grados por píxel de desplazamiento (relativo, con el cursor capturado). Constante hasta
// la fase 6 (ajustes.txt no tiene clave para ella).
inline constexpr double kLookMouseDegPerPx = 0.15;
// Giro del cuello: el ojo gira alrededor de un pivote 0,10 m detrás de él.
inline constexpr double kNeckPivot = 0.10; // m

// Lo que llega en un fotograma.
struct HeadLookInput {
    float stick_x = 0.0f;     // stick con forma, derecha positiva
    float stick_y = 0.0f;     // stick con forma, arriba positivo
    float mouse_dx_px = 0.0f; // desplazamiento del ratón, derecha positiva
    float mouse_dy_px = 0.0f; // abajo positivo (como la pantalla)
    bool recenter = false;    // botón central: el ratón vuelve al frente (con el mismo seguimiento)
    double dt_s = 0.0;        // tiempo del fotograma
};

class HeadLook {
public:
    // Con el stick fuera de su zona muerta manda el stick; al soltarlo, la cabeza vuelve a donde
    // la dejó el ratón (al frente si no se ha usado). Congelada, no hace nada.
    void update(const HeadLookInput& input);

    // Congelada, ni el stick ni el ratón mueven la cabeza: se queda donde estaba, y al
    // descongelar sigue desde ahí. En el lote 4 nada la congela; la tablet del lote 5 sí.
    void set_frozen(bool frozen) { frozen_ = frozen; }
    bool frozen() const { return frozen_; }

    double yaw() const { return yaw_; }     // rad, > 0 a la izquierda
    double pitch() const { return pitch_; } // rad, > 0 arriba

private:
    double yaw_ = 0.0;
    double pitch_ = 0.0;
    double mouse_yaw_ = 0.0; // dónde la dejó el ratón
    double mouse_pitch_ = 0.0;
    bool frozen_ = false;
};

// Desplazamiento del ojo (ejes del coche: x derecha, y arriba, z atrás) por el giro del cuello:
// el ojo gira alrededor de un pivote kNeckPivot detrás de él. A 90° a la izquierda, 0,10 m a la
// izquierda y 0,10 m hacia atrás; sin giro, nada.
Vec3d neck_eye_offset(double yaw, double pitch);

// Ojo y orientación de la cabeza en ejes del coche, relativos al ojo del asiento: primero el giro
// del cuello (mirada) y después el movimiento de la cabeza de F9 (`f9`, nulo si no se aplica): su
// desplazamiento se suma y su cabeceo y alabeo giran lo que ya mira el cuello. Con la mirada
// quieta, es lo que hacía la cámara antes de la mirada.
struct HeadView {
    Vec3d eye_offset;
    Vec3d forward;
    Vec3d up;
};
HeadView head_view(double yaw, double pitch, const HeadOffset* f9);

} // namespace rally::platform
