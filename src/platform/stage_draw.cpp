#include "platform/stage_draw.h"

#include "platform/stage_checked.h"
#include "platform/zone_draw.h"

#include <rlgl.h>

#include <chrono>
#include <cstring>
#include <utility>

namespace rally::platform {

namespace {

static_assert(StageDraw::kNearRadius == ZoneDraw::kNearRadius);
static_assert(StageDraw::kNearFadeStart == ZoneDraw::kNearFadeStart);
static_assert(StageDraw::kNearSpacing == ZoneDraw::kNearSpacing);

std::array<Color, sim::kSurfaceCount> palette_colors() {
    std::array<Color, sim::kSurfaceCount> colors{};
    for (int s = 0; s < sim::kSurfaceCount; ++s) {
        colors[static_cast<std::size_t>(s)] = surface_color(static_cast<sim::Surface>(s));
    }
    return colors;
}

StagePalette palette() {
    StagePalette p{};
    for (int s = 0; s < sim::kSurfaceCount; ++s) {
        const Color c = surface_color(static_cast<sim::Surface>(s));
        p[static_cast<std::size_t>(s)] = {c.r, c.g, c.b};
    }
    return p;
}

} // namespace

StageDraw::StageDraw() : near_(kNearRadius, kNearFadeStart, kNearSpacing, palette_colors()) {}

StageDraw::~StageDraw() { unload(); }

void StageDraw::unload() {
    for (std::vector<Mesh>* meshes : {&core_meshes_, &tile_meshes_}) {
        for (Mesh& mesh : *meshes) {
            if (mesh.vertexCount > 0) {
                UnloadMesh(mesh);
            }
        }
        meshes->clear();
    }
    if (lines_.vertexCount > 0) {
        UnloadMesh(lines_);
    }
    lines_ = Mesh{};
    surface_.reset();
    sampler_.reset();
    built_ = false;
    triangle_total_ = 0;
    gpu_bytes_ = 0;
}

Mesh StageDraw::upload(const StageMeshBuffers& buffers) {
    Mesh mesh{};
    const std::size_t count = buffers.vertex_count();
    if (count == 0 || buffers.triangle_count() == 0) {
        return mesh;
    }
    mesh.vertexCount = static_cast<int>(count);
    mesh.triangleCount = static_cast<int>(buffers.triangle_count());
    mesh.vertices = static_cast<float*>(MemAlloc(static_cast<unsigned int>(count * 12)));
    mesh.colors = static_cast<unsigned char*>(MemAlloc(static_cast<unsigned int>(count * 4)));
    std::memcpy(mesh.vertices, buffers.positions.data(), count * 12);
    std::memcpy(mesh.colors, buffers.colors.data(), count * 4);
    gpu_bytes_ += count * 16;
    if (buffers.indexed) {
        mesh.indices = static_cast<unsigned short*>(
            MemAlloc(static_cast<unsigned int>(buffers.indices.size() * 2)));
        std::memcpy(mesh.indices, buffers.indices.data(), buffers.indices.size() * 2);
        gpu_bytes_ += buffers.indices.size() * 2;
    }
    UploadMesh(&mesh, false);
    triangle_total_ += mesh.triangleCount;
    return mesh;
}

bool StageDraw::build(const sim::StageTrack& track, std::uint64_t expected_ground_hash,
                      std::string& error) {
    unload();
    track_ = &track;
    if (!build_checked_ground(track, expected_ground_hash, ground_, error)) {
        return false;
    }
    surface_ = std::make_unique<StageSurface>(ground_);
    sampler_ = std::make_unique<StageNearSampler>(track, *surface_, noise_);
    chunks_ = StageChunks::build(ground_);
    const StagePalette colors = palette();
    StageMeshBuffers buffers;
    for (const StageChunk& chunk : chunks_.core()) {
        build_core_mesh(ground_, chunks_.core_triangles().data() + chunk.first, chunk.count, colors,
                        buffers);
        core_meshes_.push_back(upload(buffers));
    }
    for (std::size_t i = 0; i < chunks_.tiles().size(); ++i) {
        build_tile_mesh(*surface_, i, colors, buffers);
        tile_meshes_.push_back(upload(buffers));
    }
    build_stage_lines(track, *surface_, noise_, buffers);
    lines_ = upload(buffers);
    chunks_.reserve_selection(core_selected_, tiles_selected_);
    near_.invalidate();
    built_ = true;
    return true;
}

bool StageDraw::ground_height(double x, double z, double& y) const {
    if (!built_) {
        return false;
    }
    const StageHit hit = surface_->query(x, z);
    y = hit.y;
    return hit.valid;
}

void StageDraw::draw(const Vec3d& eye, const Vec3d& origin, double distance) {
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    if (!built_) {
        return;
    }
    rlDrawRenderBatchActive();
    chunks_.select_tiles(eye.x, eye.z, distance, tiles_selected_);
    chunks_.select_core(eye.x, eye.z, distance, core_selected_);
    const Vec3d ground_origin{ground_.origin_x(), ground_.origin_y(), ground_.origin_z()};
    // Las mallas van en float relativo al origen del suelo: el desplazamiento al origen del
    // render se resta en doble.
    const Vec3d shifted_origin{origin.x - ground_origin.x, origin.y - ground_origin.y,
                               origin.z - ground_origin.z};
    const Vec3d shifted_eye{eye.x - ground_origin.x, eye.y - ground_origin.y,
                            eye.z - ground_origin.z};
    triangles_drawn_ = 0;
    chunks_drawn_ = 0;
    constexpr auto far_radius = static_cast<float>(kNearRadius);
    for (const int i : tiles_selected_) {
        const Mesh& mesh = tile_meshes_[static_cast<std::size_t>(i)];
        if (mesh.vertexCount > 0) {
            clip_.draw(mesh, shifted_origin, far_radius, 1.0f, shifted_eye);
            triangles_drawn_ += mesh.triangleCount;
            ++chunks_drawn_;
        }
    }
    // El núcleo gana al terreno, que queda 5 mm bajo su borde.
    offset_.set(-1.0f, -1.0f);
    for (const int i : core_selected_) {
        const Mesh& mesh = core_meshes_[static_cast<std::size_t>(i)];
        if (mesh.vertexCount > 0) {
            clip_.draw(mesh, shifted_origin, far_radius, 1.0f, shifted_eye);
            triangles_drawn_ += mesh.triangleCount;
            ++chunks_drawn_;
        }
    }
    offset_.off();
    const StageNearSampler& sampler = *sampler_;
    const auto sample = [&sampler](double x, double z) { return sampler.at(x, z); };
    near_stats_ = near_.draw(eye, origin, sample);
    triangles_drawn_ += near_.triangle_count();
    if (lines_.vertexCount > 0) {
        offset_.set(-2.0f, -2.0f);
        clip_.draw(lines_, shifted_origin, 0.0f, 1.0f, shifted_eye);
        offset_.off();
        triangles_drawn_ += lines_.triangleCount;
    }
    rlDrawRenderBatchActive();
    stats_.record(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
}

} // namespace rally::platform
