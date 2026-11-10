#include "sim/sim_world.h"

#include "sim/check.h"
#include "sim/fnv1a.h"
#include "sim/jolt_config.h"
#include "sim/layers.h"
#include "sim/rmath.h"
#include "sim/stage_ground.h"
#include "sim/surface_material.h"

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/UnorderedSet.h> // Shape::VisitedShapes: solo para contar la memoria
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/StateRecorderImpl.h>

#include <cstddef>

namespace rally::sim {

namespace {

// Capacidades del PhysicsSystem. Jolt reserva todo en Init; el tramo (fase 3) las revisará.
constexpr JPH::uint kMaxBodies = 1024;
constexpr JPH::uint kMaxBodyPairs = 1024;
constexpr JPH::uint kMaxContactConstraints = 1024;
constexpr std::size_t kTempAllocatorBytes = 4 * 1024 * 1024;

enum class State : std::uint8_t { Empty, Built, Sealed };

// Bloques de 4 × 4 muestras con 8 bits por muestra: en la rampa (la zona más inclinada) el
// error de altura es de ~1,5 mm.
constexpr JPH::uint kGroundBlockSize = 4;
constexpr JPH::uint kGroundBitsPerSample = 8;

// Baldosas del tramo: bloques de 4 × 4 y los bits por muestra que dejan el error de altura de
// Jolt por debajo de esto (m): el terreno va 5 mm bajo el núcleo y no puede asomar.
constexpr float kStageTileMaxError = 0.001f;

// Aborta con el error de Jolt si no pudo crear una forma del tramo.
void require_shape(const JPH::ShapeSettings::ShapeResult& result, const char* what) {
    if (!result.IsValid()) {
        const std::string message = std::string("SimWorld::build: Jolt no pudo crear ") + what +
                                    ": " + result.GetError().c_str();
        check_failed(message.c_str(), __FILE__, __LINE__);
    }
}

// Forma del suelo del tramo: un compuesto estático con la malla del núcleo (si la hay) y, en un
// compuesto anidado, las baldosas, en ese orden. Anidadas, los bits del SubShapeID son 1 + el
// máximo de la malla y de las baldosas, no su suma: así cabe un tramo de 30 km. Cada forma lleva
// sus SurfaceMaterial con el microrrelieve del tramo (MicroRelief::Stage: las bandas de las losas,
// mezcladas en los bordes).
JPH::Ref<JPH::Shape> stage_shape(const StageGround& ground, bool with_mesh, MicroRelief relief) {
    JPH::PhysicsMaterialList materials;
    for (int s = 0; s < kSurfaceCount; ++s) {
        materials.push_back(new SurfaceMaterial(static_cast<Surface>(s), relief));
    }
    JPH::StaticCompoundShapeSettings compound;
    JPH::StaticCompoundShapeSettings tiles;
    if (with_mesh && !ground.triangles().empty()) {
        JPH::VertexList vertices;
        vertices.reserve(ground.vertices().size());
        for (const StageVertex& v : ground.vertices()) {
            vertices.push_back(JPH::Float3(v.x, v.y, v.z));
        }
        JPH::IndexedTriangleList triangles;
        triangles.reserve(ground.triangles().size());
        for (const StageTriangle& t : ground.triangles()) {
            triangles.push_back(
                JPH::IndexedTriangle(t.a, t.b, t.c, static_cast<JPH::uint32>(t.surface)));
        }
        const JPH::MeshShapeSettings mesh(std::move(vertices), std::move(triangles), materials);
        const JPH::ShapeSettings::ShapeResult result = mesh.Create();
        require_shape(result, "la malla del tramo");
        compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), result.Get());
    }
    JPH::PhysicsMaterialList dirt;
    dirt.push_back(materials[static_cast<std::size_t>(Surface::Dirt)]);
    const auto cell = static_cast<float>(ground.cell());
    for (const StageTile& tile : ground.tiles()) {
        const bool by_cell = !tile.surfaces.empty();
        JPH::HeightFieldShapeSettings settings(tile.heights.data(), JPH::Vec3::sZero(),
                                               JPH::Vec3(cell, 1.0f, cell), kStageTileSamples,
                                               by_cell ? tile.surfaces.data() : nullptr,
                                               by_cell ? materials : JPH::PhysicsMaterialList());
        if (!by_cell) {
            // Sin índices, Jolt usa el único material de la lista (el constructor la ignora).
            settings.mMaterials = dirt;
        }
        settings.mBlockSize = kGroundBlockSize;
        settings.mBitsPerSample = settings.CalculateBitsPerSampleForError(kStageTileMaxError);
        const JPH::ShapeSettings::ShapeResult result = settings.Create();
        require_shape(result, "una baldosa del tramo");
        // Múltiplos de 63 celdas: exactos en float.
        const JPH::Vec3 corner(static_cast<float>(tile.tx * kStageTileCells) * cell, 0.0f,
                               static_cast<float>(tile.tz * kStageTileCells) * cell);
        // Un compuesto necesita al menos dos formas: una baldosa sola va al de fuera.
        (ground.tiles().size() >= 2 ? tiles : compound)
            .AddShape(corner, JPH::Quat::sIdentity(), result.Get());
    }
    if (ground.tiles().size() >= 2) {
        const JPH::ShapeSettings::ShapeResult result = tiles.Create();
        require_shape(result, "las baldosas del tramo");
        if (compound.mSubShapes.empty()) {
            return result.Get();
        }
        compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), result.Get());
    }
    const JPH::ShapeSettings::ShapeResult result = compound.Create();
    require_shape(result, "el suelo del tramo");
    return result.Get();
}

// Giro del coche con el rumbo `yaw` (alrededor de +y, como CarSpawn) apoyado en el plano de la
// calzada de la pose `p`: su adelante es el del rumbo llevado al plano y su arriba, la normal.
JPH::Quat on_road_plane(const StagePose& p, float yaw) {
    const auto sh = static_cast<float>(rmath::sin(p.heading));
    const auto ch = static_cast<float>(rmath::cos(p.heading));
    const JPH::Vec3 along(sh, static_cast<float>(p.grade), -ch);
    const JPH::Vec3 across(ch, static_cast<float>(p.bank), sh);
    const JPH::Vec3 up = across.Cross(along).Normalized();
    // Adelante del rumbo (−z girado por yaw), sin la componente normal al plano.
    const JPH::Vec3 flat = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), yaw) * JPH::Vec3(0, 0, -1);
    const JPH::Vec3 forward = (flat - up * flat.Dot(up)).Normalized();
    const JPH::Vec3 right = forward.Cross(up);
    const JPH::Mat44 m(JPH::Vec4(right, 0.0f), JPH::Vec4(up, 0.0f), JPH::Vec4(-forward, 0.0f),
                       JPH::Vec4(0.0f, 0.0f, 0.0f, 1.0f));
    return m.GetQuaternion().Normalized();
}

double stage_cell(StageGroundVariant variant) {
    if (variant == StageGroundVariant::CoarseTiles) {
        return 2.0 * kStageCell;
    }
    return variant == StageGroundVariant::DenseMesh || variant == StageGroundVariant::BandDense
               ? 0.5 * kStageCell
               : kStageCell;
}

} // namespace

CarSpawn stage_spawn(const StageTrack& track, double s, float speed) {
    const StagePose p = track.pose(s);
    CarSpawn spawn;
    spawn.x = p.x;
    spawn.y = p.y;
    spawn.z = p.z;
    spawn.yaw = static_cast<float>(-p.heading);
    spawn.speed = speed;
    return spawn;
}

struct SimWorld::Impl {
    Impl() : temp_allocator(kTempAllocatorBytes), job_system(JPH::cMaxPhysicsJobs) {
        system.Init(kMaxBodies, 0, kMaxBodyPairs, kMaxContactConstraints, broad_phase_layers,
                    object_vs_broad_phase, object_pairs);
    }

    // Las capas tienen que vivir más que el PhysicsSystem: van antes.
    const layers::BroadPhaseLayers broad_phase_layers;
    const layers::ObjectVsBroadPhase object_vs_broad_phase;
    const layers::ObjectPairs object_pairs;
    JPH::TempAllocatorImpl temp_allocator;
    JPH::JobSystemSingleThreaded job_system;
    JPH::PhysicsSystem system;

    std::vector<JPH::BodyID> bodies; // en el orden de la descripción

    // Coche. La restricción la poseen el PhysicsSystem y esta referencia.
    JPH::BodyID car; // inválido si no hay coche
    JPH::Ref<JPH::VehicleConstraint> car_constraint;
    CarController* car_controller = nullptr; // lo posee la restricción
    JPH::Ref<CarWheelTester> car_tester;
    float car_mass = 0.0f;
    CarSpawn car_spawn;
    // Bandas de ruido de h para las losas de los tests (MicroRelief::Noise).
    std::unique_ptr<TestZone> noise;

    // Mandos aplicados y botones del paso anterior. Es estado de la simulación que no está
    // en el SaveState de Jolt: en el cambio de host (fase 5) viaja con el estado del juego.
    ControlState controls;
    DriverAids aids;
    // Dirección del último paso: salidas de diagnóstico, no estado (se recalculan en cada paso).
    SteerContext steer_context;
    std::int32_t steer_aided = 0;
    float last_impact = 0.0f; // salida de diagnóstico; no es estado de la simulación
    // Telemetría (solo lectura; no es estado de la simulación).
    bool telemetry_enabled = false;
    std::int64_t steps = 0; // pasos desde build, para la telemetría
    CarTelemetry telemetry;
    State state = State::Empty;

    // Zona de pruebas (si la descripción la pide).
    std::unique_ptr<TestZone> zone;
    std::unique_ptr<LapTimer> lap;
    std::unique_ptr<RoadTimer> road;
    std::vector<JPH::BodyID> trees; // en el orden de TestZone::road().trees()
    JPH::BodyID ground;
    std::uint64_t ground_hash = 0;
    std::uint64_t zone_hash = 0;
    std::size_t ground_bytes = 0;

    // Tramo (si la descripción lo tiene). StageRun guarda la dirección del StageTrack: los dos
    // van en unique_ptr y no se mueven en la vida del mundo.
    std::unique_ptr<StageTrack> stage;
    std::unique_ptr<StageRun> stage_run;
    JPH::BodyID stage_body;
    std::uint64_t stage_ground_hash = 0;
    std::size_t stage_ground_bytes = 0;

    // Rellena `telemetry` al final de un paso. Solo lee.
    void capture_telemetry(const SimInput& input, JPH::Vec3Arg before_velocity, bool reset);
    // Recolocar: mueve el coche, pone sus velocidades y el controlador a cero. En el tramo, a
    // stage_spawn(reset_s) con placed(Button).
    void reset_car();
    // Lleva el coche quieto a stage_spawn(s), apoyado, con el controlador a cero.
    void place_on_stage(double s);
    // Giro de salida del coche: el de CarSpawn y, en el tramo con la salida en carretera,
    // apoyado en la pendiente y el peralte.
    JPH::Quat spawn_rotation(const CarSpawn& spawn) const;
};

JPH::Quat SimWorld::Impl::spawn_rotation(const CarSpawn& spawn) const {
    if (stage) {
        const StageProjection p = stage->acquire(spawn.x, spawn.z);
        if (p.on_road) {
            return on_road_plane(stage->pose(p.s), spawn.yaw);
        }
    }
    return JPH::Quat::sRotation(JPH::Vec3::sAxisY(), spawn.yaw);
}

SimWorld::SimWorld() : impl_(std::make_unique<Impl>()) {}

SimWorld::~SimWorld() = default;

void SimWorld::build(const WorldDesc& desc) {
    Impl& w = *impl_;
    RALLY_CHECK(w.state == State::Empty,
                "SimWorld::build: el mundo ya está construido (tras seal() no se añaden cuerpos)");
    // La zona se construye antes de contar: sus árboles son cuerpos.
    if (desc.test_zone) {
        w.zone = std::make_unique<TestZone>(desc.blind_surface, desc.road_surface);
    }
    const bool has_stage = !desc.stage_text.empty();
    RALLY_CHECK(!(has_stage && desc.test_zone),
                "SimWorld::build: un mundo no puede tener tramo y zona de pruebas a la vez");
    RALLY_CHECK(has_stage || desc.stage_variant == StageGroundVariant::Design,
                "SimWorld::build: una variante de medida del suelo del tramo sin tramo");
    if (has_stage) {
        auto track = std::make_unique<StageTrack>();
        const StageParseResult parsed = StageTrack::build(desc.stage_text, *track);
        RALLY_CHECK(parsed.ok, "SimWorld::build: el tramo no vale (quien llama lo lee antes con "
                               "StageTrack::build para enseñar el error)");
        w.stage = std::move(track);
        w.stage_run = std::make_unique<StageRun>(*w.stage);
    }
    const std::size_t zone_bodies = w.zone ? 1 + kWallCount + w.zone->road().trees().size() : 0;
    RALLY_CHECK(desc.bodies.size() + zone_bodies + (has_stage ? 1 : 0) + 1 <= kMaxBodies,
                "SimWorld::build: demasiados cuerpos");
    RALLY_CHECK(!desc.car || desc.car_data.mass > 0.0f,
                "SimWorld::build: el coche no tiene datos (WorldDesc::car_data)");
    RALLY_CHECK(!desc.car || desc.aids.suspension == SuspensionSetting::Medium ||
                    desc.car_data.has_settings,
                "SimWorld::build: la suspensión blanda o dura necesita la sección [settings] del "
                "coche");
    const int count = static_cast<int>(desc.bodies.size());

    JPH::BodyInterface& bodies = w.system.GetBodyInterfaceNoLock();
    // Lote de cuerpos en el orden de creación: el suelo y los muros (si hay zona), la
    // descripción y el coche.
    std::vector<JPH::BodyID> batch;
    if (desc.test_zone) {
        w.lap = std::make_unique<LapTimer>(w.zone->finish_line());
        w.road = std::make_unique<RoadTimer>(w.zone->road_start_line(), w.zone->road_finish_line());
        std::vector<float> heights;
        std::vector<std::uint8_t> surfaces;
        w.zone->fill_ground(heights, surfaces);
        w.ground_hash = sim::ground_hash(heights, surfaces);
        w.zone_hash = sim::zone_hash(heights, surfaces, *w.zone);
        JPH::PhysicsMaterialList materials;
        for (int s = 0; s < kSurfaceCount; ++s) {
            materials.push_back(new SurfaceMaterial(static_cast<Surface>(s), MicroRelief::Zone));
        }
        JPH::HeightFieldShapeSettings ground_settings(
            heights.data(),
            JPH::Vec3(static_cast<float>(zone::kGroundOrigin), 0.0f,
                      static_cast<float>(zone::kGroundOrigin)),
            JPH::Vec3(static_cast<float>(zone::kGroundSpacing), 1.0f,
                      static_cast<float>(zone::kGroundSpacing)),
            zone::kGroundSamples, surfaces.data(), materials);
        ground_settings.mBlockSize = kGroundBlockSize;
        ground_settings.mBitsPerSample = kGroundBitsPerSample;
        const JPH::ShapeSettings::ShapeResult shape = ground_settings.Create();
        RALLY_CHECK(shape.IsValid(), "SimWorld::build: Jolt no pudo crear el suelo de la zona");
        w.ground_bytes = shape.Get()->GetStats().mSizeBytes;
        const JPH::BodyCreationSettings settings(shape.Get(), JPH::RVec3::sZero(),
                                                 JPH::Quat::sIdentity(), JPH::EMotionType::Static,
                                                 layers::kStatic);
        const JPH::Body* body = bodies.CreateBody(settings);
        RALLY_CHECK(body != nullptr, "SimWorld::build: Jolt no tiene cuerpos libres");
        w.ground = body->GetID();
        batch.push_back(w.ground);
        // Muros perimetrales, después del suelo y antes de la descripción (orden fijo).
        for (const WallBox& wall : w.zone->walls()) {
            const JPH::BodyCreationSettings wall_settings(
                new JPH::BoxShape(JPH::Vec3(static_cast<float>(wall.half_x),
                                            static_cast<float>(wall.half_y),
                                            static_cast<float>(wall.half_z)),
                                  JPH::cDefaultConvexRadius,
                                  new SurfaceMaterial(Surface::Asphalt, MicroRelief::None)),
                JPH::RVec3(wall.cx, wall.cy, wall.cz), JPH::Quat::sIdentity(),
                JPH::EMotionType::Static, layers::kStatic);
            const JPH::Body* wall_body = bodies.CreateBody(wall_settings);
            RALLY_CHECK(wall_body != nullptr, "SimWorld::build: Jolt no tiene cuerpos libres");
            batch.push_back(wall_body->GetID());
        }
        // Árboles de la carretera, detrás de los muros y en el orden de la zona: cilindros
        // estáticos con una forma compartida, la base hundida kTreeSink en el suelo.
        const JPH::Ref<JPH::Shape> trunk = new JPH::CylinderShape(
            static_cast<float>(zone::kTreeHalfHeight), static_cast<float>(zone::kTreeRadius),
            JPH::cDefaultConvexRadius, new SurfaceMaterial(Surface::Dirt, MicroRelief::None));
        w.trees.reserve(w.zone->road().trees().size());
        for (const TreeSpot& tree : w.zone->road().trees()) {
            const JPH::BodyCreationSettings tree_settings(
                trunk, JPH::RVec3(tree.x, tree.y + zone::kTreeHalfHeight - zone::kTreeSink, tree.z),
                JPH::Quat::sIdentity(), JPH::EMotionType::Static, layers::kStatic);
            const JPH::Body* tree_body = bodies.CreateBody(tree_settings);
            RALLY_CHECK(tree_body != nullptr, "SimWorld::build: Jolt no tiene cuerpos libres");
            w.trees.push_back(tree_body->GetID());
            batch.push_back(tree_body->GetID());
        }
    }
    if (has_stage) {
        // Suelo del tramo: un cuerpo estático, antes de los de la descripción. La geometría se
        // calcula aquí, va a Jolt y se libera; queda su hash.
        const StageGround ground = StageGround::build(
            *w.stage, stage_cell(desc.stage_variant),
            desc.stage_variant == StageGroundVariant::GridOnly ? StageGroundKind::GridOnly
            : desc.stage_variant == StageGroundVariant::NaturalTerrain
                ? StageGroundKind::NaturalOutside
            : desc.stage_variant == StageGroundVariant::Band ||
                    desc.stage_variant == StageGroundVariant::BandDense
                ? StageGroundKind::Band
                : StageGroundKind::Design,
            desc.stage_variant == StageGroundVariant::DenseMesh ||
                    desc.stage_variant == StageGroundVariant::BandDense
                ? kStageDenseMesh
                : StageMeshDensity{});
        RALLY_CHECK(ground.band_error().empty(),
                    "SimWorld::build: no cabe el talud entre pasadas (formato 2)");
        w.stage_ground_hash = ground.hash();
        const JPH::Ref<JPH::Shape> shape =
            stage_shape(ground, desc.stage_variant != StageGroundVariant::TerrainOnly,
                        desc.stage_micro_relief ? MicroRelief::Stage : MicroRelief::None);
        JPH::Shape::VisitedShapes visited;
        w.stage_ground_bytes = shape->GetStatsRecursive(visited).mSizeBytes;
        const JPH::BodyCreationSettings settings(
            shape, JPH::RVec3(ground.origin_x(), ground.origin_y(), ground.origin_z()),
            JPH::Quat::sIdentity(), JPH::EMotionType::Static, layers::kStatic);
        const JPH::Body* body = bodies.CreateBody(settings);
        RALLY_CHECK(body != nullptr, "SimWorld::build: Jolt no tiene cuerpos libres");
        w.stage_body = body->GetID();
        batch.push_back(w.stage_body);
        // Las bandas de h de las superficies (MicroRelief::Stage las mezcla en los bordes).
        w.noise = std::make_unique<TestZone>();
    }
    w.bodies.reserve(desc.bodies.size());
    for (int i = 0; i < count; ++i) {
        const BoxDesc& box = desc.bodies[static_cast<std::size_t>(i)];
        const bool dynamic = box.motion == BodyMotion::Dynamic;
        if (box.micro && !w.noise) {
            w.noise = std::make_unique<TestZone>();
        }
        const JPH::BodyCreationSettings settings(
            new JPH::BoxShape(JPH::Vec3(box.half_x, box.half_y, box.half_z),
                              JPH::cDefaultConvexRadius,
                              new SurfaceMaterial(box.surface, box.micro ? MicroRelief::Noise
                                                                         : MicroRelief::None)),
            JPH::RVec3(box.x, box.y, box.z), JPH::Quat::sRotation(JPH::Vec3::sAxisX(), box.pitch),
            dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
            dynamic ? layers::kMoving : layers::kStatic);
        const JPH::Body* body = bodies.CreateBody(settings);
        RALLY_CHECK(body != nullptr, "SimWorld::build: Jolt no tiene cuerpos libres");
        w.bodies.push_back(body->GetID());
    }

    // AddBodiesPrepare reordena el array por capas: se le pasa una copia para conservar el
    // orden de la descripción en `bodies`.
    batch.insert(batch.end(), w.bodies.begin(), w.bodies.end());
    JPH::Body* car_body = nullptr;
    if (desc.car) {
        const CarSpawn& spawn = desc.car_spawn;
        car_body = bodies.CreateBody(car_body_settings(desc.car_data,
                                                       JPH::RVec3(spawn.x, spawn.y, spawn.z),
                                                       w.spawn_rotation(spawn), spawn.speed));
        RALLY_CHECK(car_body != nullptr, "SimWorld::build: Jolt no tiene cuerpos libres");
        w.car = car_body->GetID();
        w.car_mass = desc.car_data.mass;
        w.car_spawn = spawn;
        w.aids = desc.aids;
        batch.push_back(w.car);
    }
    if (!batch.empty()) {
        const int batch_count = static_cast<int>(batch.size());
        const JPH::BodyInterface::AddState add_state =
            bodies.AddBodiesPrepare(batch.data(), batch_count);
        bodies.AddBodiesFinalize(batch.data(), batch_count, add_state, JPH::EActivation::Activate);
    }
    if (car_body != nullptr) {
        w.car_constraint =
            make_car_constraint(*car_body, desc.car_data, -w.system.GetGravity().GetY(), desc.aids,
                                desc.explicit_wheels);
        w.car_controller = static_cast<CarController*>(w.car_constraint->GetController());
        RALLY_CHECK(!w.stage || (w.stage_run && w.noise),
                    "SimWorld::build: el tramo necesita su pasada y las bandas de h");
        w.car_tester =
            new CarWheelTester(layers::kMoving,
                               ReliefSource{w.zone.get(), w.noise.get(), w.stage.get(),
                                            w.stage_run ? &w.stage_run->state() : nullptr},
                               *w.car_controller);
        w.car_constraint->SetVehicleCollisionTester(w.car_tester);
        w.car_controller->set_rolling(desc.car_spawn.speed);
        if (desc.car_spawn.gear != kSpawnGearBySpeed) {
            w.car_controller->set_gear(desc.car_spawn.gear);
        }
        w.system.AddConstraint(w.car_constraint);
        w.system.AddStepListener(w.car_constraint);
        if (w.stage_run) {
            const CarSpawn& spawn = desc.car_spawn;
            w.stage_run->placed(StagePlace::Start, w.stage->acquire(spawn.x, spawn.z).s, spawn.x,
                                spawn.z);
        }
    }
    w.state = State::Built;
}

void SimWorld::seal() {
    Impl& w = *impl_;
    RALLY_CHECK(w.state == State::Built, "SimWorld::seal: falta build() o ya está sellado");
    w.system.OptimizeBroadPhase();
    w.state = State::Sealed;
}

bool SimWorld::is_sealed() const { return impl_->state == State::Sealed; }

void SimWorld::step(const SimInput& input) {
    Impl& w = *impl_;
    RALLY_CHECK(w.state == State::Sealed,
                "SimWorld::step: el mundo no está sellado (falta seal())");

    // La ventana de la ayuda y la velocidad de las manos, con el estado al principio del paso.
    w.steer_context = SteerContext{};
    if (w.car_controller != nullptr) {
        w.steer_context = sim::steer_context(w.car_controller->steer_assist_input(),
                                             w.car_controller->car(), w.aids.steer_assist);
    }
    w.steer_aided = w.controls.step(input, w.steer_context);
    const bool reset_pressed = (w.controls.pressed & kButtonReset) != 0;
    if (reset_pressed) {
        w.controls.zero_axes();
    }
    // Posición y velocidad antes del paso, para el cronómetro, el impacto y la telemetría.
    double before_x = 0.0;
    double before_z = 0.0;
    JPH::Vec3 before_velocity = JPH::Vec3::sZero();
    if (w.car_controller != nullptr) {
        JPH::BodyInterface& bodies = w.system.GetBodyInterfaceNoLock();
        const JPH::RVec3 before = bodies.GetPosition(w.car);
        before_x = before.GetX();
        before_z = before.GetZ();
        before_velocity = bodies.GetLinearVelocity(w.car);
        constexpr float kAxisScale = 1.0f / kAxisMax;
        CarControls controls;
        controls.steer =
            rmath::clamp(static_cast<float>(w.controls.steer.applied) * kAxisScale, -1.0f, 1.0f);
        controls.throttle =
            rmath::clamp(static_cast<float>(w.controls.throttle.applied) * kAxisScale, 0.0f, 1.0f);
        controls.brake =
            rmath::clamp(static_cast<float>(w.controls.brake.applied) * kAxisScale, 0.0f, 1.0f);
        controls.handbrake =
            rmath::clamp(static_cast<float>(w.controls.handbrake) * kAxisScale, 0.0f, 1.0f);
        controls.shift_up = (w.controls.pressed & kButtonShiftUp) != 0;
        controls.shift_down = (w.controls.pressed & kButtonShiftDown) != 0;
        w.car_controller->set_controls(controls);
    }

    const JPH::EPhysicsUpdateError error =
        w.system.Update(kStepSeconds, 1, &w.temp_allocator, &w.job_system);
    RALLY_CHECK(error == JPH::EPhysicsUpdateError::None,
                "SimWorld::step: Jolt devolvió un error en Update");

    bool moved = reset_pressed; // recolocado en este paso (botón o atajo)
    if (w.car_controller != nullptr) {
        JPH::BodyInterface& bodies = w.system.GetBodyInterfaceNoLock();
        if (w.stage_run) {
            // El único sitio que cuenta el paso de la pasada, con botón o sin él.
            w.stage_run->begin_step();
        }
        if (reset_pressed) {
            w.reset_car();
            w.last_impact = 0.0f;
        } else {
            // Lo que explican la gravedad y las fuerzas propias (el coche no tiene
            // amortiguamiento de Jolt).
            const JPH::Vec3 expected =
                before_velocity +
                (w.system.GetGravity() + w.car_controller->applied_force() / w.car_mass) *
                    kStepSeconds;
            w.last_impact = (bodies.GetLinearVelocity(w.car) - expected).Length();
        }
        if (w.lap) {
            const JPH::RVec3 after = bodies.GetPosition(w.car);
            w.lap->update(before_x, before_z, after.GetX(), after.GetZ(), reset_pressed);
            w.road->update(before_x, before_z, after.GetX(), after.GetZ(), reset_pressed);
        }
        // Tramo: con el botón ya está en stage_spawn(reset_s) (reset_car); si no, la pasada
        // mira el origen del coche y, con un atajo, lo recoloca en exit_s en el mismo paso.
        if (w.stage_run && !reset_pressed) {
            const JPH::RVec3 at = bodies.GetPosition(w.car);
            const StageRun::Step run = w.stage_run->step(at.GetX(), at.GetZ());
            if (run.shortcut) {
                w.place_on_stage(run.reset_s);
                const StagePose p = w.stage->pose(run.reset_s);
                w.stage_run->placed(StagePlace::Shortcut, run.reset_s, p.x, p.z);
                w.last_impact = 0.0f;
                moved = true;
            }
        }
    }
    ++w.steps;
    if (w.telemetry_enabled) {
        w.capture_telemetry(input, before_velocity, moved);
    }
}

void SimWorld::Impl::place_on_stage(double s) {
    Impl& w = *this;
    JPH::BodyInterface& body_interface = w.system.GetBodyInterfaceNoLock();
    const CarSpawn spawn = stage_spawn(*w.stage, s);
    body_interface.SetPositionAndRotation(w.car, JPH::RVec3(spawn.x, spawn.y, spawn.z),
                                          on_road_plane(w.stage->pose(s), spawn.yaw),
                                          JPH::EActivation::Activate);
    body_interface.SetLinearAndAngularVelocity(w.car, JPH::Vec3::sZero(), JPH::Vec3::sZero());
    w.car_controller->reset();
    w.car_constraint->ResetWarmStart();
}

void SimWorld::Impl::reset_car() {
    Impl& w = *this;
    if (w.stage_run) {
        const double s = w.stage_run->state().reset_s;
        w.place_on_stage(s);
        const StagePose p = w.stage->pose(s);
        w.stage_run->placed(StagePlace::Button, s, p.x, p.z);
        return;
    }
    JPH::BodyInterface& body_interface = w.system.GetBodyInterfaceNoLock();
    // Mover y poner a cero, nunca quitar y añadir. Un traslado no es un impacto.
    JPH::RVec3 target(w.car_spawn.x, w.car_spawn.y, w.car_spawn.z);
    JPH::Quat rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), w.car_spawn.yaw);
    if (w.zone) {
        const JPH::RVec3 at = body_interface.GetPosition(w.car);
        const RespawnPoint& r =
            w.zone->respawns()[static_cast<std::size_t>(w.zone->respawn_for(at.GetX(), at.GetZ()))];
        // El origen del coche está en el suelo.
        target = JPH::RVec3(r.x, w.zone->ground_height(r.x, r.z), r.z);
        rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), static_cast<float>(r.yaw));
    }
    body_interface.SetPositionAndRotation(w.car, target, rotation, JPH::EActivation::Activate);
    body_interface.SetLinearAndAngularVelocity(w.car, JPH::Vec3::sZero(), JPH::Vec3::sZero());
    w.car_controller->reset();
    w.car_constraint->ResetWarmStart();
}

void SimWorld::Impl::capture_telemetry(const SimInput& input, JPH::Vec3Arg before_velocity,
                                       bool reset) {
    Impl& w = *this;
    CarTelemetry& t = w.telemetry;
    t = CarTelemetry{};
    t.step = w.steps;
    t.target = input;
    if (w.car_controller == nullptr) {
        return;
    }
    const JPH::BodyInterface& body_interface = w.system.GetBodyInterfaceNoLock();
    const JPH::RVec3 com = body_interface.GetCenterOfMassPosition(w.car);
    const JPH::Quat rotation = body_interface.GetRotation(w.car);
    JPH::Vec3 v;
    JPH::Vec3 omega;
    body_interface.GetLinearAndAngularVelocity(w.car, v, omega);
    const JPH::Vec3 forward = rotation * JPH::Vec3(0.0f, 0.0f, -1.0f);
    const JPH::Vec3 right = rotation * JPH::Vec3(1.0f, 0.0f, 0.0f);
    const JPH::Vec3 up = rotation * JPH::Vec3(0.0f, 1.0f, 0.0f);

    t.x = com.GetX();
    t.y = com.GetY();
    t.z = com.GetZ();
    t.vx = v.GetX();
    t.vy = v.GetY();
    t.vz = v.GetZ();
    t.v_forward = v.Dot(forward);
    t.v_right = v.Dot(right);
    t.v_up = v.Dot(up);
    t.speed = v.Length();
    t.heading = rmath::atan2(forward.GetX(), -forward.GetZ());
    t.yaw_rate = -omega.Dot(up);
    t.beta = rmath::atan2(t.v_right, t.v_forward);
    // Recolocar es un traslado, no una aceleración.
    const JPH::Vec3 a = reset ? JPH::Vec3::sZero() : (v - before_velocity) / kStepSeconds;
    t.ax = a.GetX();
    t.ay = a.GetY();
    t.az = a.GetZ();
    t.a_forward = a.Dot(forward);
    t.a_right = a.Dot(right);

    constexpr float kAxisScale = 1.0f / kAxisMax;
    const ControlState& c = w.controls;
    t.steer = static_cast<float>(c.steer.applied) * kAxisScale;
    t.throttle = static_cast<float>(c.throttle.applied) * kAxisScale;
    t.brake = static_cast<float>(c.brake.applied) * kAxisScale;
    t.handbrake = static_cast<float>(c.handbrake) * kAxisScale;
    t.pressed = c.pressed;
    t.steer_aided = static_cast<float>(w.steer_aided) * kAxisScale;
    t.steer_digital = c.steer.digital;
    t.assist_active = w.steer_context.active;
    t.assist_rear_sliding = w.steer_context.rear_sliding;
    t.assist_right_max = static_cast<float>(w.steer_context.right_max) * kAxisScale;
    t.assist_left_max = static_cast<float>(w.steer_context.left_max) * kAxisScale;
    t.steer_speed = w.steer_context.speed;
    if (w.zone) {
        t.surface = static_cast<std::int8_t>(w.zone->surface_at(t.x, t.z));
    }
    if (w.lap) {
        const LapState& lap_state = w.lap->state();
        t.laps = lap_state.laps;
        t.lap_steps = lap_state.current();
        const RoadRunState& run = w.road->state();
        t.road_steps = run.current();
        t.road_resets = run.resets;
        t.road_runs = run.runs;
    }
    t.impact = w.last_impact;
    const CarController& controller = *w.car_controller;
    const DrivetrainOutput& drive = controller.drivetrain();
    t.gear = static_cast<std::int8_t>(drive.gear);
    t.target_gear = static_cast<std::int8_t>(drive.target_gear);
    t.gearbox = static_cast<std::uint8_t>(controller.gearbox());
    t.shifting = drive.shifting;
    t.clutch_mode = static_cast<std::uint8_t>(drive.clutch);
    t.clutch_open = drive.clutch_open;
    t.engine_omega = drive.engine_omega;
    t.boost = drive.boost;
    t.engine_throttle = drive.throttle;
    t.engine_torque = drive.engine_torque;
    t.clutch_slip = drive.clutch_slip;
    t.clutch_torque = drive.clutch_torque;
    t.axle_torque_front = drive.axle_torque[0];
    t.axle_torque_rear = drive.axle_torque[1];
    t.center_slip = drive.center_slip;
    t.viscous_torque = drive.viscous_torque;
    t.lsd_torque_front = drive.lsd_torque[0];
    t.lsd_torque_rear = drive.lsd_torque[1];
    t.drift_hold = drive.drift_hold;
    t.lcp_iterations = static_cast<std::uint8_t>(drive.lcp_iterations);
    t.lcp_failures = static_cast<std::uint8_t>(drive.lcp_failures);
    t.traction_torque_cut = drive.traction_torque_cut;
    for (int i = 0; i < kWheelCount; ++i) {
        const WheelOutput& out = controller.wheel(i);
        WheelTelemetry& wt = t.wheels[static_cast<std::size_t>(i)];
        wt.contact = out.contact ? 1 : 0;
        wt.surface = out.contact ? static_cast<std::int8_t>(out.surface) : -1;
        wt.load = out.load;
        wt.compression = out.compression;
        wt.kappa = out.kappa;
        wt.alpha = out.alpha;
        wt.slip_x = out.slip_x;
        wt.slip_y = out.slip_y;
        wt.fx = out.fx;
        wt.fy = out.fy;
        wt.fx_pure = out.fx_pure;
        wt.fy_pure = out.fy_pure;
        wt.mu = out.mu;
        wt.omega = out.omega;
        wt.brake_torque = out.brake_torque;
        wt.h = out.h;
        wt.abs_torque_cut = out.abs_torque_cut;
    }
}

void SimWorld::set_telemetry(bool enabled) { impl_->telemetry_enabled = enabled; }

bool SimWorld::telemetry_enabled() const { return impl_->telemetry_enabled; }

const CarTelemetry& SimWorld::telemetry() const { return impl_->telemetry; }

const ControlState& SimWorld::controls() const { return impl_->controls; }

const SteerContext& SimWorld::steer_context() const { return impl_->steer_context; }

std::int32_t SimWorld::steer_aided() const { return impl_->steer_aided; }

float SimWorld::last_impact() const { return impl_->last_impact; }

bool SimWorld::has_car() const { return impl_->car_controller != nullptr; }

BodyPose SimWorld::car_pose() const {
    RALLY_CHECK(has_car(), "SimWorld::car_pose: el mundo no tiene coche");
    JPH::RVec3 position;
    JPH::Quat rotation;
    impl_->system.GetBodyInterfaceNoLock().GetPositionAndRotation(impl_->car, position, rotation);
    return BodyPose{position.GetX(), position.GetY(), position.GetZ(), rotation.GetX(),
                    rotation.GetY(), rotation.GetZ(), rotation.GetW()};
}

BodyPose SimWorld::car_center_of_mass() const {
    BodyPose pose = car_pose();
    const JPH::RVec3 com =
        impl_->system.GetBodyInterfaceNoLock().GetCenterOfMassPosition(impl_->car);
    pose.x = com.GetX();
    pose.y = com.GetY();
    pose.z = com.GetZ();
    return pose;
}

BodyVelocity SimWorld::car_velocity() const {
    RALLY_CHECK(has_car(), "SimWorld::car_velocity: el mundo no tiene coche");
    JPH::Vec3 linear;
    JPH::Vec3 angular;
    impl_->system.GetBodyInterfaceNoLock().GetLinearAndAngularVelocity(impl_->car, linear, angular);
    return BodyVelocity{linear.GetX(),  linear.GetY(),  linear.GetZ(),
                        angular.GetX(), angular.GetY(), angular.GetZ()};
}

float SimWorld::car_mass() const {
    RALLY_CHECK(has_car(), "SimWorld::car_mass: el mundo no tiene coche");
    const JPH::BodyLockRead lock(impl_->system.GetBodyLockInterfaceNoLock(), impl_->car);
    return 1.0f / lock.GetBody().GetMotionProperties()->GetInverseMass();
}

const CarController& SimWorld::car_controller() const {
    RALLY_CHECK(has_car(), "SimWorld::car_controller: el mundo no tiene coche");
    return *impl_->car_controller;
}

SimWorld::WheelPose SimWorld::car_wheel(int index) const {
    RALLY_CHECK(has_car() && index >= 0 && index < kWheelCount,
                "SimWorld::car_wheel: sin coche o rueda fuera de rango");
    const JPH::Wheel* wheel = impl_->car_constraint->GetWheel(static_cast<JPH::uint>(index));
    const JPH::WheelSettings* settings = wheel->GetSettings();
    WheelPose pose;
    pose.attach_x = settings->mPosition.GetX();
    pose.attach_y = settings->mPosition.GetY();
    pose.attach_z = settings->mPosition.GetZ();
    pose.suspension_length = wheel->GetSuspensionLength();
    pose.steer = wheel->GetSteerAngle();
    pose.rotation = wheel->GetRotationAngle();
    pose.radius = impl_->car_controller->wheel_radius(index);
    pose.width = settings->mWidth;
    return pose;
}

void SimWorld::set_wheel_radius(int index, float radius) {
    RALLY_CHECK(has_car() && index >= 0 && index < kWheelCount && radius > 0.0f,
                "SimWorld::set_wheel_radius: sin coche, rueda fuera de rango o radio no válido");
    impl_->car_controller->set_wheel_radius(index, radius);
}

void SimWorld::set_car_velocity(const BodyVelocity& velocity) {
    RALLY_CHECK(has_car(), "SimWorld::set_car_velocity: el mundo no tiene coche");
    impl_->system.GetBodyInterfaceNoLock().SetLinearAndAngularVelocity(
        impl_->car, JPH::Vec3(velocity.vx, velocity.vy, velocity.vz),
        JPH::Vec3(velocity.wx, velocity.wy, velocity.wz));
}

int SimWorld::body_count() const { return static_cast<int>(impl_->bodies.size()); }

std::uint32_t SimWorld::body_id(int index) const {
    RALLY_CHECK(index >= 0 && index < body_count(), "SimWorld::body_id: índice fuera de rango");
    return impl_->bodies[static_cast<std::size_t>(index)].GetIndexAndSequenceNumber();
}

BodyPose SimWorld::pose(int index) const {
    RALLY_CHECK(index >= 0 && index < body_count(), "SimWorld::pose: índice fuera de rango");
    JPH::RVec3 position;
    JPH::Quat rotation;
    impl_->system.GetBodyInterfaceNoLock().GetPositionAndRotation(
        impl_->bodies[static_cast<std::size_t>(index)], position, rotation);
    return BodyPose{position.GetX(), position.GetY(), position.GetZ(), rotation.GetX(),
                    rotation.GetY(), rotation.GetZ(), rotation.GetW()};
}

BodyVelocity SimWorld::velocity(int index) const {
    RALLY_CHECK(index >= 0 && index < body_count(), "SimWorld::velocity: índice fuera de rango");
    JPH::Vec3 linear;
    JPH::Vec3 angular;
    impl_->system.GetBodyInterfaceNoLock().GetLinearAndAngularVelocity(
        impl_->bodies[static_cast<std::size_t>(index)], linear, angular);
    return BodyVelocity{linear.GetX(),  linear.GetY(),  linear.GetZ(),
                        angular.GetX(), angular.GetY(), angular.GetZ()};
}

const TestZone* SimWorld::zone() const { return impl_->zone.get(); }

const LapState* SimWorld::lap() const { return impl_->lap ? &impl_->lap->state() : nullptr; }

const RoadRunState* SimWorld::road_run() const {
    return impl_->road ? &impl_->road->state() : nullptr;
}

int SimWorld::tree_count() const { return static_cast<int>(impl_->trees.size()); }

BodyPose SimWorld::tree_pose(int index) const {
    RALLY_CHECK(index >= 0 && index < tree_count(), "SimWorld::tree_pose: índice fuera de rango");
    const JPH::BodyInterface& bodies = impl_->system.GetBodyInterfaceNoLock();
    const JPH::BodyID id = impl_->trees[static_cast<std::size_t>(index)];
    const JPH::RVec3 p = bodies.GetPosition(id);
    const JPH::Quat q = bodies.GetRotation(id);
    return {p.GetX(), p.GetY(), p.GetZ(), q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
}

std::uint64_t SimWorld::ground_hash() const { return impl_->ground_hash; }

std::uint64_t SimWorld::zone_hash() const { return impl_->zone_hash; }

std::size_t SimWorld::ground_bytes() const { return impl_->ground_bytes; }

const StageTrack* SimWorld::stage() const { return impl_->stage.get(); }

const StageRunState* SimWorld::stage_run() const {
    return impl_->stage_run ? &impl_->stage_run->state() : nullptr;
}

std::uint64_t SimWorld::stage_ground_hash() const { return impl_->stage_ground_hash; }

std::size_t SimWorld::stage_ground_bytes() const { return impl_->stage_ground_bytes; }

GroundProbe SimWorld::probe_ground(double x, double z) const {
    GroundProbe probe;
    const JPH::BodyID ground = impl_->ground.IsInvalid() ? impl_->stage_body : impl_->ground;
    if (ground.IsInvalid()) {
        return probe;
    }
    constexpr double kTop = 1000.0;
    const JPH::TransformedShape shape =
        impl_->system.GetBodyInterfaceNoLock().GetTransformedShape(ground);
    const JPH::RRayCast ray(JPH::RVec3(x, kTop, z), JPH::Vec3(0.0f, -2.0f * kTop, 0.0f));
    JPH::RayCastResult hit;
    if (!shape.CastRay(ray, hit)) {
        return probe;
    }
    probe.hit = true;
    probe.y = ray.GetPointOnRay(hit.mFraction).GetY();
    const auto* material = static_cast<const SurfaceMaterial*>(shape.GetMaterial(hit.mSubShapeID2));
    probe.surface = material->surface();
    return probe;
}

std::string SimWorld::save_state() const {
    JPH::StateRecorderImpl recorder;
    impl_->system.SaveState(recorder);
    return recorder.GetData();
}

void SimWorld::restore_state(const std::string& state, const ControlState& controls) {
    Impl& w = *impl_;
    RALLY_CHECK(w.state == State::Sealed, "SimWorld::restore_state: el mundo no está sellado");
    JPH::StateRecorderImpl recorder;
    recorder.WriteBytes(state.data(), state.size());
    RALLY_CHECK(w.system.RestoreState(recorder),
                "SimWorld::restore_state: Jolt no pudo restaurar el estado");
    w.controls = controls;
}

namespace {

void add_axis(Fnv1a64& hash, const SmoothedAxis& axis) {
    hash.add_u32(static_cast<std::uint32_t>(axis.applied));
    hash.add_byte(axis.digital ? 1 : 0);
}

} // namespace

std::uint64_t SimWorld::state_hash() const {
    Fnv1a64 hash;
    hash.add_bytes(save_state());
    const ControlState& c = impl_->controls;
    add_axis(hash, c.steer);
    add_axis(hash, c.throttle);
    add_axis(hash, c.brake);
    hash.add_u32(static_cast<std::uint32_t>(c.handbrake));
    hash.add_u16(c.previous_buttons);
    hash.add_u16(c.pressed);
    if (impl_->lap) {
        const LapState& lap = impl_->lap->state();
        const auto add_i64 = [&hash](std::int64_t value) {
            const auto bits = static_cast<std::uint64_t>(value);
            hash.add_u32(static_cast<std::uint32_t>(bits));
            hash.add_u32(static_cast<std::uint32_t>(bits >> 32));
        };
        add_i64(lap.steps);
        add_i64(lap.lap_start);
        hash.add_u32(static_cast<std::uint32_t>(lap.debt));
        hash.add_u32(static_cast<std::uint32_t>(lap.laps));
        add_i64(lap.last_lap);
        add_i64(lap.best_lap);
        const RoadRunState& run = impl_->road->state();
        add_i64(run.steps);
        add_i64(run.run_start);
        hash.add_u32(static_cast<std::uint32_t>(run.resets));
        hash.add_u32(static_cast<std::uint32_t>(run.start_debt));
        hash.add_u32(static_cast<std::uint32_t>(run.finish_debt));
        hash.add_u32(static_cast<std::uint32_t>(run.runs));
        add_i64(run.last_run);
        hash.add_u32(static_cast<std::uint32_t>(run.last_resets));
        add_i64(run.best_clean);
    }
    if (impl_->stage_run) {
        hash_stage_run(hash, impl_->stage_run->state());
    }
    return hash.value();
}

} // namespace rally::sim
