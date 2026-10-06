#pragma once

#include <array>

namespace rally::platform {

// Cabeza del conductor (tarea 2.7): un resorte-amortiguador por eje movido por la fuerza
// específica del coche (la que nota el cuerpo: aceleración más gravedad, en ejes del coche, menos
// la de reposo). Solo mueve la cámara del ojo: no toca la simulación. Se integra una vez por paso
// de 1/240 s (no por fotograma), así que se ve igual a 30 y a 60 fps; se dibuja interpolando
// entre el paso anterior y el último con el mismo alpha que la pose del coche.
//
// Por eje, x'' = ω²·(x_obj − x) − 2ζω·x' (Euler semiimplícito), con x_obj = ganancia × entrada
// / g. La cabeza va contra la fuerza: frenando, hacia delante; en una curva a la derecha, a la
// izquierda; en el aire, hacia arriba. Cada componente de la entrada se recorta a ±2 g antes de
// integrar (el golpe del aterrizaje lo dan la vibración y el sonido) y lo que se dibuja es
// L·tanh(x/L): los límites se alcanzan de forma asintótica, sin tope seco.

inline constexpr double kHeadGravity = 9.81;     // m/s²
inline constexpr double kHeadFrequency = 2.5;    // Hz
inline constexpr double kHeadDamping = 0.6;      // fracción del crítico
inline constexpr double kHeadInputClamp = 2.0;   // g, por componente
inline constexpr double kHeadStep = 1.0 / 240.0; // s: el paso de la simulación

// Ganancias (desplazamiento por g de entrada) y límites. Intensidad: kHeadIntensity las escala
// todas (1 = las de partida del plan de la 2.7).
inline constexpr double kHeadIntensity = 1.0;
inline constexpr double kDegree = 3.14159265358979323846 / 180.0;
inline constexpr double kHeadGainForward = 0.020;        // m/g
inline constexpr double kHeadGainRight = 0.025;          // m/g
inline constexpr double kHeadGainUp = 0.010;             // m/g
inline constexpr double kHeadGainRoll = 1.0 * kDegree;   // rad/g
inline constexpr double kHeadGainPitch = 1.0 * kDegree;  // rad/g
inline constexpr double kHeadLimitForward = 0.03;        // m
inline constexpr double kHeadLimitRight = 0.04;          // m
inline constexpr double kHeadLimitUp = 0.02;             // m
inline constexpr double kHeadLimitRoll = 2.0 * kDegree;  // rad
inline constexpr double kHeadLimitPitch = 2.0 * kDegree; // rad

// Fuerza específica menos la de reposo, en ejes del coche (m/s²): (f·adelante, f·derecha,
// f·arriba − g), con f = a + (0, g, 0) en ejes del mundo. En llano y parado, 0; en el aire,
// up = −g; parado mirando cuesta arriba, forward = g·sin θ.
struct HeadInput {
    double forward = 0.0;
    double right = 0.0;
    double up = 0.0;
};

// Desplazamiento del ojo en ejes del coche (m) y giro de la cabeza (rad), tras el límite suave.
// roll > 0: la cabeza se inclina a la derecha (la parte de arriba); pitch > 0: mira hacia arriba.
struct HeadOffset {
    double forward = 0.0;
    double right = 0.0;
    double up = 0.0;
    double roll = 0.0;
    double pitch = 0.0;
};

HeadOffset lerp(const HeadOffset& a, const HeadOffset& b, double t);

class HeadMotion {
public:
    // Un paso de 1/240 s.
    void step(const HeadInput& input);
    // Cabeza en reposo (al recolocar y al reconstruir el mundo): sin salto de interpolación.
    void reset();
    const HeadOffset& previous() const { return previous_; }
    const HeadOffset& current() const { return current_; }
    // Lo que se dibuja: entre el paso anterior y el último, con el alpha de la pose del coche.
    HeadOffset drawn(double alpha) const { return lerp(previous_, current_, alpha); }

    // Estado de los resortes sin el límite suave (para los tests): forward, right, up, roll y
    // pitch.
    static constexpr int kAxes = 5;
    const std::array<double, kAxes>& raw() const { return x_; }

private:
    std::array<double, kAxes> x_{};
    std::array<double, kAxes> v_{};
    HeadOffset previous_;
    HeadOffset current_;
};

} // namespace rally::platform
