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
using rally::platform::SkidModel;
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
    for (const SkidModel model : {SkidModel::New, SkidModel::Legacy27}) {
        CAPTURE(static_cast<int>(model));
        auto synth = std::make_unique<CarSynth>();
        synth->set_skid_model(model);
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
    std::atomic<bool> reader_running{false};
    std::thread reader([&] {
        reader_running.store(true, std::memory_order_release);
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
    // El productor espera a que el lector esté en marcha: si no, en un PC rápido publica las 10⁶
    // antes de que arranque el hilo y no se lee nada.
    while (!reader_running.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
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
    std::vector<float> samples(kSamples);
    // Con los dos modelos del sonido del deslizamiento (en la 2.7 las ruedas tienen rho 1,6).
    for (const SkidModel model : {SkidModel::New, SkidModel::Legacy27}) {
        std::vector<float> reference;
        for (const Surface surface :
             {Surface::Asphalt, Surface::Gravel, Surface::Dirt, Surface::Snow, Surface::Sand}) {
            auto synth = std::make_unique<CarSynth>();
            synth->set_skid_model(model);
            const AudioSnapshot s = driving(surface, true, 22.0f);
            for (int i = 0; i < kSamples; i += kBlock) {
                synth->render(samples.data() + i, std::min(kBlock, kSamples - i), s, buses);
            }
            if (reference.empty()) {
                reference = samples;
                CHECK(rms(reference) > 1e-3);
            } else {
                CAPTURE(rally::sim::surface_name(surface));
                CAPTURE(static_cast<int>(model));
                CHECK(samples == reference);
            }
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

namespace {

// Cuatro ruedas del mismo estado: contacto, superficie, rho y velocidad de deslizamiento.
AudioSnapshot locked(Surface surface, bool blind, float rho, float slip, float speed) {
    AudioSnapshot s = driving(surface, blind, speed);
    for (auto& w : s.wheels) {
        w.rho = rho;
        w.slip_speed = slip;
    }
    return s;
}

// Nivel eficaz de una capa con una instantánea fija: 1 s de un sintetizador nuevo.
double bus_rms(const AudioSnapshot& s, unsigned bus, SkidModel model) {
    auto synth = std::make_unique<CarSynth>();
    synth->set_skid_model(model);
    std::vector<float> out(kRate);
    synth->render(out.data(), static_cast<int>(out.size()), s, bus);
    return rms(out);
}

// Amplitud aproximada a una frecuencia (Goertzel).
double tone_amplitude(const std::vector<float>& x, double hz) {
    const double w = 2.0 * 3.14159265358979 * hz / kRate;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0;
    double s2 = 0.0;
    for (const float v : x) {
        const double s0 = v + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return 2.0 * std::sqrt(std::max(power, 0.0)) / static_cast<double>(x.size());
}

} // namespace

TEST_CASE("car_synth: skid level per wheel with both models") {
    using rally::platform::skid_level;
    struct Row {
        float rho;
        float slip;
        float now;
        float legacy;
    };
    constexpr Row kRows[] = {
        {10.0f, 34.0f, 1.0f, 1.0f},  {10.0f, 14.0f, 0.837f, 1.0f}, {10.0f, 5.5f, 0.524f, 1.0f},
        {10.0f, 3.0f, 0.387f, 1.0f}, {1.5f, 34.0f, 0.5f, 0.5f},    {0.9f, 30.0f, 0.0f, 0.0f},
        {10.0f, -1.0f, 0.0f, 0.0f}, // v_desl negativa: se recorta a 0, sin NaN
    };
    for (const Row& r : kRows) {
        CAPTURE(r.rho);
        CAPTURE(r.slip);
        CHECK(skid_level(r.rho, r.slip, SkidModel::New) == doctest::Approx(r.now).epsilon(1e-3));
        CHECK(skid_level(r.rho, r.slip, SkidModel::Legacy27) ==
              doctest::Approx(r.legacy).epsilon(1e-3));
    }
}

TEST_CASE("car_synth: on asphalt the tone gives way to noise as the wheel locks") {
    using rally::platform::asphalt_tone_share;
    struct Row {
        float rho;
        float share;
    };
    constexpr Row kRows[] = {{0.5f, 1.0f}, {1.0f, 1.0f}, {1.5f, 0.75f},
                             {2.0f, 0.5f}, {3.0f, 0.0f}, {10.0f, 0.0f}};
    for (const Row& r : kRows) {
        CAPTURE(r.rho);
        CHECK(asphalt_tone_share(r.rho, SkidModel::New) == doctest::Approx(r.share).epsilon(1e-6));
        CHECK(asphalt_tone_share(r.rho, SkidModel::Legacy27) == 1.0f);
    }
}

TEST_CASE("car_synth: a locked wheel on asphalt no longer drowns the engine") {
    // Diagnóstico del lote 1 (lote-3.md §6): el pitido bloqueado sonaba ~3 veces más fuerte que el
    // motor. Canales por separado: 4 ruedas bloqueadas a 30 m/s frente al motor solo a 1000 rpm
    // y carga 0.
    AudioSnapshot engine = driving(Surface::Asphalt, false, 30.0f);
    engine.rpm = 1000.0f;
    engine.engine_throttle = 0.0f;
    engine.throttle = 0.0f;
    engine.torque_load = 0.0f;
    const double engine_level = bus_rms(engine, rally::platform::kBusEngine, SkidModel::New);
    const AudioSnapshot wheels = locked(Surface::Asphalt, false, 10.0f, 30.0f, 30.0f);
    const double now = bus_rms(wheels, rally::platform::kBusSkid, SkidModel::New);
    const double legacy = bus_rms(wheels, rally::platform::kBusSkid, SkidModel::Legacy27);
    MESSAGE("motor a 1000 rpm: ", engine_level, "; rueda bloqueada: nuevo ", now, " (x",
            now / engine_level, "), 2.7 ", legacy, " (x", legacy / engine_level, ")");
    CHECK(now <= 1.5 * engine_level);
    CHECK(legacy >= 2.5 * engine_level);
}

TEST_CASE("car_synth: on dirt the slide gets quieter with the slip speed (new model only)") {
    const auto ratio = [](SkidModel model) {
        const double fast = bus_rms(locked(Surface::Dirt, false, 10.0f, 28.0f, 28.0f),
                                    rally::platform::kBusSkid, model);
        const double slow = bus_rms(locked(Surface::Dirt, false, 10.0f, 3.0f, 28.0f),
                                    rally::platform::kBusSkid, model);
        MESSAGE("v_desl 28 / 3 m/s: ", fast, " / ", slow, " = ", fast / slow);
        return fast / slow;
    };
    CHECK(ratio(SkidModel::New) >= 2.4);
    CHECK(ratio(SkidModel::Legacy27) <= 1.05);
}

TEST_CASE("car_synth: in blind mode the skid has no tone with either model") {
    for (const SkidModel model : {SkidModel::New, SkidModel::Legacy27}) {
        auto blind_synth = std::make_unique<CarSynth>();
        blind_synth->set_skid_model(model);
        std::vector<float> blind(kRate);
        blind_synth->render(blind.data(), kRate, locked(Surface::Asphalt, true, 1.5f, 30.0f, 30.0f),
                            rally::platform::kBusSkid);
        auto seeing = std::make_unique<CarSynth>();
        seeing->set_skid_model(model);
        std::vector<float> sighted(kRate);
        seeing->render(sighted.data(), kRate, locked(Surface::Asphalt, false, 1.5f, 30.0f, 30.0f),
                       rally::platform::kBusSkid);
        // Con vista y cerca del pico hay tono a ~850 Hz (+-40 Hz de vibrato); a ciegas, no.
        double tone_blind = 0.0;
        double tone_sighted = 0.0;
        for (const double hz : {820.0, 850.0, 880.0}) {
            tone_blind = std::max(tone_blind, tone_amplitude(blind, hz));
            tone_sighted = std::max(tone_sighted, tone_amplitude(sighted, hz));
        }
        MESSAGE("modelo ", static_cast<int>(model), ": tono a ciegas ", tone_blind, ", con vista ",
                tone_sighted);
        CHECK(tone_sighted > 3.0 * tone_blind);
        CHECK(rms(blind) > 1e-3);
    }
}

TEST_CASE("car_synth: the skid model starts as new and reset does not touch it") {
    CarSynth synth;
    CHECK(synth.skid_model() == SkidModel::New);
    synth.set_skid_model(SkidModel::Legacy27);
    synth.reset();
    CHECK(synth.skid_model() == SkidModel::Legacy27);
}
