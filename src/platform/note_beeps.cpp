#include "platform/note_beeps.h"

#include "platform/car_synth.h"

#include <algorithm>
#include <cmath>

namespace rally::platform {

namespace {

constexpr float kTwoPi = 6.283185307179586476925f;

} // namespace

void NoteBeeps::reset() {
    started_ = false;
    generation_ = 0;
    left_ = right_ = hazard_ = erase_ = 0;
    voices_ = {};
}

void NoteBeeps::Voice::start(float start_hz, float end_hz, float seconds) {
    on = true;
    phase = 0.0f;
    hz = start_hz;
    pos = 0;
    length = std::max(1, static_cast<int>(seconds * kSynthSampleRate));
    hz_slide = (end_hz - start_hz) / static_cast<float>(length);
}

float NoteBeeps::Voice::step() {
    if (!on) {
        return 0.0f;
    }
    const float attack = kBeepAttackSeconds * kSynthSampleRate;
    const float release = kBeepReleaseSeconds * kSynthSampleRate;
    const float t = static_cast<float>(pos);
    float env = std::min(t / attack, 1.0f);
    env = std::min(env, static_cast<float>(length - pos) / release);
    const float sample = std::sin(kTwoPi * phase) * std::max(env, 0.0f);
    phase += hz / kSynthSampleRate;
    phase -= std::floor(phase);
    hz += hz_slide;
    if (++pos >= length) {
        on = false;
    }
    return sample;
}

void NoteBeeps::render_add(float* out, int frames, const AudioSnapshot& s, float volume) {
    if (!s.active) {
        started_ = false;
        voices_ = {};
        return;
    }
    if (s.paused && started_ && s.generation == generation_) {
        return; // en pausa no se consumen los contadores: suenan al reanudar
    }
    if (!started_ || s.generation != generation_) {
        generation_ = s.generation;
        left_ = s.note_beeps_left;
        right_ = s.note_beeps_right;
        hazard_ = s.note_hazard_beeps;
        erase_ = s.note_erase_beeps;
        voices_ = {};
        started_ = true;
    }
    if (s.note_beeps_left != left_) {
        left_ = s.note_beeps_left;
        voices_[0].start(kBeepNoteHz, kBeepNoteHz, kBeepNoteSeconds);
    }
    if (s.note_beeps_right != right_) {
        right_ = s.note_beeps_right;
        voices_[1].start(kBeepNoteHz, kBeepNoteHz, kBeepNoteSeconds);
    }
    if (s.note_hazard_beeps != hazard_) {
        hazard_ = s.note_hazard_beeps;
        voices_[0].start(kBeepHazardHz, kBeepHazardHz, kBeepHazardSeconds);
        voices_[1].start(kBeepHazardHz, kBeepHazardHz, kBeepHazardSeconds);
    }
    if (s.note_erase_beeps != erase_) {
        erase_ = s.note_erase_beeps;
        voices_[0].start(kBeepEraseStartHz, kBeepEraseEndHz, kBeepEraseSeconds);
        voices_[1].start(kBeepEraseStartHz, kBeepEraseEndHz, kBeepEraseSeconds);
    }
    const float gain = kBeepGain * std::clamp(volume, 0.0f, 1.0f) * (s.paused ? 0.0f : 1.0f);
    for (int i = 0; i < frames; ++i) {
        out[2 * i] += gain * voices_[0].step();
        out[2 * i + 1] += gain * voices_[1].step();
    }
}

} // namespace rally::platform
