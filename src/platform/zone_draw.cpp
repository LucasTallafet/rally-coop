#include "platform/zone_draw.h"

#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>

namespace rally::platform {

namespace {

using sim::AreaId;
using sim::TestZone;
using sim::ZoneArea;
namespace zone = sim::zone;

constexpr double kPi = 3.14159265358979323846;

// Luz fija, la misma que las cajas de la demo.
const Vector3 kLight = Vector3Normalize(Vector3{0.35f, 1.0f, 0.55f});

constexpr Color kLineWhite{236, 236, 228, 255};
constexpr Color kLineYellow{232, 196, 64, 255};
constexpr Color kCone{240, 112, 32, 255};
constexpr Color kPost{236, 232, 220, 255};
constexpr Color kWall{78, 80, 86, 255};
constexpr double kLineLift = 0.03; // las líneas pintadas, por encima del microrrelieve
constexpr float kLineWidth = 0.15f;

Color shaded(Color c, float nx, float ny, float nz) {
    const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
    const float dot = (nx * kLight.x + ny * kLight.y + nz * kLight.z) / length;
    const float shade = 0.5f + 0.5f * std::max(0.0f, dot);
    return Color{static_cast<unsigned char>(c.r * shade), static_cast<unsigned char>(c.g * shade),
                 static_cast<unsigned char>(c.b * shade), c.a};
}

// Triángulos sueltos (sin índices), con el sombreado plano de cada cara en el color de sus
// vértices. Cada triángulo se orienta hacia arriba.
class TriangleList {
public:
    void triangle(Vector3 a, Vector3 b, Vector3 c, Color color) {
        Vector3 n = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
        if (n.y < 0.0f) {
            std::swap(b, c);
            n = Vector3Negate(n);
        }
        const Color face = shaded(color, n.x, n.y, n.z);
        for (const Vector3& v : {a, b, c}) {
            positions_.push_back(v.x);
            positions_.push_back(v.y);
            positions_.push_back(v.z);
            colors_.push_back(face.r);
            colors_.push_back(face.g);
            colors_.push_back(face.b);
            colors_.push_back(face.a);
        }
    }
    void quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color) {
        triangle(a, b, c, color);
        triangle(a, c, d, color);
    }

    // Sube la lista a la GPU en mallas de como mucho 3 × 2^16 vértices.
    void upload(std::vector<Mesh>& out) const {
        constexpr std::size_t kMaxVertices = 3 * 65536;
        const std::size_t total = positions_.size() / 3;
        for (std::size_t first = 0; first < total; first += kMaxVertices) {
            const std::size_t count = std::min(kMaxVertices, total - first);
            Mesh mesh{};
            mesh.vertexCount = static_cast<int>(count);
            mesh.triangleCount = static_cast<int>(count / 3);
            mesh.vertices = static_cast<float*>(MemAlloc(static_cast<unsigned int>(count * 12)));
            mesh.colors =
                static_cast<unsigned char*>(MemAlloc(static_cast<unsigned int>(count * 4)));
            std::memcpy(mesh.vertices, positions_.data() + first * 3, count * 12);
            std::memcpy(mesh.colors, colors_.data() + first * 4, count * 4);
            UploadMesh(&mesh, false);
            out.push_back(mesh);
        }
    }

private:
    std::vector<float> positions_;
    std::vector<unsigned char> colors_;
};

Vector3 v3(double x, double y, double z) {
    return Vector3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
}

struct Rect {
    double x0, x1, z0, z1;
};

// r menos el agujero h, en hasta cuatro rectángulos.
void subtract(const Rect& r, const Rect& h, std::vector<Rect>& out) {
    if (h.x1 <= r.x0 || h.x0 >= r.x1 || h.z1 <= r.z0 || h.z0 >= r.z1) {
        out.push_back(r);
        return;
    }
    if (h.z0 > r.z0) {
        out.push_back({r.x0, r.x1, r.z0, h.z0});
    }
    if (h.z1 < r.z1) {
        out.push_back({r.x0, r.x1, h.z1, r.z1});
    }
    const double z0 = std::max(r.z0, h.z0);
    const double z1 = std::min(r.z1, h.z1);
    if (h.x0 > r.x0) {
        out.push_back({r.x0, h.x0, z0, z1});
    }
    if (h.x1 < r.x1) {
        out.push_back({h.x1, r.x1, z0, z1});
    }
}

// Franjas con relieve (baches, rampa y cresta) con sus taludes: se dibujan con la rejilla
// del suelo de Jolt, ajustada hacia fuera a sus muestras.
Rect snapped_footprint(const ZoneArea& a, double margin) {
    const auto down = [](double v) {
        return zone::kGroundOrigin +
               std::floor((v - zone::kGroundOrigin) / zone::kGroundSpacing) * zone::kGroundSpacing;
    };
    const auto up = [](double v) {
        return zone::kGroundOrigin +
               std::ceil((v - zone::kGroundOrigin) / zone::kGroundSpacing) * zone::kGroundSpacing;
    };
    return {down(a.min_x - margin), up(a.max_x + margin), down(a.min_z), up(a.max_z)};
}

bool is_road(AreaId id) { return id == AreaId::Road || id == AreaId::RoadShoulder; }

bool is_relief(AreaId id) {
    return id == AreaId::BumpsAsphalt || id == AreaId::BumpsGravel || id == AreaId::Ramp ||
           id == AreaId::Crest;
}

// Margen de los taludes de cada franja con relieve.
double relief_margin(AreaId id) {
    switch (id) {
    case AreaId::Ramp:
        return 0.15 * zone::kRampRun + 1.0;
    case AreaId::Crest:
        return zone::kCrestHeight + 1.0;
    default:
        return 1.0;
    }
}

} // namespace

Color surface_color(sim::Surface surface) {
    switch (surface) {
    case sim::Surface::Asphalt:
        return Color{96, 99, 106, 255};
    case sim::Surface::Gravel:
        return Color{198, 182, 142, 255};
    case sim::Surface::Dirt:
        return Color{164, 124, 84, 255};
    case sim::Surface::Snow:
        return Color{240, 243, 247, 255};
    case sim::Surface::Sand:
        return Color{236, 222, 160, 255};
    }
    return Color{255, 0, 255, 255};
}

namespace {

std::array<Color, sim::kSurfaceCount> zone_colors() {
    std::array<Color, sim::kSurfaceCount> colors{};
    for (int s = 0; s < sim::kSurfaceCount; ++s) {
        colors[static_cast<std::size_t>(s)] = surface_color(static_cast<sim::Surface>(s));
    }
    return colors;
}

} // namespace

ZoneDraw::ZoneDraw() : near_(kNearRadius, kNearFadeStart, kNearSpacing, zone_colors()) {}

ZoneDraw::~ZoneDraw() { unload_meshes(); }

void ZoneDraw::unload_meshes() {
    trees_.reset();
    for (std::vector<Mesh>* layer : {&ground_, &decals_, &lines_}) {
        for (Mesh& mesh : *layer) {
            UnloadMesh(mesh);
        }
        layer->clear();
    }
}

void ZoneDraw::build(const sim::TestZone& zone, bool blind) {
    unload_meshes();
    zone_ = &zone;
    blind_ = blind;
    near_.invalidate(); // la superficie de la pista ciega puede haber cambiado
    const auto color_of = [blind](sim::Surface surface) {
        return blind ? kBlindColor : surface_color(surface);
    };
    // Altura de lo que se pinta encima del suelo: la geometría y, fuera del modo a ciegas, h.
    const auto painted_y = [&zone, blind](double x, double z) {
        return zone.ground_height(x, z) + (blind ? 0.0 : zone.micro_height(x, z)) + kLineLift;
    };

    // Suelo: el cuadrado de Jolt menos las franjas con relieve, y esas franjas con su rejilla.
    TriangleList ground;
    // Fondo llano hasta el borde del suelo.
    const double lo = zone::kGroundOrigin;
    const double hi = -zone::kGroundOrigin;
    std::vector<Rect> background{{lo, hi, lo, hi}};
    for (const ZoneArea& a : zone.areas()) {
        if (!is_relief(a.id) || is_road(a.id)) {
            continue;
        }
        const Rect hole = snapped_footprint(a, relief_margin(a.id));
        std::vector<Rect> next;
        for (const Rect& r : background) {
            subtract(r, hole, next);
        }
        background = std::move(next);

        const double s = zone::kGroundSpacing;
        for (double z = hole.z0; z < hole.z1 - 0.5 * s; z += s) {
            for (double x = hole.x0; x < hole.x1 - 0.5 * s; x += s) {
                const Vector3 p00 = v3(x, zone.ground_height(x, z), z);
                const Vector3 p10 = v3(x + s, zone.ground_height(x + s, z), z);
                const Vector3 p11 = v3(x + s, zone.ground_height(x + s, z + s), z + s);
                const Vector3 p01 = v3(x, zone.ground_height(x, z + s), z + s);
                // Superficie de la celda por su centro, como el material de Jolt.
                const Color c = color_of(zone.surface_at(x + 0.5 * s, z + 0.5 * s));
                ground.triangle(p00, p01, p11, c);
                ground.triangle(p00, p11, p10, c);
            }
        }
    }
    {
        // Carretera: su caja, por filas de la rejilla de Jolt. Una celda con alguna esquina a
        // otra altura que 0 o con otra superficie que la tierra va con su relieve; las seguidas
        // que no, en una tira llana.
        const sim::TestRoad& road = zone.road();
        const double s = zone::kGroundSpacing;
        const auto down = [s](double v) {
            return zone::kGroundOrigin + std::floor((v - zone::kGroundOrigin) / s) * s;
        };
        const auto up = [s](double v) {
            return zone::kGroundOrigin + std::ceil((v - zone::kGroundOrigin) / s) * s;
        };
        const Rect box{down(road.min_x()), up(road.max_x()), down(road.min_z()), up(road.max_z())};
        std::vector<Rect> next;
        for (const Rect& r : background) {
            subtract(r, box, next);
        }
        background = std::move(next);
        const int nx = static_cast<int>(std::lround((box.x1 - box.x0) / s));
        const int nz = static_cast<int>(std::lround((box.z1 - box.z0) / s));
        std::vector<float> y(static_cast<std::size_t>(nx + 1) * (nz + 1));
        for (int j = 0; j <= nz; ++j) {
            for (int i = 0; i <= nx; ++i) {
                y[static_cast<std::size_t>(j) * (nx + 1) + i] =
                    static_cast<float>(zone.ground_height(box.x0 + i * s, box.z0 + j * s));
            }
        }
        const Color dirt = color_of(sim::Surface::Dirt);
        for (int j = 0; j < nz; ++j) {
            const double z = box.z0 + j * s;
            int flat_from = -1;
            for (int i = 0; i <= nx; ++i) {
                bool relief = false;
                sim::Surface surface = sim::Surface::Dirt;
                if (i < nx) {
                    const std::size_t a = static_cast<std::size_t>(j) * (nx + 1) + i;
                    const std::size_t c = a + static_cast<std::size_t>(nx + 1);
                    relief = y[a] != 0.0f || y[a + 1] != 0.0f || y[c] != 0.0f || y[c + 1] != 0.0f;
                    if (!relief) {
                        surface = zone.surface_at(box.x0 + (i + 0.5) * s, z + 0.5 * s);
                        relief = surface != sim::Surface::Dirt;
                    }
                }
                if ((relief || i == nx) && flat_from >= 0) {
                    const double x0 = box.x0 + flat_from * s;
                    const double x1 = box.x0 + i * s;
                    ground.quad(v3(x0, 0.0, z), v3(x0, 0.0, z + s), v3(x1, 0.0, z + s),
                                v3(x1, 0.0, z), dirt);
                    flat_from = -1;
                }
                if (i == nx) {
                    break;
                }
                if (!relief) {
                    if (flat_from < 0) {
                        flat_from = i;
                    }
                    continue;
                }
                const double x = box.x0 + i * s;
                const std::size_t a = static_cast<std::size_t>(j) * (nx + 1) + i;
                const std::size_t c = a + static_cast<std::size_t>(nx + 1);
                const Vector3 p00 = v3(x, y[a], z);
                const Vector3 p10 = v3(x + s, y[a + 1], z);
                const Vector3 p11 = v3(x + s, y[c + 1], z + s);
                const Vector3 p01 = v3(x, y[c], z + s);
                const Color color = color_of(zone.surface_at(x + 0.5 * s, z + 0.5 * s));
                ground.triangle(p00, p01, p11, color);
                ground.triangle(p00, p11, p10, color);
            }
        }
    }
    for (const Rect& r : background) {
        ground.quad(v3(r.x0, 0.0, r.z0), v3(r.x0, 0.0, r.z1), v3(r.x1, 0.0, r.z1),
                    v3(r.x1, 0.0, r.z0), color_of(sim::Surface::Dirt));
    }
    // Muros perimetrales: cajas grises con colores planos (las cinco caras visibles).
    for (const sim::WallBox& wall : zone.walls()) {
        const double x0 = wall.cx - wall.half_x;
        const double x1 = wall.cx + wall.half_x;
        const double z0 = wall.cz - wall.half_z;
        const double z1 = wall.cz + wall.half_z;
        const double y0 = wall.cy - wall.half_y;
        const double y1 = wall.cy + wall.half_y;
        ground.quad(v3(x0, y1, z0), v3(x0, y1, z1), v3(x1, y1, z1), v3(x1, y1, z0), kWall);
        // Caras verticales con la normal hacia fuera de la caja (para el sombreado).
        ground.quad(v3(x0, y0, z0), v3(x0, y1, z0), v3(x1, y1, z0), v3(x1, y0, z0), kWall);
        ground.quad(v3(x1, y0, z1), v3(x1, y1, z1), v3(x0, y1, z1), v3(x0, y0, z1), kWall);
        ground.quad(v3(x0, y0, z1), v3(x0, y1, z1), v3(x0, y1, z0), v3(x0, y0, z0), kWall);
        ground.quad(v3(x1, y0, z0), v3(x1, y1, z0), v3(x1, y1, z1), v3(x1, y0, z1), kWall);
    }
    ground.upload(ground_);

    // Áreas llanas: rectángulos y calzadas, en el plano del suelo.
    TriangleList decals;
    for (const ZoneArea& a : zone.areas()) {
        if (is_relief(a.id) || is_road(a.id)) {
            continue;
        }
        const Color c = color_of(a.surface);
        if (!a.road) {
            decals.quad(v3(a.min_x, 0.0, a.min_z), v3(a.min_x, 0.0, a.max_z),
                        v3(a.max_x, 0.0, a.max_z), v3(a.max_x, 0.0, a.min_z), c);
            continue;
        }
        const double length = TestZone::axis_length(a);
        const int steps = static_cast<int>(std::ceil(length / 0.5));
        for (int i = 0; i < steps; ++i) {
            const sim::AxisPoint p = TestZone::axis_point(a, length * i / steps);
            const sim::AxisPoint q = TestZone::axis_point(a, length * (i + 1) / steps);
            const double w = a.half_width;
            decals.quad(v3(p.x - w * std::cos(p.psi), 0.0, p.z - w * std::sin(p.psi)),
                        v3(q.x - w * std::cos(q.psi), 0.0, q.z - w * std::sin(q.psi)),
                        v3(q.x + w * std::cos(q.psi), 0.0, q.z + w * std::sin(q.psi)),
                        v3(p.x + w * std::cos(p.psi), 0.0, p.z + w * std::sin(p.psi)), c);
        }
    }
    decals.upload(decals_);

    // Líneas pintadas: tiras de `width` a lo largo de una polilínea.
    TriangleList lines;
    const auto polyline = [&](const std::vector<std::array<double, 2>>& points, bool closed,
                              float width, Color color) {
        const std::size_t count = points.size();
        const std::size_t segments = closed ? count : count - 1;
        for (std::size_t i = 0; i < segments; ++i) {
            const auto& p = points[i];
            const auto& q = points[(i + 1) % count];
            const double dx = q[0] - p[0];
            const double dz = q[1] - p[1];
            const double length = std::sqrt(dx * dx + dz * dz);
            const double nx = -dz / length * 0.5 * width;
            const double nz = dx / length * 0.5 * width;
            lines.quad(v3(p[0] - nx, painted_y(p[0] - nx, p[1] - nz), p[1] - nz),
                       v3(q[0] - nx, painted_y(q[0] - nx, q[1] - nz), q[1] - nz),
                       v3(q[0] + nx, painted_y(q[0] + nx, q[1] + nz), q[1] + nz),
                       v3(p[0] + nx, painted_y(p[0] + nx, p[1] + nz), p[1] + nz), color);
        }
    };
    const auto circle = [&](double cx, double cz, double radius) {
        std::vector<std::array<double, 2>> points;
        const int steps = static_cast<int>(std::ceil(2.0 * kPi * radius / 0.5));
        for (int i = 0; i < steps; ++i) {
            const double t = 2.0 * kPi * i / steps;
            points.push_back({cx + radius * std::cos(t), cz + radius * std::sin(t)});
        }
        polyline(points, true, 0.2f, kLineWhite);
    };
    const ZoneArea& circle_area = zone.area(AreaId::Circle);
    circle(circle_area.cx, circle_area.cz, zone::kCircleRadius);
    for (const AreaId patch : {AreaId::PatchDirt, AreaId::PatchSnow, AreaId::PatchSand}) {
        const ZoneArea& a = zone.area(patch);
        circle(a.cx, a.cz, zone::kPatchCircleRadius);
    }
    // Bordes de las calzadas (en el modo a ciegas son lo único que marca la pista).
    for (const AreaId road : {AreaId::Hairpin, AreaId::BlindTrack}) {
        const ZoneArea& a = zone.area(road);
        const double length = TestZone::axis_length(a);
        const int steps = static_cast<int>(std::ceil(length / 0.5));
        for (const double side : {-1.0, 1.0}) {
            std::vector<std::array<double, 2>> points;
            const double offset = side * (a.half_width - 0.25);
            for (int i = 0; i < steps + (a.closed ? 0 : 1); ++i) {
                const sim::AxisPoint p = TestZone::axis_point(a, length * i / steps);
                points.push_back({p.x + offset * std::cos(p.psi), p.z + offset * std::sin(p.psi)});
            }
            polyline(points, a.closed, kLineWidth, kLineWhite);
        }
    }
    // Carretera: líneas de salida y meta y, en el modo a ciegas (donde los colores no la
    // marcan), los bordes de la calzada, como la pista ciega.
    for (const sim::FinishLine* line : {&zone.road_start_line(), &zone.road_finish_line()}) {
        const double half = zone::kRoadHalfWidth;
        polyline({{line->x - half * -line->forward_z, line->z - half * line->forward_x},
                  {line->x + half * -line->forward_z, line->z + half * line->forward_x}},
                 false, 0.6f, kLineWhite);
    }
    if (blind) {
        const sim::TestRoad& road = zone.road();
        for (const double side : {-1.0, 1.0}) {
            std::vector<std::array<double, 2>> points;
            const double offset = side * (zone::kRoadHalfWidth - 0.25);
            for (double s = road.first_s(); s <= road.last_s(); s += 0.5) {
                const sim::RoadPoint p = road.point(s);
                points.push_back({p.x + offset * std::cos(p.psi), p.z + offset * std::sin(p.psi)});
            }
            polyline(points, false, kLineWidth, kLineWhite);
        }
    }
    // Línea de meta de la pista ciega.
    const sim::FinishLine& finish = zone.finish_line();
    const double half_road = 0.5 * zone::kBlindRoadWidth;
    polyline({{finish.x - half_road * -finish.forward_z, finish.z - half_road * finish.forward_x},
              {finish.x + half_road * -finish.forward_z, finish.z + half_road * finish.forward_x}},
             false, 0.6f, kLineWhite);
    // Recta: una línea cada 100 m y, en amarillo, el principio de los baches y de la grava.
    const ZoneArea& straight = zone.area(AreaId::Straight);
    for (int i = 0; i <= 10; ++i) {
        const double z = straight.max_z - 100.0 * i;
        polyline({{straight.min_x, z}, {straight.max_x, z}}, false, 0.3f,
                 i == 10 ? kLineYellow : kLineWhite);
    }
    const ZoneArea& gravel_bumps = zone.area(AreaId::BumpsGravel);
    polyline({{gravel_bumps.min_x, gravel_bumps.max_z}, {gravel_bumps.max_x, gravel_bumps.max_z}},
             false, 0.3f, kLineYellow);
    lines.upload(lines_);

    // Conos del eslalon y postes cada 100 m a los lados de la recta.
    // Árboles de la carretera: el del bosque de medida a escala 1 (tronco de 0,25 m, como el
    // cilindro de Jolt), hundido 5 cm y con un giro distinto cada uno (solo se ve).
    {
        const sim::TestRoad& road = zone.road();
        trees_center_ = {0.5 * (road.min_x() + road.max_x()), 0.0,
                         0.5 * (road.min_z() + road.max_z())};
        std::vector<Matrix> transforms;
        transforms.reserve(road.trees().size());
        for (std::size_t i = 0; i < road.trees().size(); ++i) {
            const sim::TreeSpot& t = road.trees()[i];
            const auto yaw = static_cast<float>(2.399963 * static_cast<double>(i));
            transforms.push_back(MatrixMultiply(
                MatrixRotateY(yaw), MatrixTranslate(static_cast<float>(t.x - trees_center_.x),
                                                    static_cast<float>(t.y - 0.05),
                                                    static_cast<float>(t.z - trees_center_.z))));
        }
        trees_ = std::make_unique<TreeInstances>(transforms);
    }
    cones_ = zone.slalom_cones();
    posts_.clear();
    for (int i = 0; i <= 10; ++i) {
        const double z = straight.max_z - 100.0 * i;
        posts_.push_back({straight.min_x - 2.0, 0.0, z});
        posts_.push_back({straight.max_x + 2.0, 0.0, z});
    }
}

void ZoneDraw::draw_near(const Vec3d& eye, const Vec3d& origin) {
    const sim::TestZone& zone = *zone_;
    const auto sample = [&zone](double x, double z) {
        const sim::TestZone::MicroSample micro = zone.micro_sample(x, z);
        NearTexel texel;
        texel.height = static_cast<float>(zone.ground_height(x, z));
        texel.micro = static_cast<float>(micro.micro);
        texel.surface = static_cast<float>(micro.surface);
        return texel;
    };
    const NearStats stats = near_.draw(eye, origin, sample);
    breakdown_.near_ms = stats.near_ms;
    breakdown_.upload_ms = stats.upload_ms;
    breakdown_.cells = stats.cells;
    breakdown_.uploads = stats.uploads;
}

void ZoneDraw::draw(const Vec3d& eye, const Vec3d& origin) {
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    if (zone_ == nullptr) {
        return;
    }
    breakdown_ = Breakdown{};
    rlDrawRenderBatchActive();
    // Sin la rejilla cercana (modo a ciegas), lo lejano llega hasta el ojo.
    const float far_radius = blind_ ? 0.0f : static_cast<float>(kNearRadius);
    for (const Mesh& mesh : ground_) {
        clip_.draw(mesh, origin, far_radius, 1.0f, eye);
    }
    if (!blind_) {
        draw_near(eye, origin);
    }
    offset_.set(-1.0f, -1.0f);
    for (const Mesh& mesh : decals_) {
        clip_.draw(mesh, origin, far_radius, 1.0f, eye);
    }
    offset_.set(-2.0f, -2.0f);
    for (const Mesh& mesh : lines_) {
        clip_.draw(mesh, origin, 0.0f, 1.0f, eye);
    }
    offset_.off();
    // Conos y postes por las dos caras: no dependen del orden de sus vértices.
    rlDisableBackfaceCulling();
    draw_props(origin);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    if (trees_) {
        trees_->draw(relative_to_camera(trees_center_, origin));
    }
    stats_.record(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
}

void ZoneDraw::draw_props(const Vec3d& origin) const {
    const auto ground_y = [this](double x, double z) {
        return zone_->ground_height(x, z) + (blind_ ? 0.0 : zone_->micro_height(x, z));
    };
    rlBegin(RL_TRIANGLES);
    // Conos: pirámides de ocho lados de 0,7 m.
    constexpr int kSides = 8;
    for (const sim::AxisPoint& c : cones_) {
        const Vec3f base = relative_to_camera({c.x, ground_y(c.x, c.z), c.z}, origin);
        const Vector3 tip{base.x, base.y + 0.7f, base.z};
        for (int i = 0; i < kSides; ++i) {
            const double a0 = 2.0 * kPi * i / kSides;
            const double a1 = 2.0 * kPi * (i + 1) / kSides;
            const Vector3 p0{base.x + 0.2f * static_cast<float>(std::cos(a0)), base.y,
                             base.z + 0.2f * static_cast<float>(std::sin(a0))};
            const Vector3 p1{base.x + 0.2f * static_cast<float>(std::cos(a1)), base.y,
                             base.z + 0.2f * static_cast<float>(std::sin(a1))};
            const Vector3 n =
                Vector3CrossProduct(Vector3Subtract(p1, tip), Vector3Subtract(p0, tip));
            const Color face = shaded(kCone, n.x, n.y, n.z);
            rlColor4ub(face.r, face.g, face.b, face.a);
            rlVertex3f(tip.x, tip.y, tip.z);
            rlVertex3f(p1.x, p1.y, p1.z);
            rlVertex3f(p0.x, p0.y, p0.z);
        }
    }
    // Postes de 1,5 m.
    for (const Vec3d& p : posts_) {
        const Vec3f b = relative_to_camera({p.x, 0.0, p.z}, origin);
        constexpr float h = 0.08f;
        const Vector3 corners[4] = {{b.x - h, 0, b.z - h},
                                    {b.x + h, 0, b.z - h},
                                    {b.x + h, 0, b.z + h},
                                    {b.x - h, 0, b.z + h}};
        for (int i = 0; i < 4; ++i) {
            const Vector3 a = corners[i];
            const Vector3 c = corners[(i + 1) % 4];
            const Vector3 n = Vector3CrossProduct(Vector3Subtract(c, a), Vector3{0, 1, 0});
            const Color face = shaded(kPost, n.x, n.y, n.z);
            rlColor4ub(face.r, face.g, face.b, face.a);
            const float y0 = b.y;
            const float y1 = b.y + 1.5f;
            rlVertex3f(a.x, y0, a.z);
            rlVertex3f(c.x, y1, c.z);
            rlVertex3f(c.x, y0, c.z);
            rlVertex3f(a.x, y0, a.z);
            rlVertex3f(a.x, y1, a.z);
            rlVertex3f(c.x, y1, c.z);
        }
    }
    rlEnd();
}

} // namespace rally::platform
