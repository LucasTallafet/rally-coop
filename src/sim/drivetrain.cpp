#include "sim/drivetrain.h"

#include "sim/rmath.h"

namespace rally::sim {

namespace {

constexpr float kRpm = 3.14159265358979323846f / 30.0f; // rad/s por rpm

// Banda del regulador de ralentí: de θ₀ a θ₀ + 1 en 100 rpm por debajo del ralentí.
constexpr float kIdleBand = 100.0f * kRpm;
// Rampa mínima del embrague en arranque con el acelerador a 0 (rpm sobre engage_rpm).
constexpr float kLaunchMinRamp = 100.0f * kRpm;
// Golpe de gas del cambio: acelerador 1 a 300 rpm por debajo del objetivo.
constexpr float kBlipBand = 300.0f * kRpm;
// La espera en derrape del automático se corta 100 rpm antes del corte del limitador.
constexpr float kHoldLimitMargin = 100.0f * kRpm;

// Tolerancias del conjunto activo: evitan que el redondeo haga oscilar una fila entre pegada y
// deslizando (relativa a la caja y absoluta en rad/s).
constexpr float kCapTolerance = 1.0e-5f;
constexpr float kVelocityTolerance = 1.0e-5f;
// Regularización relativa de la diagonal de las filas de Coulomb: filas dependientes (las dos
// ruedas de un eje bloqueadas y su autoblocante pegado) dejan A singular.
constexpr float kRegularization = 1.0e-6f;

float lerp(float a, float b, float t) { return a + (b - a) * t; }

float smoothstep(float edge0, float edge1, float x) {
    if (!(edge1 > edge0)) {
        return x >= edge1 ? 1.0f : 0.0f;
    }
    const float s = rmath::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return s * s * (3.0f - 2.0f * s);
}

// rpm de la caja en `gear` con el giro `drive_speed` a la entrada de los diferenciales.
float gear_speed(const CarData& car, int gear, float drive_speed) {
    return gear_ratio(car, gear) * drive_speed;
}

void request_shift(const CarData& car, int gear, DrivetrainState& state) {
    state.target_gear = static_cast<std::int8_t>(gear);
    state.shift_steps = static_cast<std::int16_t>(car.shift_steps);
    state.drift_steps = 0;
    if (state.shift_steps == 0) {
        state.gear = state.target_gear;
    }
}

// La marcha `gear` engranada a `drive_speed` no pasa del limitador.
bool fits(const CarData& car, int gear, float drive_speed) {
    return gear_speed(car, gear, drive_speed) <= car.limiter_speed;
}

} // namespace

void drivetrain_reset(const CarData& car, DrivetrainState& state) {
    state = DrivetrainState{};
    state.engine_omega = car.idle_speed;
    state.gear = 1;
    state.target_gear = 1;
}

float gear_ratio(const CarData& car, int gear) {
    switch (gear) {
    case kReverseGear:
        return -car.reverse_ratio;
    case 1:
        return car.gear_ratio_1;
    case 2:
        return car.gear_ratio_2;
    case 3:
        return car.gear_ratio_3;
    case 4:
        return car.gear_ratio_4;
    case 5:
        return car.gear_ratio_5;
    default:
        return 0.0f;
    }
}

float engine_curve(const CarData& car, float omega) {
    const int n = car.torque_points;
    if (omega <= car.torque_speed[0]) {
        return car.torque_atm[0];
    }
    for (int i = 1; i < n; ++i) {
        const auto k = static_cast<std::size_t>(i);
        if (omega <= car.torque_speed[k]) {
            const float t =
                (omega - car.torque_speed[k - 1]) / (car.torque_speed[k] - car.torque_speed[k - 1]);
            return lerp(car.torque_atm[k - 1], car.torque_atm[k], t);
        }
    }
    return car.torque_atm[static_cast<std::size_t>(n - 1)];
}

float engine_full_torque(const CarData& car, float omega, float boost) {
    return car.torque_scale * engine_curve(car, omega) * (1.0f + car.boost_gain * boost);
}

float limiter_factor(const CarData& car, float omega) {
    return rmath::clamp((car.limiter_speed - omega) / car.limiter_ramp, 0.0f, 1.0f);
}

float idle_throttle(const CarData& car, float omega) {
    return rmath::clamp(car.idle_throttle + (car.idle_speed - omega) / kIdleBand, 0.0f, 1.0f);
}

float engine_torque(const CarData& car, float omega, float throttle, float boost) {
    return throttle * engine_full_torque(car, omega, boost) * limiter_factor(car, omega) -
           (1.0f - throttle) * car.engine_friction * omega;
}

float boost_target(const CarData& car, float omega, float throttle) {
    const float ramp = rmath::clamp((omega - car.boost_threshold_speed) /
                                        (car.boost_full_speed - car.boost_threshold_speed),
                                    0.0f, 1.0f);
    return car.boost_max * throttle * ramp;
}

float turbo_step(const CarData& car, float boost, float omega, float throttle, float dt) {
    if (!(car.boost_max > 0.0f)) {
        return 0.0f;
    }
    const float target = boost_target(car, omega, throttle);
    const float tau = target > boost ? car.boost_rise_time * car.boost_rise_speed /
                                           rmath::max(omega, car.idle_speed)
                                     : car.boost_fall_time;
    const float next = boost + (target - boost) * (1.0f - rmath::exp(-dt / tau));
    return rmath::clamp(next, 0.0f, car.boost_max);
}

float clutch_capacity(const CarData& car, ClutchMode mode, float engine_omega, float throttle) {
    if (mode == ClutchMode::Closed) {
        return car.clutch_capacity;
    }
    // Arranque: el motor se queda cerca de ω_l(θ), que sube con el acelerador hasta launch_rpm.
    // Por debajo de engage_rpm la capacidad es 0: al ralentí, ni avance lento ni calado. Entre
    // engage_rpm y la rampa, el embrague ya muerde (hasta bite_fraction de la capacidad, por
    // debajo del par del motor): el coche arranca mientras el motor sube de vueltas.
    const float target = lerp(car.engage_speed + kLaunchMinRamp, car.launch_speed, throttle);
    const float low = rmath::max(car.engage_speed, target - car.launch_ramp);
    const float bite = car.bite_fraction * smoothstep(car.engage_speed, low, engine_omega);
    const float ramp = (1.0f - car.bite_fraction) * smoothstep(low, target, engine_omega);
    return car.clutch_capacity * (bite + ramp);
}

void update_gearbox(const CarData& car, const GearboxInput& in, DrivetrainState& state) {
    if (state.shift_steps > 0) {
        --state.shift_steps;
        if (state.shift_steps == 0) {
            state.gear = state.target_gear;
        }
        return;
    }
    const int gear = state.gear;
    const float speed = in.drive_speed;

    // Botones (regla D4): caja R – N – 1ª … 5ª en los dos modos.
    if (in.shift_up && !in.shift_down) {
        if (gear == kReverseGear) {
            request_shift(car, kNeutralGear, state);
        } else if (gear == kNeutralGear) {
            if (fits(car, 1, speed)) {
                request_shift(car, 1, state);
            }
        } else if (in.mode == GearboxMode::Sequential && gear < kGearCount) {
            request_shift(car, gear + 1, state);
        }
        return;
    }
    if (in.shift_down && !in.shift_up) {
        if (gear == kNeutralGear) {
            if (in.stopped) {
                request_shift(car, kReverseGear, state);
            }
        } else if (gear >= 1) {
            if (in.mode == GearboxMode::Automatic) {
                // En automático, bajar solo sirve parado: a N.
                if (in.stopped) {
                    request_shift(car, kNeutralGear, state);
                }
            } else if (gear == 1) {
                request_shift(car, kNeutralGear, state);
            } else if (fits(car, gear - 1, speed)) {
                request_shift(car, gear - 1, state);
            }
        }
        return;
    }

    if (in.mode != GearboxMode::Automatic || gear < 1) {
        state.drift_steps = 0;
        return;
    }
    // Automático: umbrales según el acelerador, con la caja (las ruedas), no el motor: en el
    // arranque el embrague patina y no cuenta.
    const float rpm = gear_speed(car, gear, speed);
    const float up = lerp(car.upshift_closed, car.upshift_full, in.throttle);
    const float down = lerp(car.downshift_closed, car.downshift_full, in.throttle);
    if (gear < kGearCount && rpm >= up) {
        const float hold_limit = car.limiter_speed - car.limiter_ramp - kHoldLimitMargin;
        const bool at_limit = rmath::max(rpm, state.engine_omega) >= hold_limit;
        if (in.drifting && !at_limit && state.drift_steps < car.drift_hold_steps) {
            ++state.drift_steps;
            return;
        }
        request_shift(car, gear + 1, state);
        return;
    }
    state.drift_steps = 0;
    if (gear > 1 && rpm < down && gear_speed(car, gear - 1, speed) < up) {
        request_shift(car, gear - 1, state);
    }
}

float shift_throttle(const CarData& car, const DrivetrainState& state, float drive_speed) {
    if (state.target_gear == kNeutralGear) {
        return 0.0f;
    }
    const float target = gear_speed(car, state.target_gear, drive_speed);
    return rmath::clamp((target - state.engine_omega) / kBlipBand, 0.0f, 1.0f);
}

DriveSolution solve_drive(const DriveSystem& system, std::array<RowState, kDriveRows>& rows) {
    // A = J·M⁻¹·Jᵀ y b = J·x*.
    std::array<std::array<float, kDriveRows>, kDriveRows> a{};
    std::array<float, kDriveRows> b{};
    for (int k = 0; k < kDriveRows; ++k) {
        const auto rk = static_cast<std::size_t>(k);
        if (!system.active[rk]) {
            rows[rk] = RowState::Inactive;
            continue;
        }
        if (rows[rk] == RowState::Inactive) {
            rows[rk] = RowState::Stick;
        }
        float dot = 0.0f;
        for (std::size_t d = 0; d < kDriveDofs; ++d) {
            dot += system.jacobian[rk][d] * system.velocity[d];
        }
        b[rk] = dot;
        for (int l = 0; l <= k; ++l) {
            const auto rl = static_cast<std::size_t>(l);
            float sum = 0.0f;
            for (std::size_t d = 0; d < kDriveDofs; ++d) {
                sum += system.jacobian[rk][d] * system.inv_mass[d] * system.jacobian[rl][d];
            }
            a[rk][rl] = sum;
            a[rl][rk] = sum;
        }
    }
    // Diagonal que solo entra en la solución: 1/(h·c) de las amortiguadas y la regularización
    // de las de Coulomb (la velocidad de la comprobación usa A sin ella).
    std::array<float, kDriveRows> extra{};
    for (std::size_t k = 0; k < kDriveRows; ++k) {
        extra[k] = system.soft[k] > 0.0f ? system.soft[k] : kRegularization * a[k][k];
    }

    DriveSolution out;
    std::array<float, kDriveRows>& impulse = out.impulse;
    out.converged = false;
    for (int iteration = 1; iteration <= kMaxLcpIterations; ++iteration) {
        out.iterations = iteration;
        // Filas pegadas (y las amortiguadas): índices y lado derecho con las que deslizan.
        std::array<int, kDriveRows> index{};
        int n = 0;
        for (int k = 0; k < kDriveRows; ++k) {
            const RowState s = rows[static_cast<std::size_t>(k)];
            if (s == RowState::Inactive) {
                impulse[static_cast<std::size_t>(k)] = 0.0f;
            } else if (s == RowState::SlipPositive) {
                impulse[static_cast<std::size_t>(k)] = system.cap[static_cast<std::size_t>(k)];
            } else if (s == RowState::SlipNegative) {
                impulse[static_cast<std::size_t>(k)] = -system.cap[static_cast<std::size_t>(k)];
            } else {
                index[static_cast<std::size_t>(n++)] = k;
            }
        }
        std::array<std::array<float, kDriveRows>, kDriveRows> l{};
        std::array<float, kDriveRows> y{};
        for (int i = 0; i < n; ++i) {
            const auto ri = static_cast<std::size_t>(index[static_cast<std::size_t>(i)]);
            float rhs = -b[ri];
            for (std::size_t k = 0; k < kDriveRows; ++k) {
                const RowState s = rows[k];
                if (s == RowState::SlipPositive || s == RowState::SlipNegative) {
                    rhs -= a[ri][k] * impulse[k];
                }
            }
            y[static_cast<std::size_t>(i)] = rhs;
        }
        // Cholesky A_SS = L·Lᵀ (tamaño fijo, sin pivotar: A es definida positiva).
        for (int i = 0; i < n; ++i) {
            const auto si = static_cast<std::size_t>(i);
            const auto ri = static_cast<std::size_t>(index[si]);
            for (int j = 0; j <= i; ++j) {
                const auto sj = static_cast<std::size_t>(j);
                const auto rj = static_cast<std::size_t>(index[sj]);
                float sum = a[ri][rj] + (i == j ? extra[ri] : 0.0f);
                for (int k = 0; k < j; ++k) {
                    const auto sk = static_cast<std::size_t>(k);
                    sum -= l[si][sk] * l[sj][sk];
                }
                if (i == j) {
                    l[si][si] = rmath::sqrt(rmath::max(sum, 1.0e-30f));
                } else {
                    l[si][sj] = sum / l[sj][sj];
                }
            }
        }
        for (int i = 0; i < n; ++i) {
            const auto si = static_cast<std::size_t>(i);
            float sum = y[si];
            for (int k = 0; k < i; ++k) {
                sum -= l[si][static_cast<std::size_t>(k)] * y[static_cast<std::size_t>(k)];
            }
            y[si] = sum / l[si][si];
        }
        for (int i = n - 1; i >= 0; --i) {
            const auto si = static_cast<std::size_t>(i);
            float sum = y[si];
            for (int k = i + 1; k < n; ++k) {
                sum -= l[static_cast<std::size_t>(k)][si] * y[static_cast<std::size_t>(k)];
            }
            y[si] = sum / l[si][si];
        }
        for (int i = 0; i < n; ++i) {
            impulse[static_cast<std::size_t>(index[static_cast<std::size_t>(i)])] =
                y[static_cast<std::size_t>(i)];
        }

        // Comprobación: una pegada que pide más que su caja pasa a deslizar con su signo; una
        // que desliza y cuya velocidad relativa iría con su impulso vuelve a pegarse.
        bool changed = false;
        for (std::size_t k = 0; k < kDriveRows; ++k) {
            const RowState s = rows[k];
            if (s == RowState::Inactive || system.soft[k] > 0.0f) {
                continue;
            }
            const float cap = system.cap[k];
            if (s == RowState::Stick) {
                if (impulse[k] > cap * (1.0f + kCapTolerance)) {
                    rows[k] = RowState::SlipPositive;
                    changed = true;
                } else if (impulse[k] < -cap * (1.0f + kCapTolerance)) {
                    rows[k] = RowState::SlipNegative;
                    changed = true;
                }
                continue;
            }
            float v = b[k];
            for (std::size_t j = 0; j < kDriveRows; ++j) {
                v += a[k][j] * impulse[j];
            }
            const float direction = s == RowState::SlipPositive ? 1.0f : -1.0f;
            if (v * direction > kVelocityTolerance) {
                rows[k] = RowState::Stick;
                changed = true;
            }
        }
        if (!changed) {
            out.converged = true;
            break;
        }
    }
    // Sin converger: cada impulso, a su caja.
    for (std::size_t k = 0; k < kDriveRows; ++k) {
        if (rows[k] != RowState::Inactive && system.soft[k] == 0.0f) {
            impulse[k] = rmath::clamp(impulse[k], -system.cap[k], system.cap[k]);
        }
    }
    for (std::size_t d = 0; d < kDriveDofs; ++d) {
        float dv = 0.0f;
        for (std::size_t k = 0; k < kDriveRows; ++k) {
            dv += system.jacobian[k][d] * impulse[k];
        }
        out.velocity[d] = system.velocity[d] + system.inv_mass[d] * dv;
    }
    return out;
}

} // namespace rally::sim
