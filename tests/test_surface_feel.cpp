// Vibración por superficie y deslizamiento y su envío al mando (tarea 2.7).

#include "car_worlds.h"
#include "platform/slip.h"
#include "platform/surface_feel.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>

using rally::platform::continuous_rumble;
using rally::platform::mean_roughness;
using rally::platform::Rumble;
using rally::platform::RumbleInput;
using rally::platform::RumbleMixer;
using rally::sim::Surface;

TEST_CASE("slip: rho is 1 at either peak and sqrt 2 at both") {
    const rally::sim::TireSurface& tire = rally::tests::reference_car().tire(Surface::Gravel);
    CHECK(rally::platform::normalized_slip(tire.slip_ratio_peak, 0.0f, tire) ==
          doctest::Approx(1.0).epsilon(1e-5));
    CHECK(rally::platform::normalized_slip(0.0f, tire.slip_angle_peak, tire) ==
          doctest::Approx(1.0).epsilon(1e-5));
    CHECK(rally::platform::normalized_slip(-tire.slip_ratio_peak, -tire.slip_angle_peak, tire) ==
          doctest::Approx(std::sqrt(2.0)).epsilon(1e-5));
    CHECK(rally::platform::normalized_slip(0.0f, 0.0f, tire) == 0.0f);
}

namespace {

RumbleInput on(Surface surface, bool blind, float speed, float suspension, float rho) {
    const auto s = static_cast<std::int8_t>(surface);
    RumbleInput in;
    in.speed = speed;
    in.roughness = mean_roughness({s, s, s, s}, blind);
    in.suspension_speed = suspension;
    in.rho_max = rho;
    return in;
}

} // namespace

TEST_CASE("surface_feel: rumble grows with speed and with slip above the peak") {
    float previous = -1.0f;
    for (const float v : {0.0f, 10.0f, 20.0f, 30.0f}) {
        const Rumble r = continuous_rumble(on(Surface::Gravel, false, v, 0.0f, 0.0f), false);
        CHECK(r.low > previous);
        previous = r.low;
    }
    CHECK(continuous_rumble(on(Surface::Gravel, false, 0.0f, 0.0f, 0.0f), false).low == 0.0f);
    previous = -1.0f;
    for (const float rho : {1.0f, 1.2f, 1.5f, 2.0f}) {
        const Rumble r = continuous_rumble(on(Surface::Asphalt, false, 20.0f, 0.0f, rho), false);
        CHECK(r.high > previous);
        previous = r.high;
    }
    // Por debajo del pico no hay motor agudo, y satura en ρ = 2.
    CHECK(continuous_rumble(on(Surface::Asphalt, false, 20.0f, 0.0f, 0.99f), false).high == 0.0f);
    CHECK(continuous_rumble(on(Surface::Asphalt, false, 20.0f, 0.0f, 3.0f), false).high ==
          continuous_rumble(on(Surface::Asphalt, false, 20.0f, 0.0f, 2.0f), false).high);
    // Los baches suman al grave.
    CHECK(continuous_rumble(on(Surface::Asphalt, false, 20.0f, 0.3f, 0.0f), false).low >
          continuous_rumble(on(Surface::Asphalt, false, 20.0f, 0.0f, 0.0f), false).low);
    // Con vista, las superficies vibran distinto.
    CHECK(continuous_rumble(on(Surface::Gravel, false, 20.0f, 0.0f, 0.0f), false).low >
          continuous_rumble(on(Surface::Asphalt, false, 20.0f, 0.0f, 0.0f), false).low);
}

TEST_CASE("surface_feel: in blind mode every surface rumbles the same, without the suspension") {
    for (const float v : {5.0f, 25.0f}) {
        for (const float rho : {0.5f, 1.4f}) {
            const Rumble ref = continuous_rumble(on(Surface::Asphalt, true, v, 0.0f, rho), true);
            for (const Surface s : {Surface::Gravel, Surface::Dirt, Surface::Snow, Surface::Sand}) {
                for (const float suspension : {0.0f, 0.2f, 0.8f}) {
                    const Rumble r = continuous_rumble(on(s, true, v, suspension, rho), true);
                    CHECK(r.low == ref.low);
                    CHECK(r.high == ref.high);
                }
            }
        }
    }
    // Una rueda en el aire no cuenta en la media.
    CHECK(mean_roughness({-1, -1, -1, -1}, false) == 0.0f);
    CHECK(mean_roughness({static_cast<std::int8_t>(Surface::Gravel), -1, -1, -1}, false) ==
          rally::platform::kSurfaceRoughness[1]);
}

TEST_CASE("surface_feel: the mixer sends on change or every 100 ms and the impact wins") {
    RumbleMixer mixer;
    constexpr std::int64_t ms = 1'000'000;
    Rumble c;
    c.low = 0.3f;
    c.high = 0.1f;
    auto sent = mixer.update(c, 0.0f, 0, false);
    REQUIRE(sent.has_value());
    CHECK(sent->low == 0.3f);
    CHECK(sent->duration_ms == rally::platform::kRumbleSendMs);
    // Menos de un 2 % de cambio antes de 100 ms: nada.
    c.low = 0.31f;
    CHECK_FALSE(mixer.update(c, 0.0f, 16 * ms, false).has_value());
    // A los 100 ms se vuelve a mandar.
    CHECK(mixer.update(c, 0.0f, 100 * ms, false).has_value());
    // Un impacto de 6 m/s manda el máximo por motor durante 150 ms.
    sent = mixer.update(c, 6.0f, 116 * ms, false);
    REQUIRE(sent.has_value());
    CHECK(sent->low == 1.0f);
    CHECK(sent->high == 0.5f);
    CHECK_FALSE(mixer.update(c, 0.0f, 200 * ms, false).has_value()); // sigue el impacto
    sent = mixer.update(c, 0.0f, 267 * ms, false);                   // ya pasó
    REQUIRE(sent.has_value());
    CHECK(sent->low == doctest::Approx(0.31f));
    // En silencio (pausa), un 0 una vez y nada más.
    sent = mixer.update(c, 0.0f, 283 * ms, true);
    REQUIRE(sent.has_value());
    CHECK(sent->low == 0.0f);
    CHECK(sent->high == 0.0f);
    CHECK_FALSE(mixer.update(c, 0.0f, 500 * ms, true).has_value());
}
