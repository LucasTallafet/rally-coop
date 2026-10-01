#include "platform/frame_pacer.h"

#include <SDL3/SDL_video.h>

#include <chrono>
#include <cmath>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// Después de <windows.h>, que define sus tipos (timeBeginPeriod, winmm).
#include <timeapi.h>

// Windows 10 1803 o posterior; con un SDK anterior la constante no existe.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace rally::platform {

namespace {

constexpr std::int64_t kFps30PeriodNs = 1'000'000'000 / 30;

// Comprobación del intervalo de presentación: se ignoran los primeros fotogramas tras
// el cambio y se mide la media de los siguientes.
constexpr int kIntervalWarmupFrames = 10;
constexpr int kIntervalMeasuredFrames = 60;
// Por debajo de esta fracción de la duración esperada, el controlador no respeta el intervalo.
constexpr double kIntervalMinRatio = 0.8;

std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Frecuencia del monitor donde está la ventana; 60 si no se conoce.
float query_refresh_rate() {
    SDL_Window* window = SDL_GL_GetCurrentWindow();
    const SDL_DisplayID display = window != nullptr ? SDL_GetDisplayForWindow(window) : 0;
    const SDL_DisplayMode* mode = display != 0 ? SDL_GetCurrentDisplayMode(display) : nullptr;
    if (mode == nullptr || mode->refresh_rate <= 0.0f) {
        return 60.0f;
    }
    return mode->refresh_rate;
}

} // namespace

FramePacer::FramePacer() {
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                    TIMER_ALL_ACCESS);
    high_resolution_timer_ = timer_ != nullptr;
    if (timer_ == nullptr) {
        // Windows 10 anterior a la 1803: temporizador normal con resolución de 1 ms.
        raised_timer_resolution_ = timeBeginPeriod(1) == TIMERR_NOERROR;
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    }
    refresh_rate_ = query_refresh_rate();
    apply();
}

FramePacer::~FramePacer() {
    if (timer_ != nullptr) {
        CloseHandle(timer_);
    }
    if (raised_timer_resolution_) {
        timeEndPeriod(1);
    }
}

std::int64_t FramePacer::begin_frame() {
    const float refresh_rate = query_refresh_rate();
    if (refresh_rate != refresh_rate_) {
        // Otro monitor u otro modo de pantalla: se vuelve a probar el intervalo.
        refresh_rate_ = refresh_rate;
        interval_ignored_ = false;
        apply();
    }

    if (use_timer_) {
        sleep_until(deadline_ns_);
    }
    const std::int64_t start = now_ns();
    if (use_timer_) {
        // Plazos fijos cada 1/30 s; si se ha perdido uno entero, se empieza de nuevo.
        deadline_ns_ = start - deadline_ns_ >= kFps30PeriodNs ? start + kFps30PeriodNs
                                                              : deadline_ns_ + kFps30PeriodNs;
    }

    const std::int64_t frame_ns = last_frame_start_ns_ < 0 ? 0 : start - last_frame_start_ns_;
    last_frame_start_ns_ = start;
    check_swap_interval(frame_ns);
    return frame_ns;
}

void FramePacer::set_target(FrameTarget target) {
    target_ = target;
    apply();
}

void FramePacer::set_vsync(bool vsync) {
    vsync_ = vsync;
    apply();
}

void FramePacer::apply() {
    int interval = vsync_ ? 1 : 0;
    bool timer = false;
    if (target_ == FrameTarget::Fps30) {
        const long multiple = std::lround(refresh_rate_ / 30.0f);
        const bool divisible =
            multiple >= 1 &&
            std::fabs(refresh_rate_ - 30.0f * static_cast<float>(multiple)) <= 0.5f;
        if (vsync_ && divisible && !interval_ignored_) {
            interval = static_cast<int>(multiple);
        } else {
            timer = true;
        }
    }

    if (!SDL_GL_SetSwapInterval(interval) && interval > 1) {
        interval_ignored_ = true;
        interval = 1;
        timer = true;
        SDL_GL_SetSwapInterval(interval);
    }

    swap_interval_ = interval;
    if (timer && !use_timer_) {
        deadline_ns_ = now_ns();
    }
    use_timer_ = timer;
    interval_checked_ = false;
    interval_frames_ = 0;
    interval_ns_ = 0;
}

void FramePacer::sleep_until(std::int64_t deadline_ns) {
    const std::int64_t remaining = deadline_ns - now_ns();
    if (remaining <= 0) {
        return;
    }
    if (timer_ != nullptr) {
        LARGE_INTEGER due;
        due.QuadPart = -(remaining / 100); // relativo, en unidades de 100 ns
        if (due.QuadPart == 0) {
            return;
        }
        if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(timer_, INFINITE);
            return;
        }
    }
    Sleep(static_cast<DWORD>(remaining / 1'000'000));
}

void FramePacer::check_swap_interval(std::int64_t frame_ns) {
    if (swap_interval_ <= 1 || use_timer_ || interval_checked_) {
        return;
    }
    ++interval_frames_;
    if (interval_frames_ <= kIntervalWarmupFrames) {
        return;
    }
    interval_ns_ += frame_ns;
    if (interval_frames_ < kIntervalWarmupFrames + kIntervalMeasuredFrames) {
        return;
    }
    const double expected_ns = 1e9 * swap_interval_ / refresh_rate_;
    const double measured_ns = static_cast<double>(interval_ns_) / kIntervalMeasuredFrames;
    if (measured_ns < kIntervalMinRatio * expected_ns) {
        interval_ignored_ = true;
        apply();
    } else {
        interval_checked_ = true; // no se vuelve a medir hasta el próximo cambio
    }
}

} // namespace rally::platform
