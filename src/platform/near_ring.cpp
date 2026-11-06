#include "platform/near_ring.h"

#include <cmath>
#include <cstring>

namespace rally::platform {

NearRing::NearRing(double radius, double spacing) : spacing_(spacing) {
    cells_ = static_cast<int>(2.0 * radius / spacing) + 1;
    texels_.assign(static_cast<std::size_t>(cells_) * cells_ * 4, 0.0f);
    column_.assign(static_cast<std::size_t>(cells_) * 4, 0.0f);
    pending_.reserve(static_cast<std::size_t>(2 * cells_ + 2));
}

const float* NearRing::column(int slot_index) {
    const int n = cells_;
    for (int r = 0; r < n; ++r) {
        std::memcpy(column_.data() + 4 * r,
                    texels_.data() + static_cast<std::size_t>(4 * (r * n + slot_index)),
                    4 * sizeof(float));
    }
    return column_.data();
}

NearTexel NearRing::texel(std::int64_t gx, std::int64_t gz) const {
    const float* t = texels_.data() + 4 * (static_cast<std::size_t>(slot(gz)) * cells_ + slot(gx));
    NearTexel out;
    out.height = t[0];
    out.micro = t[1];
    out.surface = t[2];
    out.valid = t[3];
    return out;
}

int NearRing::update(const Vec3d& eye, const void* sample, SampleFn fn) {
    pending_.clear();
    const int n = cells_;
    const auto floor_index = [this](double v) {
        return static_cast<std::int64_t>(std::floor(v / spacing_));
    };
    const std::int64_t ax = floor_index(eye.x) - n / 2;
    const std::int64_t az = floor_index(eye.z) - n / 2;
    if (valid_ && ax == anchor_x_ && az == anchor_z_) {
        return 0; // la rejilla no se ha movido: la textura sirve tal cual
    }
    const std::int64_t dx = ax - anchor_x_;
    const std::int64_t dz = az - anchor_z_;
    const bool full = !valid_ || dx <= -n || dx >= n || dz <= -n || dz >= n;
    anchor_x_ = ax;
    anchor_z_ = az;
    int computed = 0;
    // La celda (gx, gz) del mundo, en su sitio del anillo.
    const auto compute = [&](std::int64_t gx, std::int64_t gz, int sx, int sz) {
        ++computed;
        const NearTexel t =
            fn(sample, static_cast<double>(gx) * spacing_, static_cast<double>(gz) * spacing_);
        float* texel = texels_.data() + 4 * (static_cast<std::size_t>(sz) * n + sx);
        texel[0] = t.height;
        texel[1] = t.micro;
        texel[2] = t.surface;
        texel[3] = t.valid;
    };
    if (full) {
        // Al empezar o tras un salto (recolocar): toda la rejilla.
        for (int j = 0; j < n; ++j) {
            const std::int64_t gz = anchor_z_ + j;
            const int sz = slot(gz);
            for (int i = 0; i < n; ++i) {
                const std::int64_t gx = anchor_x_ + i;
                compute(gx, gz, slot(gx), sz);
            }
        }
        pending_.push_back({Upload::Kind::Full, 0});
        valid_ = true;
        return computed;
    }
    // Solo entran |dz| filas y |dx| columnas: las filas se suben enteras y las columnas, sin
    // las celdas de las filas nuevas, también enteras.
    const std::int64_t row_first = dz > 0 ? anchor_z_ + n - dz : anchor_z_;
    const std::int64_t row_end = dz > 0 ? anchor_z_ + n : anchor_z_ - dz;
    const std::int64_t col_first = dx > 0 ? anchor_x_ + n - dx : anchor_x_;
    const std::int64_t col_end = dx > 0 ? anchor_x_ + n : anchor_x_ - dx;
    for (std::int64_t gz = row_first; gz < row_end; ++gz) {
        const int sz = slot(gz);
        int sx = slot(anchor_x_);
        for (int i = 0; i < n; ++i) {
            compute(anchor_x_ + i, gz, sx, sz);
            sx = sx + 1 == n ? 0 : sx + 1;
        }
        pending_.push_back({Upload::Kind::Row, sz});
    }
    for (std::int64_t gx = col_first; gx < col_end; ++gx) {
        const int sx = slot(gx);
        int sz = slot(anchor_z_);
        for (int j = 0; j < n; ++j) {
            const std::int64_t gz = anchor_z_ + j;
            if (gz < row_first || gz >= row_end) {
                compute(gx, gz, sx, sz);
            }
            sz = sz + 1 == n ? 0 : sz + 1;
        }
        pending_.push_back({Upload::Kind::Column, sx});
    }
    return computed;
}

} // namespace rally::platform
