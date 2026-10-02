#include "platform/frame_pacer.h"

#include "platform/fixed_step_clock.h"

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
constexpr std::int64_t kFps30PeriodUnits = 8 * kStepUnits; // 1/30 s en unidades del reloj

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

struct RefreshRate {
    float hz = 60.0f;
    // Frecuencia exacta como fracción (60000/1001 en 59,94 Hz).
    int numerator = 60;
    int denominator = 1;
};

// Frecuencia del monitor donde está la ventana; 60 si no se conoce.
RefreshRate query_refresh_rate() {
    SDL_Window* window = SDL_GL_GetCurrentWindow();
    const SDL_DisplayID display = window != nullptr ? SDL_GetDisplayForWindow(window) : 0;
    const SDL_DisplayMode* mode = display != 0 ? SDL_GetCurrentDisplayMode(display) : nullptr;
    RefreshRate rate;
    if (mode == nullptr || mode->refresh_rate <= 0.0f) {
        return rate;
    }
    rate.hz = mode->refresh_rate;
    if (mode->refresh_rate_numerator > 0 && mode->refresh_rate_denominator > 0) {
        rate.numerator = mode->refresh_rate_numerator;
        rate.denominator = mode->refresh_rate_denominator;
    } else {
        // Sin fracción exacta: la frecuencia en milésimas de Hz.
        rate.numerator = static_cast<int>(std::lround(mode->refresh_rate * 1000.0f));
        rate.denominator = 1000;
    }
    return rate;
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
    const RefreshRate rate = query_refresh_rate();
    refresh_rate_ = rate.hz;
    refresh_period_ = refresh_period_units(rate.numerator, rate.denominator);
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
    const RefreshRate rate = query_refresh_rate();
    const std::int64_t refresh_period = refresh_period_units(rate.numerator, rate.denominator);
    if (rate.hz != refresh_rate_ || refresh_period != refresh_period_) {
        // Otro monitor u otro modo de pantalla: se vuelve a probar el intervalo.
        refresh_rate_ = rate.hz;
        refresh_period_ = refresh_period;
        interval_ignored_ = false;
        apply();
    }

    const std::int64_t before_wait = now_ns();
    if (use_timer_) {
        sleep_until(deadline_ns_);
    }
    const std::int64_t start = now_ns();
    last_wait_ns_ = start - before_wait;
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

std::int64_t FramePacer::frame_period() const {
    if (use_timer_) {
        return kFps30PeriodUnits;
    }
    return vsync_ ? swap_interval_ * refresh_period_ : 0;
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
