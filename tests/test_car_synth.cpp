// Sonido sintético y su instantánea entre hilos (tarea 2.7).

#include "car_worlds.h"
#include "new_counter.h"
#include "platform/audio_snapshot.h"
#include "platform/car_feedback.h"
#include "platform/car_synth.h"
#include "sim/drivetrain.h"
#include "sim/surface.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

using rally::platform::AudioSnapshot;
using rally::platform::CarSynth;
using rally::platform::TripleBuffer;
using rally::sim::Surface;

namespace {

constexpr int kRate = 48000;
constexpr int kBlock = 512;

AudioSnapshot driving(Surface surface, bool blind, float speed) {
    AudioSnapshot s;
    s.active = true;
    s.blind = blind;
    s.cylinders = 4;
    s.idle_rpm = 1000.0f;
    s.limiter_rpm = 8200.0f;
    s.limiter_ramp_rpm = 100.0f;
    s.boost_max_bar = 1.2f;
    s.rpm = 5000.0f;
    s.engine_throttle = 0.8f;
    s.torque_load = 0.6f;
    s.throttle = 0.8f;
    s.boost_bar = 0.9f;
    s.speed = speed;
    for (std::size_t i = 0; i < s.wheels.size(); ++i) {
        s.wheels[i].contact = true;
        s.wheels[i].surface = static_cast<std::int8_t>(surface);
        s.wheels[i].rho = i < 2 ? 0.6f : 1.6f; // las traseras deslizan
        s.wheels[i].slip_speed = i < 2 ? 0.3f : 4.0f;
    }
    return s;
}

double rms(const std::vector<float>& x) {
    double sum = 0.0;
    for (const float v : x) {
        sum += static_cast<double>(v) * v;
    }
    return std::sqrt(sum / static_cast<double>(x.size()));
}

} // namespace

TEST_CASE("car_synth: 10 s of samples with changing snapshots allocate nothing") {
    auto synth = std::make_unique<CarSynth>();
    std::array<float, kBlock> block{};
    AudioSnapshot s = driving(Surface::Gravel, false, 20.0f);
    // Primero un bloque, por si algo se inicia la primera vez.
    synth->render(block.data(), kBlock, s);
    const std::uint64_t before = rally::tests::global_new_count();
    int sample = 0;
    double energy = 0.0;
    bool in_range = true;
    while (sample < 10 * kRate) {
        // La instantánea cambia cada 800 muestras (60 Hz), con sucesos de vez en cuando.
        const int frame = sample / 800;
        s.rpm = 1500.0f + 6800.0f * static_cast<float>(frame % 120) / 120.0f;
        s.engine_throttle = (frame / 30) % 2 == 0 ? 1.0f : 0.1f;
        s.throttle = s.engine_throttle;
        s.boost_bar = 1.2f * s.engine_throttle;
        s.speed = 5.0f + static_cast<float>(frame % 300) * 0.1f;
        s.shifts = static_cast<std::uint32_t>(frame / 90);
        s.blowoffs = static_cast<std::uint32_t>(frame / 60);
        s.blowoff_bar = 1.0f;
        s.impacts = static_cast<std::uint32_t>(frame / 200);
        s.impact_peak = 4.0f;
        s.wheels[0].surface = static_cast<std::int8_t>(frame % 5);
        s.paused = frame % 250 > 240;
        synth->render(block.data(), kBlock, s);
        for (const float v : block) {
            // Sin CHECK aquí: doctest reserva al informar (con -s) y lo contaría.
            in_range = in_range && std::isfinite(v) && std::abs(v) <= 1.0f;
            energy += static_cast<double>(v) * v;
        }
        sample += kBlock;
    }
    const std::uint64_t after = rally::tests::global_new_count();
    CHECK(after == before);
    CHECK(in_range);
    CHECK(energy > 0.0);
}

TEST_CASE("audio_snapshot: the reader never sees a half-written snapshot") {
    // Un productor publica 10⁶ instantáneas con su número de secuencia repetido en varios campos;
    // el consumidor lee sin parar y comprueba que todos coinciden y que la secuencia no retrocede.
    auto buffer = std::make_unique<TripleBuffer<AudioSnapshot>>();
    constexpr std::uint64_t kCount = 1'000'000;
    std::atomic<bool> done{false};
    std::uint64_t torn = 0;
    std::uint64_t backwards = 0;
    std::uint64_t reads = 0;
    std::uint64_t last_seen = 0;
    std::thread reader([&] {
        while (!done.load(std::memory_order_acquire)) {
            const AudioSnapshot& s = buffer->read();
            const std::uint64_t seq = s.sequence;
            const auto low = static_cast<std::uint32_t>(seq);
            if (s.shifts != low || s.blowoffs != low || s.impacts != low ||
                s.rpm != static_cast<float>(low % 100000) ||
                s.generation != static_cast<std::uint32_t>(seq >> 1)) {
                ++torn;
            }
            if (seq < last_seen) {
                ++backwards;
            }
            last_seen = seq;
            ++reads;
        }
    });
    for (std::uint64_t i = 1; i <= kCount; ++i) {
        AudioSnapshot s;
        s.sequence = i;
        const auto low = static_cast<std::uint32_t>(i);
        s.shifts = s.blowoffs = s.impacts = low;
        s.rpm = static_cast<float>(low % 100000);
        s.generation = static_cast<std::uint32_t>(i >> 1);
        buffer->publish(s);
    }
    done.store(true, std::memory_order_release);
    reader.join();
    // La última publicada es la que se lee al final.
    CHECK(buffer->read().sequence == kCount);
    MESSAGE(reads, " lecturas");
    CHECK(torn == 0);
    CHECK(backwards == 0);
    CHECK(reads > 0);
}

TEST_CASE("car_synth: in blind mode rolling, skid and stones sound the same on every surface") {
    const unsigned buses =
        rally::platform::kBusRolling | rally::platform::kBusSkid | rally::platform::kBusStones;
    constexpr int kSamples = 2 * kRate;
    std::vector<float> reference;
    std::vector<float> samples(kSamples);
    for (const Surface surface :
         {Surface::Asphalt, Surface::Gravel, Surface::Dirt, Surface::Snow, Surface::Sand}) {
        auto synth = std::make_unique<CarSynth>();
        const AudioSnapshot s = driving(surface, true, 22.0f);
        for (int i = 0; i < kSamples; i += kBlock) {
            synth->render(samples.data() + i, std::min(kBlock, kSamples - i), s, buses);
        }
        if (reference.empty()) {
            reference = samples;
            CHECK(rms(reference) > 1e-3);
        } else {
            CAPTURE(rally::sim::surface_name(surface));
            CHECK(samples == reference);
        }
    }
    // Con vista, sí cambian.
    auto gravel = std::make_unique<CarSynth>();
    auto asphalt = std::make_unique<CarSynth>();
    std::vector<float> g(kSamples);
    std::vector<float> a(kSamples);
    for (int i = 0; i < kSamples; i += kBlock) {
        const int n = std::min(kBlock, kSamples - i);
        gravel->render(g.data() + i, n, driving(Surface::Gravel, false, 22.0f), buses);
        asphalt->render(a.data() + i, n, driving(Surface::Asphalt, false, 22.0f), buses);
    }
    CHECK(g != a);
}

TEST_CASE("car_synth: at full throttle the engine sounds fuller with boost") {
    // Parada de la 2.7: sin silbido, la presión se oye en la carga del motor (0,3·acelerador
    // efectivo + 0,7·par / par pleno). A 4500 rpm y a fondo, con el par de la simulación (coche de
    // referencia) sin presión y con la presión máxima: el RMS del motor sube al menos 1,4 veces
    // (+3 dB). Misma semilla: un sintetizador nuevo para cada medida.
    const rally::sim::CarData& car = rally::tests::reference_car();
    const float omega = 4500.0f * 3.14159265f / 30.0f;
    const auto engine_rms = [&](float boost_pa) {
        AudioSnapshot s = driving(Surface::Asphalt, false, 0.0f);
        s.rpm = 4500.0f;
        s.engine_throttle = 1.0f;
        s.throttle = 1.0f;
        s.boost_bar = boost_pa * 1.0e-5f;
        s.torque_load = rally::platform::engine_torque_load(
            rally::sim::engine_torque(car, omega, 1.0f, boost_pa), omega, car);
        auto synth = std::make_unique<CarSynth>();
        std::vector<float> out(kRate);
        synth->render(out.data(), static_cast<int>(out.size()), s, rally::platform::kBusEngine);
        return std::pair{rms(out), s.torque_load};
    };
    const auto [without, load_without] = engine_rms(0.0f);
    const auto [with, load_with] = engine_rms(car.boost_max);
    MESSAGE("motor a 4500 rpm a fondo: carga del par ", load_without, " → ", load_with, "; RMS ",
            without, " → ", with, " (×", with / without, ")");
    CHECK(load_with == doctest::Approx(1.0).epsilon(1e-4));
    CHECK(with >= 1.4 * without);
}

TEST_CASE("car_synth: the engine follows the load and silences when paused") {
    auto synth = std::make_unique<CarSynth>();
    std::vector<float> out(kRate / 2);
    AudioSnapshot s = driving(Surface::Asphalt, false, 0.0f);
    s.engine_throttle = 1.0f;
    s.torque_load = 1.0f;
    synth->render(out.data(), static_cast<int>(out.size()), s, rally::platform::kBusEngine);
    const double loud = rms(out);
    s.engine_throttle = 0.0f;
    s.torque_load = 0.0f;
    synth->render(out.data(), static_cast<int>(out.size()), s, rally::platform::kBusEngine);
    const double quiet = rms(out);
    MESSAGE("motor a 5000 rpm: RMS con carga ", loud, ", sin carga ", quiet);
    CHECK(loud > 1.5 * quiet);
    s.paused = true;
    synth->render(out.data(), static_cast<int>(out.size()), s);
    CHECK(std::abs(out.back()) < 1e-6f);
    synth->set_volume(0.0f);
    s.paused = false;
    synth->render(out.data(), static_cast<int>(out.size()), s);
    CHECK(rms(out) == 0.0);
}
