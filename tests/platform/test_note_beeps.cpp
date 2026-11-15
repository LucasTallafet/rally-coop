// Pitidos de las notas (lote 7, tarea panel): un pitido por suceso, por su canal, sin sonido en la
// pausa y con los pitidos sonando en el modo a ciegas. Sin ventana ni dispositivo de audio.

#include "platform/audio_snapshot.h"
#include "platform/car_synth.h"
#include "platform/note_beeps.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <vector>

using namespace rally::platform;

namespace {

AudioSnapshot base_snapshot() {
    AudioSnapshot s;
    s.active = true;
    s.generation = 1;
    return s;
}

struct Energy {
    double left = 0.0;
    double right = 0.0;
};

Energy render_energy(NoteBeeps& beeps, const AudioSnapshot& s, int frames, float volume = 1.0f) {
    std::vector<float> out(static_cast<std::size_t>(2 * frames), 0.0f);
    beeps.render_add(out.data(), frames, s, volume);
    Energy e;
    for (int i = 0; i < frames; ++i) {
        e.left += static_cast<double>(out[static_cast<std::size_t>(2 * i)]) *
                  out[static_cast<std::size_t>(2 * i)];
        e.right += static_cast<double>(out[static_cast<std::size_t>(2 * i + 1)]) *
                   out[static_cast<std::size_t>(2 * i + 1)];
    }
    return e;
}

constexpr int kSecond = 48000;

} // namespace

TEST_CASE("note beeps: no event, no sound; a note beeps on its own channel only") {
    NoteBeeps beeps;
    AudioSnapshot s = base_snapshot();
    Energy e = render_energy(beeps, s, 4800);
    CHECK(e.left == 0.0);
    CHECK(e.right == 0.0);
    s.note_beeps_left = 1;
    e = render_energy(beeps, s, 4800);
    CHECK(e.left > 1.0);
    CHECK(e.right == 0.0);
    // El pitido dura unos 70 ms y no se repite sin un incremento nuevo.
    e = render_energy(beeps, s, kSecond);
    CHECK(e.left == 0.0);
    s.note_beeps_right = 1;
    e = render_energy(beeps, s, 4800);
    CHECK(e.right > 1.0);
    CHECK(e.left == 0.0);
}

TEST_CASE("note beeps: a hazard sounds on both channels with another pitch than a note") {
    NoteBeeps beeps;
    AudioSnapshot s = base_snapshot();
    render_energy(beeps, s, 480);
    s.note_hazard_beeps = 1;
    const Energy e = render_energy(beeps, s, 4800);
    CHECK(e.left > 1.0);
    CHECK(e.right > 1.0);
    CHECK(e.left == doctest::Approx(e.right));
    // El tono: cruces por cero en 50 ms (los de 660 Hz frente a los de 880 Hz).
    const auto crossings = [](const AudioSnapshot& snap, bool hazard) {
        NoteBeeps b;
        AudioSnapshot base = base_snapshot();
        std::vector<float> warm(960, 0.0f);
        b.render_add(warm.data(), 480, base, 1.0f);
        AudioSnapshot trig = snap;
        if (hazard) {
            trig.note_hazard_beeps = 1;
        } else {
            trig.note_beeps_left = 1;
        }
        std::vector<float> out(2 * 2400, 0.0f);
        b.render_add(out.data(), 2400, trig, 1.0f);
        int count = 0;
        for (int i = 1; i < 2400; ++i) {
            if ((out[static_cast<std::size_t>(2 * (i - 1))] < 0.0f) !=
                (out[static_cast<std::size_t>(2 * i)] < 0.0f)) {
                ++count;
            }
        }
        return count;
    };
    const int note = crossings(base_snapshot(), false);
    const int hazard = crossings(base_snapshot(), true);
    CHECK(note > hazard);
}

TEST_CASE("note beeps: the erase tone is another sound, longer, on both channels") {
    NoteBeeps beeps;
    AudioSnapshot s = base_snapshot();
    render_energy(beeps, s, 480);
    s.note_erase_beeps = 1;
    // Dura 140 ms: a los 62 ms suena y a los 200 ms ya no.
    const Energy early = render_energy(beeps, s, 3000);
    CHECK(early.left > 1.0);
    CHECK(early.right > 1.0);
    render_energy(beeps, s, 3720); // hasta los 140 ms
    const Energy late = render_energy(beeps, s, 4800);
    CHECK(late.left == 0.0);
}

TEST_CASE("note beeps: pause is silent, blind mode does not mute, a new world takes a base") {
    NoteBeeps beeps;
    AudioSnapshot s = base_snapshot();
    render_energy(beeps, s, 480);
    s.paused = true;
    s.note_beeps_left = 1;
    Energy e = render_energy(beeps, s, 4800);
    CHECK(e.left == 0.0);
    // El pitido de la pausa no se pierde: suena al reanudar.
    s.paused = false;
    e = render_energy(beeps, s, 4800);
    CHECK(e.left > 1.0);
    // A ciegas el sonido del coche cambia de perfil, pero los pitidos siguen.
    NoteBeeps blind;
    AudioSnapshot b = base_snapshot();
    b.blind = true;
    render_energy(blind, b, 480);
    b.note_beeps_right = 1;
    e = render_energy(blind, b, 4800);
    CHECK(e.right > 1.0);
    // Otra generación: los contadores son la base, sin sucesos.
    NoteBeeps fresh;
    AudioSnapshot w = base_snapshot();
    render_energy(fresh, w, 480);
    w.generation = 2;
    w.note_beeps_left = 7;
    e = render_energy(fresh, w, 4800);
    CHECK(e.left == 0.0);
    // Volumen cero: nada.
    NoteBeeps quiet;
    AudioSnapshot q = base_snapshot();
    render_energy(quiet, q, 480);
    q.note_beeps_left = 1;
    e = render_energy(quiet, q, 4800, 0.0f);
    CHECK(e.left == 0.0);
}

TEST_CASE("note beeps: cost of a callback with a beep") {
    NoteBeeps beeps;
    AudioSnapshot s = base_snapshot();
    render_energy(beeps, s, 480);
    using Clock = std::chrono::steady_clock;
    std::vector<float> out(2 * 480, 0.0f);
    double with_ns = 0.0;
    for (int i = 0; i < 200; ++i) {
        s.note_beeps_left = static_cast<std::uint32_t>(i + 1);
        const auto t0 = Clock::now();
        beeps.render_add(out.data(), 480, s, 1.0f);
        with_ns += std::chrono::duration<double, std::nano>(Clock::now() - t0).count();
    }
    MESSAGE("render_add con un pitido, 480 muestras (10 ms): ", with_ns / 200.0 / 1000.0, " us");
    CHECK(with_ns > 0.0);
}
