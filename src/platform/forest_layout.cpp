#include "platform/forest_layout.h"

#include "sim/pcg32.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rally::platform {

namespace {

struct Vec {
    float x;
    float y;
    float z;
};

struct Rgb {
    float r;
    float g;
    float b;
};

constexpr Rgb kTrunk{110.0f, 80.0f, 52.0f};
constexpr Rgb kLowerCrown{58.0f, 108.0f, 60.0f};
constexpr Rgb kUpperCrown{72.0f, 126.0f, 66.0f};

// Copa inferior, copa superior y tronco (radio en la base, alturas de la base y de la punta).
constexpr float kTrunkRadius = 0.25f;
constexpr float kTrunkTop = 2.0f;
constexpr float kLowerRadius = 2.0f;
constexpr float kLowerBase = 1.5f;
constexpr float kLowerApex = 6.0f;
constexpr float kUpperRadius = 1.5f;
constexpr float kUpperBase = 4.0f;
constexpr float kUpperApex = kTreeHeight;

Vec ring(float radius, float y, int index, int sides) {
    const float angle =
        2.0f * std::numbers::pi_v<float> * static_cast<float>(index) / static_cast<float>(sides);
    return Vec{radius * std::cos(angle), y, radius * std::sin(angle)};
}

class TreeBuilder {
public:
    explicit TreeBuilder(std::array<TreeVertex, kTreeVertices>& out) : out_(out) {}

    // Un triángulo con el color de la cara aclarado según su normal frente a la luz fija.
    void triangle(Vec a, Vec b, Vec c, Rgb color) {
        const Vec u{b.x - a.x, b.y - a.y, b.z - a.z};
        const Vec v{c.x - a.x, c.y - a.y, c.z - a.z};
        const Vec n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
        const float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        const float lit =
            (n.x * kLight.x + n.y * kLight.y + n.z * kLight.z) / (length * light_length_);
        const float shade = 0.5f + 0.5f * std::max(0.0f, lit);
        for (const Vec& p : {a, b, c}) {
            out_[static_cast<std::size_t>(next_++)] =
                TreeVertex{p.x,
                           p.y,
                           p.z,
                           static_cast<std::uint8_t>(color.r * shade),
                           static_cast<std::uint8_t>(color.g * shade),
                           static_cast<std::uint8_t>(color.b * shade),
                           255};
        }
    }

    // Cono con base: lados de (punta, i + 1, i) y base de (centro, i, i + 1), hacia abajo.
    void cone(float radius, float base, float apex, Rgb color) {
        const Vec top{0.0f, apex, 0.0f};
        const Vec center{0.0f, base, 0.0f};
        for (int i = 0; i < kTreeConeSides; ++i) {
            const Vec p0 = ring(radius, base, i, kTreeConeSides);
            const Vec p1 = ring(radius, base, i + 1, kTreeConeSides);
            triangle(top, p1, p0, color);
            triangle(center, p0, p1, color);
        }
    }

    // Prisma sin tapas: dos triángulos por lado.
    void trunk() {
        for (int i = 0; i < kTreeTrunkSides; ++i) {
            const Vec q0 = ring(kTrunkRadius, 0.0f, i, kTreeTrunkSides);
            const Vec q1 = ring(kTrunkRadius, 0.0f, i + 1, kTreeTrunkSides);
            const Vec t0 = ring(kTrunkRadius, kTrunkTop, i, kTreeTrunkSides);
            const Vec t1 = ring(kTrunkRadius, kTrunkTop, i + 1, kTreeTrunkSides);
            triangle(t0, q1, q0, kTrunk);
            triangle(t0, t1, q1, kTrunk);
        }
    }

    int count() const { return next_; }

private:
    // La misma dirección de luz que las cajas de la demo.
    static constexpr Vec kLight{0.35f, 1.0f, 0.55f};
    const float light_length_ =
        std::sqrt(kLight.x * kLight.x + kLight.y * kLight.y + kLight.z * kLight.z);

    std::array<TreeVertex, kTreeVertices>& out_;
    int next_ = 0;
};

} // namespace

void place_trees(std::span<TreePlacement> trees, std::uint64_t seed, float inner_radius,
                 float outer_radius) {
    sim::Pcg32 random(seed, 0x7265'6573u); // stream propio del bosque
    const float inner2 = inner_radius * inner_radius;
    const float outer2 = outer_radius * outer_radius;
    for (TreePlacement& tree : trees) {
        // Radio con densidad uniforme por área: r² uniforme en [inner², outer²).
        const float radius = std::sqrt(inner2 + random.next_float01() * (outer2 - inner2));
        const float angle = 2.0f * std::numbers::pi_v<float> * random.next_float01();
        tree.x = radius * std::cos(angle);
        tree.z = radius * std::sin(angle);
        tree.scale = kTreeMinScale + random.next_float01() * (kTreeMaxScale - kTreeMinScale);
        tree.yaw = 2.0f * std::numbers::pi_v<float> * random.next_float01();
    }
}

std::array<TreeVertex, kTreeVertices> build_tree() {
    std::array<TreeVertex, kTreeVertices> vertices{};
    TreeBuilder builder(vertices);
    builder.trunk();
    builder.cone(kLowerRadius, kLowerBase, kLowerApex, kLowerCrown);
    builder.cone(kUpperRadius, kUpperBase, kUpperApex, kUpperCrown);
    return vertices;
}

} // namespace rally::platform
