#include "platform/button_latch.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"

#include <doctest/doctest.h>

#include <cstdint>

using rally::platform::ButtonLatch;
using rally::sim::kButtonJump;

namespace {
constexpr std::uint16_t kOther = 1u << 1;
} // namespace

TEST_CASE("button_latch: a press inside a frame with no steps reaches the next frame with steps") {
    ButtonLatch latch;
    latch.apply(kButtonJump, 0);             // empieza y acaba en un fotograma de 0 pasos
    CHECK(latch.apply(0, 0) == kButtonJump); // otro fotograma vacío: sigue retenida
    CHECK(latch.apply(0, 4) == kButtonJump); // llega al primer fotograma con pasos
    CHECK(latch.apply(0, 4) == 0);           // y después se limpia
}

TEST_CASE("button_latch: presses in several empty frames are combined") {
    ButtonLatch latch;
    latch.apply(kButtonJump, 0);
    latch.apply(kOther, 0);
    CHECK(latch.apply(0, 1) == (kButtonJump | kOther));
}

TEST_CASE("button_latch: frames with steps pass their buttons through") {
    ButtonLatch latch;
    CHECK(latch.apply(kButtonJump, 4) == kButtonJump); // mantenido
    CHECK(latch.apply(kButtonJump, 4) == kButtonJump);
    CHECK(latch.apply(0, 4) == 0);
}

TEST_CASE("button_latch: a press in a frame with no steps makes the box jump on the next step") {
    const rally::sim::JoltRuntime runtime;
    rally::sim::WorldDesc desc;
    desc.bodies = {
        {rally::sim::BodyMotion::Static, 0.0, -0.5, 0.0, 5.0f, 0.5f, 5.0f},
        {rally::sim::BodyMotion::Dynamic, 0.0, 0.5, 0.0, 0.5f, 0.5f, 0.5f},
    };
    desc.controlled = 1;
    rally::sim::SimWorld world;
    world.build(desc);
    world.seal();

    // Fotogramas como los de la demo sin sincronización vertical: uno de 0 pasos con la
    // pulsación entera y luego uno de 1 paso sin la tecla.
    ButtonLatch latch;
    rally::sim::SimInput input;
    input.buttons = latch.apply(kButtonJump, 0);
    input.buttons = latch.apply(0, 1);
    world.step(input);
    CHECK(world.pose(1).y > 0.5 + 0.01);
}
