#include "car_worlds.h"
#include "platform/button_latch.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"

#include <doctest/doctest.h>

#include <cstdint>

using rally::platform::ButtonLatch;
using rally::sim::kButtonReset;

namespace {
constexpr std::uint16_t kOther = 1u << 1;
} // namespace

TEST_CASE("button_latch: a press inside a frame with no steps reaches the next frame with steps") {
    ButtonLatch latch;
    latch.apply(kButtonReset, 0);             // empieza y acaba en un fotograma de 0 pasos
    CHECK(latch.apply(0, 0) == kButtonReset); // otro fotograma vacío: sigue retenida
    CHECK(latch.apply(0, 4) == kButtonReset); // llega al primer fotograma con pasos
    CHECK(latch.apply(0, 4) == 0);            // y después se limpia
}

TEST_CASE("button_latch: presses in several empty frames are combined") {
    ButtonLatch latch;
    latch.apply(kButtonReset, 0);
    latch.apply(kOther, 0);
    CHECK(latch.apply(0, 1) == (kButtonReset | kOther));
}

TEST_CASE("button_latch: frames with steps pass their buttons through") {
    ButtonLatch latch;
    CHECK(latch.apply(kButtonReset, 4) == kButtonReset); // mantenido
    CHECK(latch.apply(kButtonReset, 4) == kButtonReset);
    CHECK(latch.apply(0, 4) == 0);
}

TEST_CASE("button_latch: a press in a frame with no steps resets the car on the next step") {
    const rally::sim::JoltRuntime runtime;
    rally::sim::WorldDesc desc = rally::tests::flat_world(rally::sim::Surface::Asphalt);
    rally::sim::SimWorld world;
    world.build(desc);
    world.seal();
    rally::sim::SimInput drive;
    drive.throttle = rally::sim::kAxisMax;
    for (int i = 0; i < rally::sim::kStepsPerSecond; ++i) {
        world.step(drive);
    }
    REQUIRE(world.car_pose().z < -1.0);

    // Fotogramas como los de la demo sin sincronización vertical: uno de 0 pasos con la
    // pulsación entera y luego uno de 1 paso sin la tecla.
    ButtonLatch latch;
    rally::sim::SimInput input;
    input.buttons = latch.apply(kButtonReset, 0);
    input.buttons = latch.apply(0, 1);
    world.step(input);
    CHECK(world.car_pose().z == 0.0);
}
