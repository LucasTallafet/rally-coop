#pragma once

#include "platform/interpolation.h"
#include "platform/near_field.h"

#include <cstdint>
#include <vector>

namespace rally::platform {

// La contabilidad de la rejilla cercana en anillo (la de ZoneDraw::update_near, extraída en la
// tarea tramo-dibujo para poder probarla sin ventana): un cuadrado de cells² celdas del mundo
// (de paso `spacing`) anclado al mundo y direccionado en anillo (índice del mundo módulo cells).
// Cada fotograma solo calcula las filas y columnas que entran; lo que hay que subir a la textura
// de la GPU queda en pending(). Sin reservas tras el constructor.
class NearRing {
public:
    struct Upload {
        enum class Kind : std::uint8_t { Full, Row, Column };
        Kind kind = Kind::Full;
        int slot = 0; // fila o columna del anillo
    };

    NearRing(double radius, double spacing);

    int cells() const { return cells_; }
    double spacing() const { return spacing_; }
    // La próxima actualización calcula toda la rejilla.
    void invalidate() { valid_ = false; }

    using SampleFn = NearTexel (*)(const void*, double, double);
    // Mueve el anclaje al ojo y calcula con `fn(sample, x, z)` las celdas que entran. Devuelve
    // cuántas calculó. Deja las subidas pendientes en pending().
    int update(const Vec3d& eye, const void* sample, SampleFn fn);

    template <class Sample>
    int update(const Vec3d& eye, const Sample& sample) {
        return update(eye, &sample, [](const void* p, double x, double z) {
            return (*static_cast<const Sample*>(p))(x, z);
        });
    }

    const std::vector<Upload>& pending() const { return pending_; }
    // Los texels (4 floats por celda) en el orden de la textura, y la columna `slot` copiada a un
    // búfer propio (para subirla).
    const float* texels() const { return texels_.data(); }
    const float* column(int slot);
    std::int64_t anchor_x() const { return anchor_x_; }
    std::int64_t anchor_z() const { return anchor_z_; }
    int slot(std::int64_t g) const {
        const std::int64_t m = g % cells_;
        return static_cast<int>(m < 0 ? m + cells_ : m);
    }
    // La celda del mundo (gx, gz), que tiene que estar dentro del anillo actual.
    NearTexel texel(std::int64_t gx, std::int64_t gz) const;

private:
    double spacing_;
    int cells_ = 0;
    std::vector<float> texels_;
    std::vector<float> column_;
    std::vector<Upload> pending_;
    bool valid_ = false;
    std::int64_t anchor_x_ = 0;
    std::int64_t anchor_z_ = 0;
};

} // namespace rally::platform
