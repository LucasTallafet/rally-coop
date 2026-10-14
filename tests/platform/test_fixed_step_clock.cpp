#include "platform/fixed_step_clock.h"
#include "sim/pcg32.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>

using rally::platform::FixedStepClock;
using rally::platform::FrameSteps;
using rally::platform::kMaxStepsPerFrame;

namespace {

constexpr std::int64_t kNsPerSecond = 1'000'000'000;

// Pasos que corresponden a un tiempo total sin tope: floor(total × 240 / 1 s).
std::int64_t expected_steps(std::int64_t total_ns) { return total_ns * 240 / kNsPerSecond; }

void check_alpha(const FrameSteps& steps) {
    REQUIRE(steps.alpha >= 0.0);
    REQUIRE(steps.alpha < 1.0);
}

// Repite un ciclo de duraciones que suma un número exacto de pasos y comprueba que todos
// los fotogramas dan los mismos pasos.
template <std::size_t N>
void check_constant_rate(const std::array<std::int64_t, N>& cycle, int steps_per_frame) {
    FixedStepClock clock;
    for (int frame = 0; frame < 10'000; ++frame) {
        const FrameSteps steps = clock.advance(cycle[static_cast<std::size_t>(frame) % N]);
        REQUIRE(steps.count == steps_per_frame);
        check_alpha(steps);
    }
    CHECK(clock.steps_done() == 10'000 * steps_per_frame);
}

} // namespace

TEST_CASE("fixed step clock: 60 fps gives 4 steps per frame") {
    // 1/60 s no es un número entero de ns; tres fotogramas suman 1/20 s exacto. El largo va
    // primero para que ningún prefijo del ciclo quede por debajo de un número entero de pasos.
    check_constant_rate(std::array<std::int64_t, 3>{16'666'667, 16'666'667, 16'666'666}, 4);
}

TEST_CASE("fixed step clock: 30 fps gives 8 steps per frame") {
    check_constant_rate(std::array<std::int64_t, 3>{33'333'334, 33'333'333, 33'333'333}, 8);
}

TEST_CASE("fixed step clock: a frame of zero gives no steps") {
    FixedStepClock clock;
    const FrameSteps steps = clock.advance(0);
    CHECK(steps.count == 0);
    CHECK(steps.first == 0);
    CHECK(steps.alpha == 0.0);
}

TEST_CASE("fixed step clock: a step starts exactly at 1/240 s") {
    FixedStepClock clock;
    CHECK(clock.advance(4'166'666).count == 0); // 4 166 666 × 240 < 10^9
    CHECK(clock.advance(1).count == 1);         // 4 166 667 × 240 ≥ 10^9
}

TEST_CASE("fixed step clock: irregular frames give floor of total time over step") {
    FixedStepClock clock;
    rally::sim::Pcg32 rng(2026u, 10u);
    std::int64_t total_ns = 0;
    std::int64_t total_steps = 0;
    for (int frame = 0; frame < 10'000; ++frame) {
        // Entre 0 y 100 ms: nunca supera el tope de 24 pasos.
        const std::int64_t frame_ns = rng.bounded(100'000'001u);
        const FrameSteps steps = clock.advance(frame_ns);
        REQUIRE(steps.count <= kMaxStepsPerFrame);
        REQUIRE(steps.first == total_steps); // numeración sin huecos
        check_alpha(steps);

        total_ns += frame_ns;
        total_steps += steps.count;
        REQUIRE(total_steps == expected_steps(total_ns));
        // alpha es exactamente la fracción de paso pendiente.
        REQUIRE(steps.alpha == static_cast<double>(total_ns * 240 % kNsPerSecond) /
                                   static_cast<double>(kNsPerSecond));
    }
    CHECK(clock.steps_done() == total_steps);
}

TEST_CASE("fixed step clock: long frames are capped without catching up later") {
    FixedStepClock clock;
    // 1,001 s son 240,24 pasos: se dan 24 y se descartan los 216 enteros sobrantes.
    const FrameSteps capped = clock.advance(1'001'000'000);
    CHECK(capped.count == kMaxStepsPerFrame);
    CHECK(capped.alpha == doctest::Approx(0.24).epsilon(1e-12));

    // El fotograma siguiente vuelve a lo normal: 0,24 + 4 pasos.
    const FrameSteps next = clock.advance(16'666'667);
    CHECK(next.count == 4);
    CHECK(next.first == kMaxStepsPerFrame);
    check_alpha(next);
}

TEST_CASE("fixed step clock: frames just over the cap give exactly the cap") {
    FixedStepClock clock;
    CHECK(clock.advance(100'000'000).count == 24); // 100 ms: justo el tope
    CHECK(clock.advance(100'000'000).count == 24);
    CHECK(clock.advance(104'166'667).count == 24); // 25 pasos pedidos
    CHECK(clock.advance(16'666'667).count == 4);
}

TEST_CASE("fixed step clock: extreme and negative durations are safe") {
    FixedStepClock clock;
    const FrameSteps huge = clock.advance(std::numeric_limits<std::int64_t>::max());
    CHECK(huge.count == kMaxStepsPerFrame);
    check_alpha(huge);

    const FrameSteps negative = clock.advance(-5'000'000);
    CHECK(negative.count == 0);
    CHECK(negative.alpha == huge.alpha);
    CHECK(clock.steps_done() == kMaxStepsPerFrame);
}

// --- Ajuste a la sincronización vertical ---------------------------------------------

namespace {

using rally::platform::kMaxDebtUnits;
using rally::platform::kMaxRepayUnits;
using rally::platform::kStepUnits;
using rally::platform::refresh_period_units;

constexpr std::int64_t kPeriod60 = 4 * kStepUnits; // 1/60 s exacto en unidades del reloj

// Reloj con el ajuste activo y el fotograma inicial de 0 ns (el pacer da 0 la primera vez).
FixedStepClock snapping_clock(std::int64_t period = kPeriod60) {
    FixedStepClock clock;
    clock.set_snap_period(period);
    clock.advance(0);
    return clock;
}

// Instantes medidos de los refrescos de un monitor de numerator/denominator Hz, en ns, con
// un ruido de medida uniforme en [-jitter_ns, jitter_ns] sobre cada instante.
class VsyncTimeline {
public:
    VsyncTimeline(std::int64_t numerator, std::int64_t denominator, std::int64_t jitter_ns)
        : numerator_(numerator), denominator_(denominator), jitter_ns_(jitter_ns),
          previous_(measured(0)) {}

    // Duración medida del fotograma que acaba `refreshes` refrescos después del anterior.
    std::int64_t next_frame(std::int64_t refreshes = 1) {
        refresh_ += refreshes;
        const std::int64_t now = measured(refresh_);
        const std::int64_t frame_ns = now - previous_;
        previous_ = now;
        return frame_ns;
    }

private:
    std::int64_t measured(std::int64_t refresh) {
        const std::int64_t exact = refresh * kNsPerSecond * denominator_ / numerator_;
        const auto span = static_cast<std::uint32_t>(2 * jitter_ns_ + 1);
        return exact + static_cast<std::int64_t>(rng_.bounded(span)) - jitter_ns_;
    }

    std::int64_t numerator_;
    std::int64_t denominator_;
    std::int64_t jitter_ns_;
    rally::sim::Pcg32 rng_{2026u, 42u};
    std::int64_t refresh_ = 0;
    std::int64_t previous_;
};

// Cota de la deriva: tiempo real − deuda − tiempo de juego es constante desde el primer
// fotograma ajustado (la fase centrada fija el origen, a menos de medio paso) y la deuda
// nunca pasa del tope.
class DriftCheck {
public:
    void after(const FixedStepClock& clock, const FrameSteps& steps, std::int64_t frame_ns) {
        real_ += frame_ns * 240;
        REQUIRE(clock.debt() <= kMaxDebtUnits);
        REQUIRE(clock.debt() >= -kMaxDebtUnits);
        const std::int64_t game =
            clock.steps_done() * kStepUnits +
            static_cast<std::int64_t>(steps.alpha * static_cast<double>(kStepUnits) + 0.5);
        const std::int64_t offset = real_ - clock.debt() - game;
        if (!started_) {
            started_ = steps.snapped;
            offset_ = offset;
            REQUIRE(offset <= kStepUnits / 2);
            REQUIRE(offset >= -kStepUnits / 2);
            return;
        }
        REQUIRE(offset == offset_);
    }

private:
    std::int64_t real_ = 0;
    std::int64_t offset_ = 0;
    bool started_ = false;
};

} // namespace

TEST_CASE("fixed step clock snap: refresh period in clock units") {
    CHECK(refresh_period_units(60, 1) == 4 * kStepUnits);
    CHECK(refresh_period_units(60000, 1001) == 4'004'000'000);
    CHECK(refresh_period_units(144, 1) == 1'666'666'667);
    CHECK(refresh_period_units(0, 1) == 0);
    CHECK(refresh_period_units(60, 0) == 0);
}

TEST_CASE("fixed step clock snap: the first snapped frame centres the phase") {
    FixedStepClock clock = snapping_clock();
    const FrameSteps steps = clock.advance(16'666'667);
    CHECK(steps.snapped);
    CHECK(steps.count == 4);
    CHECK(steps.alpha == 0.5);
    // 1/3 ns medido de más (80 unidades), menos 80/16 devueltas.
    CHECK(clock.debt() == 75);
}

TEST_CASE("fixed step clock snap: 60 Hz with 0.8 ms of noise always gives 4 steps") {
    // Ruido de ±0,4 ms en cada instante medido: los dt van de 15,87 a 17,47 ms.
    FixedStepClock clock = snapping_clock();
    VsyncTimeline vsync(60, 1, 400'000);
    DriftCheck drift;
    for (int frame = 0; frame < 20'000; ++frame) {
        const std::int64_t frame_ns = vsync.next_frame();
        const FrameSteps steps = clock.advance(frame_ns);
        REQUIRE(steps.snapped);
        REQUIRE(steps.count == 4);
        REQUIRE(steps.alpha > 0.4);
        REQUIRE(steps.alpha < 0.6);
        drift.after(clock, steps, frame_ns);
    }
}

TEST_CASE("fixed step clock snap: 60 Hz with 1.5 ms of noise always gives 4 steps") {
    // Ruido de ±0,75 ms en cada instante medido: los dt van de 15,17 a 18,17 ms.
    FixedStepClock clock = snapping_clock();
    VsyncTimeline vsync(60, 1, 750'000);
    DriftCheck drift;
    for (int frame = 0; frame < 20'000; ++frame) {
        const std::int64_t frame_ns = vsync.next_frame();
        const FrameSteps steps = clock.advance(frame_ns);
        REQUIRE(steps.snapped);
        REQUIRE(steps.count == 4);
        REQUIRE(steps.alpha > 0.2);
        REQUIRE(steps.alpha < 0.8);
        drift.after(clock, steps, frame_ns);
    }
}

TEST_CASE("fixed step clock snap: replay of frames measured on an RX 580 at 60 Hz") {
    // dt reales de la demo en ventana a 60 Hz (fase 1, tarea 4b; 30 s, fotogramas_000.csv).
    // Ruido de hasta ±2,3 ms en parejas (+x, −x), un fotograma perdido y un monitor que va a
    // 60,015 Hz. Con la ventana de 1 ms y volviendo a centrar tras cada fotograma sin ajustar
    // salían 295 sin ajustar y 16 de 3 o 5 pasos; con 2 ms y volviendo a centrar, 39; con
    // 2 ms centrando una vez, 17 sin ajustar y 19.
    std::ifstream file(RALLY_TEST_DATA_DIR "/frames_rx580_60hz.txt");
    REQUIRE(file.good());
    FixedStepClock clock;
    clock.set_snap_period(kPeriod60);
    DriftCheck drift;
    std::string line;
    int frame = 0;
    int unsnapped = 0;
    int noise = 0;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::int64_t frame_ns = std::stoll(line);
        const FrameSteps steps = clock.advance(frame_ns);
        if (frame_ns > 0) {
            drift.after(clock, steps, frame_ns);
        }
        // Como el contador en pantalla: sin el primer segundo.
        if (frame++ >= 60) {
            unsnapped += steps.snapped ? 0 : 1;
            const bool lost = 2 * frame_ns * 240 >= 3 * kPeriod60;
            noise += steps.count != 4 && !lost ? 1 : 0;
        }
    }
    CHECK(frame == 1'786);
    // Con la ventana de 2,5 ms se ajustan todos (el mayor desvío es de 2,36 ms). Los 11
    // fotogramas de 3 o 5 pasos son los cruces del borde por la deriva del monitor (unos 1,7
    // en 29 s, con vaivén), y avanzan el juego igual de suave.
    CHECK(unsnapped == 0);
    CHECK(noise == 11);
}

TEST_CASE("fixed step clock snap: without snapping the same noise gives 3 and 5 steps") {
    // Contraste: el mismo ruido sin ajuste, con alpha cerca del borde entre pasos.
    FixedStepClock clock;
    clock.advance(0);
    VsyncTimeline vsync(60, 1, 400'000);
    int off_steps = 0;
    for (int frame = 0; frame < 20'000; ++frame) {
        off_steps += clock.advance(vsync.next_frame()).count != 4 ? 1 : 0;
    }
    CHECK(off_steps > 1'000);
}

TEST_CASE("fixed step clock snap: lost frames of 33 ms give 8 steps") {
    FixedStepClock clock = snapping_clock();
    VsyncTimeline vsync(60, 1, 400'000);
    DriftCheck drift;
    for (int frame = 0; frame < 20'000; ++frame) {
        const bool lost = frame % 7 == 3 || frame % 11 == 5;
        const std::int64_t frame_ns = vsync.next_frame(lost ? 2 : 1);
        const FrameSteps steps = clock.advance(frame_ns);
        REQUIRE(steps.snapped);
        REQUIRE(steps.count == (lost ? 8 : 4));
        drift.after(clock, steps, frame_ns);
    }
}

TEST_CASE("fixed step clock snap: 30 fps by swap interval gives 8 steps") {
    FixedStepClock clock = snapping_clock();
    VsyncTimeline vsync(60, 1, 400'000);
    DriftCheck drift;
    for (int frame = 0; frame < 10'000; ++frame) {
        const std::int64_t frame_ns = vsync.next_frame(2);
        const FrameSteps steps = clock.advance(frame_ns);
        REQUIRE(steps.count == 8);
        drift.after(clock, steps, frame_ns);
    }
}

TEST_CASE("fixed step clock snap: a 59.94 Hz monitor reported as 60 Hz keeps bounded drift") {
    // El monitor va un 0,1 % más lento de lo que dice: la deuda crece 16,7 µs por fotograma
    // y se devuelve. alpha deriva despacio y cada 250 fotogramas, al cruzar el borde entre
    // pasos, sale un paso de más: un fotograma de 5 o, con el vaivén de la devolución, algún
    // par de 5 y 3. El tiempo de juego avanza lo mismo; solo cambia en qué fotograma cae el paso.
    FixedStepClock clock = snapping_clock();
    VsyncTimeline vsync(60000, 1001, 400'000);
    DriftCheck drift;
    std::int64_t extra = 0;
    int off_steps = 0;
    for (int frame = 0; frame < 25'000; ++frame) {
        const std::int64_t frame_ns = vsync.next_frame();
        const FrameSteps steps = clock.advance(frame_ns);
        REQUIRE(steps.snapped);
        REQUIRE(steps.count >= 3);
        REQUIRE(steps.count <= 5);
        extra += steps.count - 4;
        off_steps += steps.count != 4 ? 1 : 0;
        if (frame > 1'000) {
            // Equilibrio de ~0,27 ms más el ruido de ±0,8 ms entre dos instantes.
            REQUIRE(clock.debt() < 1'200'000 * 240);
        }
        drift.after(clock, steps, frame_ns);
    }
    // 25 000 fotogramas × 16,7 µs = 0,417 s = 100 pasos.
    CHECK(extra >= 99);
    CHECK(extra <= 101);
    CHECK(off_steps <= 120); // casi siempre un solo fotograma de 5 por cruce (106 con esta semilla)
}

TEST_CASE("fixed step clock snap: independent noise of 0.8 ms per frame keeps bounded drift") {
    // Ruido independiente en cada dt (no en el instante): el tiempo real hace un paseo
    // aleatorio y el juego tiene que seguirlo, así que hay algún fotograma de 3 o 5 pasos.
    FixedStepClock clock = snapping_clock();
    rally::sim::Pcg32 rng(7u, 3u);
    DriftCheck drift;
    for (int frame = 0; frame < 20'000; ++frame) {
        const std::int64_t frame_ns =
            16'666'667 + static_cast<std::int64_t>(rng.bounded(1'600'001u)) - 800'000;
        const FrameSteps steps = clock.advance(frame_ns);
        REQUIRE(steps.snapped);
        REQUIRE(steps.count >= 3);
        REQUIRE(steps.count <= 5);
        drift.after(clock, steps, frame_ns);
    }
}

TEST_CASE("fixed step clock snap: debt over 4 ms is paid at once") {
    // Cada fotograma mide 0,9 ms más que el periodo: dentro de la ventana, pero la deuda
    // crece 0,8 ms por fotograma hasta pasar del tope.
    FixedStepClock clock = snapping_clock();
    DriftCheck drift;
    bool paid = false;
    for (int frame = 0; frame < 100; ++frame) {
        const std::int64_t debt_before = clock.debt();
        const FrameSteps steps = clock.advance(17'566'667);
        REQUIRE(steps.snapped);
        paid = paid || (debt_before > 0 && clock.debt() == 0);
        drift.after(clock, steps, 17'566'667);
    }
    CHECK(paid);
}

TEST_CASE("fixed step clock snap: frames outside the window are not snapped") {
    FixedStepClock clock = snapping_clock();
    CHECK(clock.advance(16'666'667).snapped);
    const FrameSteps odd = clock.advance(25'000'000); // a 8,3 ms de 1 y de 2 periodos
    CHECK_FALSE(odd.snapped);
    CHECK(odd.count == 6);
    CHECK(clock.advance(14'100'000).snapped == false); // a 2,57 ms del periodo
    CHECK(clock.advance(14'200'000).snapped);          // a 2,47 ms
    CHECK(clock.advance(19'100'000).snapped);          // a 2,43 ms
    CHECK(clock.advance(19'200'000).snapped == false); // a 2,53 ms
}

TEST_CASE("fixed step clock snap: the window is the smaller of 2.5 ms and 40 % of the period") {
    using rally::platform::snap_window_units;
    struct Rate {
        std::int64_t hz;
        std::int64_t window_ns; // ventana esperada
    };
    // 60 Hz: 40 % son 6,67 ms; 144 Hz: 2,78 ms; 240 Hz: 1,67 ms (2,5 ms pasaría de medio
    // periodo, 2,08 ms).
    for (const Rate rate : {Rate{60, 2'500'000}, Rate{144, 2'500'000}, Rate{240, 1'666'666}}) {
        CAPTURE(rate.hz);
        const std::int64_t period = refresh_period_units(rate.hz, 1);
        const std::int64_t window = snap_window_units(period);
        CHECK(window / 240 == rate.window_ns);
        CHECK(2 * window < period); // menos de medio periodo: múltiplo sin ambigüedad

        const std::int64_t period_ns = kNsPerSecond / rate.hz;
        for (const std::int64_t multiple : {1, 2, 3}) {
            for (const std::int64_t sign : {-1, 1}) {
                FixedStepClock clock = snapping_clock(period);
                const std::int64_t centre = multiple * period_ns;
                const FrameSteps inside = clock.advance(centre + sign * (rate.window_ns - 10'000));
                CHECK(inside.snapped);
                // k periodos exactos más la devolución de deuda del fotograma.
                CHECK(inside.used - multiple * period <= kMaxRepayUnits);
                CHECK(inside.used - multiple * period >= -kMaxRepayUnits);
                const FrameSteps outside = clock.advance(centre + sign * (rate.window_ns + 10'000));
                CHECK_FALSE(outside.snapped);
            }
        }
    }
}

TEST_CASE("fixed step clock snap: a frame outside the window does not centre the phase again") {
    FixedStepClock clock = snapping_clock();
    REQUIRE(clock.advance(16'666'667).alpha == 0.5);
    // Un instante que llega 2,6 ms tarde: 4,62 pasos sin ajustar, cruza el borde.
    const FrameSteps late = clock.advance(19'266'667);
    CHECK_FALSE(late.snapped);
    CHECK(late.count == 5);
    // El siguiente, 1,85 ms más corto, se ajusta sin volver a centrar: la deuda lo compensa
    // poco a poco en vez de mover el origen sobre un instante tardío.
    const FrameSteps next = clock.advance(14'816'667);
    CHECK(next.snapped);
    CHECK(next.alpha < 0.15);
}

TEST_CASE("fixed step clock snap: disabling it repays the remaining debt") {
    FixedStepClock clock = snapping_clock();
    for (int frame = 0; frame < 3; ++frame) {
        clock.advance(17'566'667); // deja deuda
    }
    REQUIRE(clock.debt() > 0);
    clock.set_snap_period(0);
    for (int frame = 0; frame < 1'000; ++frame) {
        CHECK_FALSE(clock.advance(16'666'667).snapped);
    }
    CHECK(clock.debt() < 16); // lo que queda por debajo de deuda/16 entera
}

TEST_CASE("fixed step clock snap: the step cap discards the debt") {
    FixedStepClock clock = snapping_clock();
    clock.advance(17'566'667);
    REQUIRE(clock.debt() > 0);
    CHECK(clock.advance(1'000'000'000).count == kMaxStepsPerFrame);
    CHECK(clock.debt() == 0);
}
