#pragma once

// Apoyo de los tests del panel (lote 7): un rasterizador mínimo de triángulos y un escritor de PNG
// sin compresión (bloques «stored» de deflate), para sacar capturas del interior sin ventana. Solo
// se usa si RALLY_PANEL_CAPTURES apunta a una carpeta; los tests no dependen del resultado.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace rally::tests {

struct RgbImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb;
    std::vector<float> inv_depth; // 1 / adelante, por píxel: 0 = nada dibujado

    RgbImage(int w, int h, std::array<std::uint8_t, 3> background)
        : width(w), height(h), rgb(static_cast<std::size_t>(w) * h * 3),
          inv_depth(static_cast<std::size_t>(w) * h, 0.0f) {
        for (std::size_t i = 0; i < rgb.size(); i += 3) {
            rgb[i] = background[0];
            rgb[i + 1] = background[1];
            rgb[i + 2] = background[2];
        }
    }
};

struct ScreenVertex {
    double x = 0.0; // píxeles
    double y = 0.0;
    double inv_z = 0.0;
};

// Triángulo con prueba de profundidad (1/z interpolado en pantalla: correcto en perspectiva).
inline void fill_triangle(RgbImage& img, const ScreenVertex& a, const ScreenVertex& b,
                          const ScreenVertex& c, std::array<std::uint8_t, 3> color) {
    const double area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    if (std::abs(area) < 1e-12) {
        return;
    }
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int x1 = std::min(img.width - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int y1 = std::min(img.height - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double px = x + 0.5;
            const double py = y + 0.5;
            const double w0 = ((b.x - px) * (c.y - py) - (c.x - px) * (b.y - py)) / area;
            const double w1 = ((c.x - px) * (a.y - py) - (a.x - px) * (c.y - py)) / area;
            const double w2 = 1.0 - w0 - w1;
            if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) {
                continue;
            }
            const float inv = static_cast<float>(w0 * a.inv_z + w1 * b.inv_z + w2 * c.inv_z);
            const std::size_t idx = static_cast<std::size_t>(y) * img.width + x;
            if (inv < img.inv_depth[idx]) {
                continue;
            }
            img.inv_depth[idx] = inv;
            img.rgb[idx * 3] = color[0];
            img.rgb[idx * 3 + 1] = color[1];
            img.rgb[idx * 3 + 2] = color[2];
        }
    }
}

inline std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            t[n] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

inline void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

inline void chunk(std::vector<std::uint8_t>& out, const char* type,
                  const std::vector<std::uint8_t>& data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32(body.data(), body.size()));
}

inline bool write_png(const std::string& path, const RgbImage& img) {
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(img.height) * (img.width * 3 + 1));
    for (int y = 0; y < img.height; ++y) {
        raw.push_back(0);
        const auto* row = &img.rgb[static_cast<std::size_t>(y) * img.width * 3];
        raw.insert(raw.end(), row, row + img.width * 3);
    }
    std::vector<std::uint8_t> z = {0x78, 0x01};
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    for (std::size_t pos = 0; pos < raw.size(); pos += 65535) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - pos);
        z.push_back(pos + n >= raw.size() ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n & 0xFF));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n & 0xFF));
        z.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                 raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
    }
    put32(z, (b << 16) | a);
    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> header;
    put32(header, static_cast<std::uint32_t>(img.width));
    put32(header, static_cast<std::uint32_t>(img.height));
    header.insert(header.end(), {8, 2, 0, 0, 0});
    chunk(png, "IHDR", header);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});
    std::FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "wb") != 0 || file == nullptr) {
        return false;
    }
    std::fwrite(png.data(), 1, png.size(), file);
    std::fclose(file);
    return true;
}

// Recorta [x0, x1) × [y0, y1) y lo amplía `scale` veces (vecino más próximo).
inline RgbImage crop_scaled(const RgbImage& src, int x0, int y0, int x1, int y1, int scale) {
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, src.width);
    y1 = std::min(y1, src.height);
    RgbImage out((x1 - x0) * scale, (y1 - y0) * scale, {0, 0, 0});
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const std::size_t s = (static_cast<std::size_t>(y0 + y / scale) * src.width +
                                   static_cast<std::size_t>(x0 + x / scale)) * 3;
            const std::size_t d = (static_cast<std::size_t>(y) * out.width + x) * 3;
            out.rgb[d] = src.rgb[s];
            out.rgb[d + 1] = src.rgb[s + 1];
            out.rgb[d + 2] = src.rgb[s + 2];
        }
    }
    return out;
}

} // namespace rally::tests
