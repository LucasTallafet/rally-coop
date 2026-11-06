#pragma once

#include <cmath>
#include <cstdint>

namespace rally::platform {

// Rejilla cercana alrededor del ojo (la de ZoneDraw y la de StageDraw): la parte que se puede
// probar sin ventana. Cada celda del mundo lleva su altura, el microrrelieve h y la superficie;
// la malla de la GPU interpola la altura entre vértices de la rejilla (kNearSpacing) y apaga h
// entre kNearFadeStart y kNearRadius.

// Una celda de la textura de la rejilla. `valid` 0 marca un hueco (sin suelo): el triángulo
// que lo tiene como vértice de arranque no se dibuja.
struct NearTexel {
    float height = 0.0f;  // altura del suelo, en el mundo
    float micro = 0.0f;   // h: microrrelieve
    float surface = 0.0f; // índice de sim::Surface
    float valid = 1.0f;
};

// Desvanecido de h con la distancia horizontal `dist` al ojo: 1 hasta `start`, 0 desde `radius`,
// con smoothstep. Es la misma cuenta que el shader de vértices.
inline double near_fade(double dist, double start, double radius) {
    double t = (radius - dist) / (radius - start);
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    return t * t * (3.0 - 2.0 * t);
}

// Lo que dibuja la rejilla en (x, z), con el ojo en (eye_x, eye_z): la altura del suelo más h
// apagada por el desvanecido, en cada vértice de la rejilla, e interpolada en el triángulo de la
// malla que contiene el punto (las diagonales de ZoneDraw: a–c, con a = (i, j) y c = (i+1, j+1)).
// `sample(gx, gz)` da la celda del mundo (gx, gz) (su posición es gx·spacing, gz·spacing).
// `ok` es false si algún vértice es un hueco.
template <class Sample>
double near_drawn_height(const Sample& sample, double spacing, double radius, double fade_start,
                         double eye_x, double eye_z, double x, double z, bool& ok) {
    const auto gx = static_cast<std::int64_t>(std::floor(x / spacing));
    const auto gz = static_cast<std::int64_t>(std::floor(z / spacing));
    const double u = x / spacing - static_cast<double>(gx);
    const double v = z / spacing - static_cast<double>(gz);
    ok = true;
    const auto vertex = [&](std::int64_t i, std::int64_t j) {
        const NearTexel t = sample(i, j);
        ok = ok && t.valid > 0.5f;
        const double px = static_cast<double>(i) * spacing - eye_x;
        const double pz = static_cast<double>(j) * spacing - eye_z;
        const double fade = near_fade(std::sqrt(px * px + pz * pz), fade_start, radius);
        return static_cast<double>(t.height) + static_cast<double>(t.micro) * fade;
    };
    const double ha = vertex(gx, gz);
    const double hc = vertex(gx + 1, gz + 1);
    if (u >= v) {
        // Triángulo a, c, b (b = (i+1, j)).
        const double hb = vertex(gx + 1, gz);
        return ha + u * (hb - ha) + v * (hc - hb);
    }
    // Triángulo a, d, c (d = (i, j+1)).
    const double hd = vertex(gx, gz + 1);
    return ha + u * (hc - hd) + v * (hd - ha);
}

} // namespace rally::platform
