// Cabeza del conductor (tarea 2.7): resorte-amortiguador por paso, recorte de la entrada y límite
// suave.

#include "platform/head_motion.h"
#include "sim/pcg32.h"

#include <doctest/doctest.h>

#include <cmath>

using rally::platform::HeadInput;
using rally::platform::HeadMotion;
using rally::platform::HeadOffset;
using rally::platform::kHeadGravity;

namespace {

constexpr int kStepsPerSecond = 240;

void run(HeadMotion& head, const HeadInput& input, double seconds) {
    const int steps = static_cast<int>(seconds * kStepsPerSecond);
    for (int i = 0; i < steps; ++i) {
        head.step(input);
    }
}

} // namespace

TEST_CASE("head_motion: a constant 0.3 g settles at the expected offset within 5 percent") {
    // En la zona lineal: x_obj = −ganancia × 0,3 y lo dibujado es L·tanh(x_obj / L).
    // Lateral: 4·tanh(0,75 / 4) = 0,741 cm; longitudinal: 3·tanh(0,6 / 3) = 0,592 cm; vertical:
    // 2·tanh(0,3 / 2) = 0,298 cm. A 0,3 g el límite suave resta solo un 1,2 %.
    const double a = 0.3 * kHeadGravity;
    HeadMotion head;
    run(head, {0.0, a, 0.0}, 5.0);
    CHECK(head.current().right == doctest::Approx(-0.007413).epsilon(0.05));
    CHECK(head.current().roll <
          0.0); // en una curva a la derecha, la cabeza se inclina a la izquierda
    head.reset();
    run(head, {-a, 0.0, 0.0}, 5.0); // frenando: hacia delante
    CHECK(head.current().forward == doctest::Approx(0.005921).epsilon(0.05));
    CHECK(head.current().pitch < 0.0);
    head.reset();
    run(head, {0.0, 0.0, a}, 5.0);
    CHECK(head.current().up == doctest::Approx(-0.002978).epsilon(0.05));
    MESSAGE("lateral ", head.current().right * 100.0, " cm");
}

TEST_CASE("head_motion: at constant velocity the head returns to rest") {
    HeadMotion head;
    run(head, {0.5 * kHeadGravity, -0.5 * kHeadGravity, 0.2 * kHeadGravity}, 1.0);
    CHECK(std::abs(head.current().forward) > 0.002);
    run(head, {}, 3.0);
    const HeadOffset& h = head.current();
    CHECK(std::abs(h.forward) < 1e-5);
    CHECK(std::abs(h.right) < 1e-5);
    CHECK(std::abs(h.up) < 1e-5);
    CHECK(std::abs(h.roll) < 1e-5);
    CHECK(std::abs(h.pitch) < 1e-5);
}

TEST_CASE("head_motion: random accelerations up to 10 g never pass the limits") {
    rally::sim::Pcg32 rng(7, 3);
    HeadMotion head;
    double max_right = 0.0;
    for (int i = 0; i < 100000;) {
        const auto r = [&rng] { return (rng.next_float01() * 2.0 - 1.0) * 10.0 * kHeadGravity; };
        // Cada valor dura entre 1 y 240 pasos.
        const HeadInput in{r(), r(), r()};
        const int hold = 1 + static_cast<int>(rng.bounded(240));
        for (int s = 0; s < hold && i < 100000; ++s, ++i) {
            head.step(in);
            const HeadOffset& h = head.current();
            REQUIRE(std::abs(h.forward) <= rally::platform::kHeadLimitForward);
            REQUIRE(std::abs(h.right) <= rally::platform::kHeadLimitRight);
            REQUIRE(std::abs(h.up) <= rally::platform::kHeadLimitUp);
            REQUIRE(std::abs(h.roll) <= rally::platform::kHeadLimitRoll);
            REQUIRE(std::abs(h.pitch) <= rally::platform::kHeadLimitPitch);
            max_right = std::max(max_right, std::abs(h.right));
        }
    }
    MESSAGE("lateral máximo con ±10 g: ", max_right * 100.0, " cm (límite 4)");
}

TEST_CASE("head_motion: a 50 g spike in one step moves the head less than 0.25 cm") {
    // El aterrizaje: el recorte a ±2 g deja el impulso en 2 g durante 1/240 s.
    HeadMotion head;
    head.step({0.0, 0.0, 50.0 * kHeadGravity});
    double peak = 0.0;
    for (int i = 0; i < kStepsPerSecond; ++i) {
        head.step({});
        peak = std::max(peak, std::abs(head.current().up));
    }
    MESSAGE("pico vertical tras 50 g en un paso: ", peak * 100.0, " cm");
    CHECK(peak < 0.0025);
}

TEST_CASE("head_motion: 30 and 60 fps give the same head at the same instants") {
    // Se integra por paso: a 30 fps hay 8 pasos por fotograma y a 60, 4. En los instantes
    // comunes, el estado y lo dibujado (alpha 1 al final del fotograma) son iguales bit a bit.
    rally::sim::Pcg32 rng(11, 5);
    HeadInput inputs[2400];
    for (HeadInput& in : inputs) {
        in = {(rng.next_float01() - 0.5) * 20.0, (rng.next_float01() - 0.5) * 20.0,
              (rng.next_float01() - 0.5) * 20.0};
    }
    HeadMotion at30;
    HeadMotion at60;
    int step30 = 0;
    int step60 = 0;
    for (int frame = 0; frame < 300; ++frame) { // 10 s a 30 fps
        for (int s = 0; s < 8; ++s) {
            at30.step(inputs[step30++]);
        }
        for (int half = 0; half < 2; ++half) {
            for (int s = 0; s < 4; ++s) {
                at60.step(inputs[step60++]);
            }
        }
        REQUIRE(at30.raw() == at60.raw());
        const HeadOffset a = at30.drawn(1.0);
        const HeadOffset b = at60.drawn(1.0);
        REQUIRE(a.forward == b.forward);
        REQUIRE(a.right == b.right);
        REQUIRE(a.up == b.up);
        REQUIRE(a.roll == b.roll);
        REQUIRE(a.pitch == b.pitch);
    }
}
