#include "platform/audio_output.h"

#include "platform/car_synth.h"

#include <raylib.h>

#include <algorithm>
#include <atomic>
#include <chrono>

namespace rally::platform {

namespace {

// Estado del callback: estático porque raylib no le pasa un puntero de usuario.
CarSynth g_synth;
TripleBuffer<AudioSnapshot> g_snapshots;
AudioStream g_stream{};

// Medidas (las escribe el callback, las lee el bucle).
std::atomic<std::int64_t> g_start_ns{0};
std::atomic<std::int64_t> g_calls{0};
std::atomic<std::int64_t> g_frames{0};
std::atomic<std::int64_t> g_busy_ns{0};
std::atomic<std::int64_t> g_max_call_ns{0};
std::atomic<std::int64_t> g_max_gap_ns{0};
std::atomic<std::int64_t> g_gaps{0};
std::atomic<std::int64_t> g_last_call_ns{0};

std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void store_max(std::atomic<std::int64_t>& target, std::int64_t value) {
    std::int64_t current = target.load(std::memory_order_relaxed);
    while (value > current &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
}

// Hilo de audio: sin reservas ni cerrojos.
void audio_callback(void* buffer, unsigned int frames) {
    const std::int64_t begin = now_ns();
    const std::int64_t last = g_last_call_ns.exchange(begin, std::memory_order_relaxed);
    if (last != 0) {
        const std::int64_t gap = begin - last;
        store_max(g_max_gap_ns, gap);
        if (static_cast<double>(gap) > kAudioGapFactor * kAudioDevicePeriodMs * 1e6) {
            g_gaps.fetch_add(1, std::memory_order_relaxed);
        }
    }
    g_synth.render(static_cast<float*>(buffer), static_cast<int>(frames), g_snapshots.read());
    const std::int64_t spent = now_ns() - begin;
    g_busy_ns.fetch_add(spent, std::memory_order_relaxed);
    store_max(g_max_call_ns, spent);
    g_calls.fetch_add(1, std::memory_order_relaxed);
    g_frames.fetch_add(frames, std::memory_order_relaxed);
}

} // namespace

AudioOutput::AudioOutput() {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        return;
    }
    g_synth.reset();
    g_snapshots.publish(AudioSnapshot{});
    g_stream = LoadAudioStream(kAudioSampleRate, 32, 1);
    if (!IsAudioStreamValid(g_stream)) {
        CloseAudioDevice();
        return;
    }
    reset_stats();
    SetAudioStreamCallback(g_stream, audio_callback);
    PlayAudioStream(g_stream);
    ready_ = true;
}

AudioOutput::~AudioOutput() {
    if (!ready_) {
        return;
    }
    StopAudioStream(g_stream);
    UnloadAudioStream(g_stream);
    CloseAudioDevice();
}

void AudioOutput::publish(const AudioSnapshot& snapshot) { g_snapshots.publish(snapshot); }

void AudioOutput::set_volume(float volume) { g_synth.set_volume(std::clamp(volume, 0.0f, 1.0f)); }

void AudioOutput::set_skid_model(SkidModel model) { g_synth.set_skid_model(model); }

SkidModel AudioOutput::skid_model() const { return g_synth.skid_model(); }

AudioStats AudioOutput::stats() const {
    AudioStats s;
    s.ready = ready_;
    s.calls = g_calls.load(std::memory_order_relaxed);
    s.frames = g_frames.load(std::memory_order_relaxed);
    const std::int64_t elapsed = now_ns() - g_start_ns.load(std::memory_order_relaxed);
    s.elapsed_s = static_cast<double>(elapsed) * 1e-9;
    s.cpu_fraction = elapsed > 0 ? static_cast<double>(g_busy_ns.load(std::memory_order_relaxed)) /
                                       static_cast<double>(elapsed)
                                 : 0.0;
    s.max_call_ms = static_cast<double>(g_max_call_ns.load(std::memory_order_relaxed)) * 1e-6;
    s.max_gap_ms = static_cast<double>(g_max_gap_ns.load(std::memory_order_relaxed)) * 1e-6;
    s.gaps = g_gaps.load(std::memory_order_relaxed);
    return s;
}

void AudioOutput::reset_stats() {
    g_start_ns.store(now_ns(), std::memory_order_relaxed);
    g_calls.store(0, std::memory_order_relaxed);
    g_frames.store(0, std::memory_order_relaxed);
    g_busy_ns.store(0, std::memory_order_relaxed);
    g_max_call_ns.store(0, std::memory_order_relaxed);
    g_max_gap_ns.store(0, std::memory_order_relaxed);
    g_gaps.store(0, std::memory_order_relaxed);
    g_last_call_ns.store(0, std::memory_order_relaxed);
}

} // namespace rally::platform
