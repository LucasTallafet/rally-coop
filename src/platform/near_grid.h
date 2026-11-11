#pragma once

#include "platform/camera_space.h"
#include "platform/near_field.h"
#include "platform/near_ring.h"
#include "sim/surface.h"

#include <raylib.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

namespace rally::platform {

// Shader de recorte por distancia (el del suelo de ZoneDraw): color por vértice y descarta por la
// distancia horizontal al ojo. side = 1 descarta lo que está a menos de `radius` (lejos);
// side = −1, lo que está a más (cerca). Con radius = 0 y side = 1 no descarta nada. Necesita la
// ventana abierta.
class ClipShader {
public:
    ClipShader();
    ~ClipShader();
    ClipShader(const ClipShader&) = delete;
    ClipShader& operator=(const ClipShader&) = delete;

    // Dibuja `mesh` desplazada a `origin` (render relativo) con el recorte.
    void draw(const Mesh& mesh, const Vec3d& origin, float radius, float side, const Vec3d& eye);

private:
    Material material_{};
    int loc_radius_ = -1;
    int loc_side_ = -1;
    int loc_eye_ = -1;
};

// Desplazamiento de polígonos de OpenGL, que rlgl no expone: se pide a SDL. Sin él (no debería
// pasar con OpenGL 3.3) lo que va encima se dibuja igual, con riesgo de parpadeo lejos.
class PolygonOffset {
public:
    PolygonOffset();
    bool available() const { return offset_ != nullptr && enable_ != nullptr; }
    // Activa el desplazamiento (factor, unidades) para lo que se dibuje a partir de aquí.
    void set(float factor, float units) const;
    void off() const;

private:
    using Offset = void(__stdcall*)(float, float);
    using Capability = void(__stdcall*)(unsigned int);
    Offset offset_ = nullptr;
    Capability enable_ = nullptr;
    Capability disable_ = nullptr;
};

// Lo que cuesta actualizar la rejilla en un fotograma.
struct NearStats {
    double near_ms = 0.0;   // update entero
    double upload_ms = 0.0; // dentro de él, rlUpdateTexture
    int cells = 0;          // celdas calculadas
    int uploads = 0;        // subidas a la textura
};

// Rejilla cercana en anillo (la de ZoneDraw, extraída en la tarea tramo-dibujo para que la use
// también StageDraw): una malla fija de cells² vértices cada `spacing` m y una textura RGBA32F
// con (altura, h, superficie, válido) de cada celda, que lleva NearRing (platform/near_ring: el
// anillo y sus cuentas, sin ventana). Aquí solo se sube lo que entra y se dibuja. El shader de
// vértices lee la altura, apaga h entre fade_start y radius y sombrea con la normal de los
// vecinos; el de fragmentos descarta lo que pasa de `radius` y lo que no es válido. Necesita la
// ventana abierta.
class NearGrid {
public:
    // `colors`: color de cada superficie (índice de sim::Surface). `extra_colors`: colores de los
    // índices siguientes (kSurfaceCount, ...), hasta 3 (el tramo: roca, tierra de cuneta y talud);
    // ZoneDraw no pasa ninguno y su dibujo no cambia.
    NearGrid(double radius, double fade_start, double spacing,
             const std::array<Color, sim::kSurfaceCount>& colors,
             const Color* extra_colors = nullptr, int extra_count = 0);
    ~NearGrid();
    NearGrid(const NearGrid&) = delete;
    NearGrid& operator=(const NearGrid&) = delete;

    // La próxima actualización calcula toda la rejilla (el suelo ha cambiado).
    void invalidate() { ring_.invalidate(); }
    int cells_per_side() const { return ring_.cells(); }
    int triangle_count() const { return near_.triangleCount; }

    // Actualiza la textura con `sample(x, z)` (NearTexel de la celda del mundo en metros) para las
    // celdas que entran, y dibuja la rejilla. Sin reservas por fotograma.
    template <class Sample>
    NearStats draw(const Vec3d& eye, const Vec3d& origin, const Sample& sample) {
        NearStats stats;
        const auto start = std::chrono::steady_clock::now();
        stats.cells = ring_.update(eye, sample);
        upload(stats);
        stats.near_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        draw_mesh(eye, origin);
        return stats;
    }

private:
    void upload(NearStats& stats);
    void draw_mesh(const Vec3d& eye, const Vec3d& origin);

    NearRing ring_;
    double spacing_;
    Mesh near_{};
    Shader shader_{};
    Material material_{};
    unsigned int texture_ = 0;
    int loc_anchor_ = -1;
    int loc_eye_ = -1;
};

} // namespace rally::platform
