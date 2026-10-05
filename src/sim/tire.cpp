#include "sim/tire.h"

#include "sim/rmath.h"

namespace rally::sim {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Gravedad para la masa de esquina del amortiguamiento de la carcasa (la de Jolt por defecto).
constexpr float kGravity = 9.81f;

// Iteraciones de Newton para B (fijas: el resultado no depende de una tolerancia).
constexpr int kShapeIterations = 30;

// Por debajo de este ρ la fuerza es la lineal D·B·C·ρ (f(ρ)/ρ → B·C).
constexpr float kLinearRho = 1.0e-6f;

float magnitude(float x, float y) { return rmath::sqrt(x * x + y * y); }

// Argumento interior de la curva: φ = Bρ − E·(Bρ − atan Bρ).
float inner(const TireSurface& t, float rho) {
    const float b_rho = t.shape_b * rho;
    return b_rho - t.curvature_e * (b_rho - rmath::atan(b_rho));
}

float smoothstep(float edge0, float edge1, float x) {
    const float s = rmath::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return s * s * (3.0f - 2.0f * s);
}

// Paso de Coulomb: `base` es el giro sin los pares de Coulomb y `stop`, lo que esos pares
// quitan en el paso. Nunca invierten el giro: si no llegan a moverla, la rueda queda en 0.
float coulomb(float base, float stop) {
    if (base > stop) {
        return base - stop;
    }
    if (base < -stop) {
        return base + stop;
    }
    return 0.0f;
}

// Relajación exponencial exacta en h con tasa a: x ← x·e + entrada·φ.
struct Relax {
    float e = 1.0f;
    float phi = 0.0f;
};

Relax relax(float a, float h) {
    Relax r;
    if (a * h < 1.0e-6f) {
        r.e = 1.0f - a * h;
        r.phi = h;
    } else {
        r.e = rmath::exp(-a * h);
        r.phi = (1.0f - r.e) / a;
    }
    return r;
}

// Recorte de la huella: escala u y w para que ρ ≤ rho_max. Devuelve true si ha recortado.
bool limit_footprint(const TireSurface& t, float rho_max, float& u, float& w) {
    const float rho_x = u / (t.relaxation_long * t.slip_ratio_peak);
    const float rho_y = w / (t.relaxation_lat * t.tan_alpha_peak);
    const float rho = magnitude(rho_x, rho_y);
    if (rho <= rho_max) {
        return false;
    }
    const float scale = rho_max / rho;
    u *= scale;
    w *= scale;
    return true;
}

// ∂F_x/∂u de la fuerza elástica, con la dirección normalizada (la del régimen de adherencia,
// donde la rigidez importa para el giro semi-implícito): F_x = D·q(ρ)·ρ_x con q = f(ρ)/ρ.
// Nunca negativa: en la rama descendente la pendiente se trata de forma explícita.
float elastic_slope_x(const TireSurface& t, float u, float w, float d) {
    const float to_rho_x = 1.0f / (t.relaxation_long * t.slip_ratio_peak);
    const float rho_x = u * to_rho_x;
    const float rho_y = w / (t.relaxation_lat * t.tan_alpha_peak);
    const float rho = magnitude(rho_x, rho_y);
    float derivative = 0.0f; // ∂(q·ρ_x)/∂ρ_x
    if (rho < kLinearRho) {
        derivative = t.shape_b * t.shape_c;
    } else {
        const float f = tire_curve(t, rho);
        const float q = f / rho;
        const float dq = (tire_curve_slope(t, rho) * rho - f) / (rho * rho);
        derivative = q + dq * rho_x * rho_x / rho;
    }
    return rmath::max(0.0f, d * derivative * to_rho_x);
}

} // namespace

bool derive_tire_shape(TireSurface& tire, std::string& error) {
    if (!(tire.curvature_e < 1.0f)) {
        error = "curvature_e tiene que ser menor que 1 (con E ≥ 1 la curva baja de slide_ratio)";
        return false;
    }
    if (!(tire.slide_ratio > 0.0f && tire.slide_ratio < 1.0f)) {
        error = "slide_ratio tiene que estar entre 0 y 1";
        return false;
    }
    // La raíz entre 1 y 2 de sin(C·π/2) = slide_ratio: la que da pico.
    const float c = 2.0f - (2.0f / kPi) * rmath::asin(tire.slide_ratio);
    // Pico en ρ = 1: C·atan(φ(1)) = π/2, es decir, B·(1 − E) + E·atan B = tan(π/(2C)).
    const float target = rmath::tan(kPi / (2.0f * c));
    const float e = tire.curvature_e;
    float b = target;
    for (int i = 0; i < kShapeIterations; ++i) {
        const float g = b * (1.0f - e) + e * rmath::atan(b) - target;
        const float dg = (1.0f - e) + e / (1.0f + b * b);
        b -= g / dg;
    }
    const float residual = b * (1.0f - e) + e * rmath::atan(b) - target;
    if (!(b > 0.0f) || !(rmath::abs(residual) <= 1.0e-5f * target)) {
        error = "no se puede poner el pico de la curva donde se pide con este curvature_e";
        return false;
    }
    tire.shape_c = c;
    tire.shape_b = b;
    tire.tan_alpha_peak = rmath::tan(tire.slip_angle_peak);
    // Rueda bloqueada (κ = −1) deslizando a kSlideCapAngle.
    const float lateral = rmath::tan(kSlideCapAngle) / tire.tan_alpha_peak;
    tire.slide_cap = magnitude(1.0f / tire.slip_ratio_peak, lateral);
    return true;
}

float tire_curve(const TireSurface& tire, float rho) {
    return rmath::sin(tire.shape_c * rmath::atan(inner(tire, rho)));
}

float tire_curve_slope(const TireSurface& tire, float rho) {
    const float b_rho = tire.shape_b * rho;
    const float phi = inner(tire, rho);
    const float dphi =
        tire.shape_b * (1.0f - tire.curvature_e + tire.curvature_e / (1.0f + b_rho * b_rho));
    return rmath::cos(tire.shape_c * rmath::atan(phi)) * tire.shape_c * dphi / (1.0f + phi * phi);
}

float peak_mu(const TireSurface& tire, float load, float nominal_load) {
    const float ratio = rmath::clamp(load / nominal_load, 0.0f, kMaxLoadRatio);
    return tire.mu_peak * (1.0f - tire.load_sensitivity * (ratio - 1.0f));
}

TireForce tire_force(const TireSurface& tire, float kappa, float tan_alpha, float load,
                     float nominal_load) {
    TireForce force;
    const float d = peak_mu(tire, load, nominal_load) * load;
    const float rho_x = kappa / tire.slip_ratio_peak;
    const float rho_y = tan_alpha / tire.tan_alpha_peak;
    const float rho = magnitude(rho_x, rho_y);
    if (rho < kLinearRho) {
        // f(ρ)/ρ → B·C: fuerza lineal, en la dirección normalizada.
        const float k = d * tire.shape_b * tire.shape_c;
        force.fx = k * rho_x;
        force.fy = k * rho_y;
        force.fx_pure = force.fx;
        force.fy_pure = force.fy;
        return force;
    }
    const float magnitude_f = d * tire_curve(tire, rho);
    // Dirección: la del vector normalizado (adherencia), la de la velocidad de deslizamiento
    // (κ_r, tan α_r) al deslizar, y entre ρ = 1,5 y 3 la suma de los dos unitarios con pesos
    // (1 − w) y w, normalizada. Los dos están en el mismo cuadrante: la suma nunca es nula.
    float dx = rho_x / rho;
    float dy = rho_y / rho;
    const float blend = smoothstep(kDirectionBlendStart, kDirectionBlendEnd, rho);
    if (blend > 0.0f) {
        const float slide = magnitude(kappa, tan_alpha);
        dx = (1.0f - blend) * dx + blend * kappa / slide;
        dy = (1.0f - blend) * dy + blend * tan_alpha / slide;
        const float length = magnitude(dx, dy);
        dx /= length;
        dy /= length;
    }
    force.fx = magnitude_f * dx;
    force.fy = magnitude_f * dy;
    const float sign_x = rho_x < 0.0f ? -1.0f : 1.0f;
    const float sign_y = rho_y < 0.0f ? -1.0f : 1.0f;
    force.fx_pure = sign_x * d * tire_curve(tire, rmath::abs(rho_x));
    force.fy_pure = sign_y * d * tire_curve(tire, rmath::abs(rho_y));
    return force;
}

TireForce step_tire(const CarData& car, const TireInput& in, float h, bool implicit,
                    WheelDynamics& state) {
    const float r = in.radius;
    const float inertia = car.wheel_inertia;
    TireForce force;
    if (!(in.load > 0.0f)) {
        // Sin carga (en el aire): sin deformación ni fuerza. El motor acelera la rueda y el
        // freno la para.
        state.u = 0.0f;
        state.w = 0.0f;
        const float k = h / inertia;
        state.omega = coulomb(state.omega + k * in.drive_torque, k * in.stop_torque);
        return force;
    }
    const TireSurface& t = car.tire(in.surface);
    const float limit = peak_mu(t, in.load, car.nominal_load) * in.load; // D
    const float sigma_x = t.relaxation_long;
    const float sigma_y = t.relaxation_lat;
    // Amortiguamiento de la carcasa con ζ del modo carrocería–huella (masa de esquina F_z/g)
    // y la rigidez de la huella en el origen, B·C·D/(κ_pico·σ_x) y B·C·D/(tan α_pico·σ_y):
    // c = 2·ζ·√(k·m).
    const float stiffness = t.shape_b * t.shape_c * limit;
    const float corner_mass = in.load / kGravity;
    const float c_x = 2.0f * car.carcass_damping_long *
                      rmath::sqrt(stiffness / (t.slip_ratio_peak * sigma_x) * corner_mass);
    const float c_y = 2.0f * car.carcass_damping_lat *
                      rmath::sqrt(stiffness / (t.tan_alpha_peak * sigma_y) * corner_mass);
    // Factor de baja velocidad: 1 parado y con la rueda quieta, 0 desde carcass_damping_fade.
    const float v_ref = rmath::max(magnitude(in.vx, in.vy), rmath::abs(state.omega) * r);
    const float fade = rmath::clamp(1.0f - v_ref / car.carcass_damping_fade, 0.0f, 1.0f);
    // Recorte de la huella: ρ ≤ 1 parado (Coulomb en el pico), slide_cap (rueda bloqueada a
    // 75°) desde carcass_damping_fade.
    const float rho_max = 1.0f + (t.slide_cap - 1.0f) * (1.0f - fade);
    const Relax rx = relax(rmath::abs(in.vx) / sigma_x, h);
    const Relax ry = relax(rmath::abs(in.vx) / sigma_y, h);
    const float s_y = -in.vy;

    // Amortiguamiento de la carcasa sobre la velocidad de deformación de la huella, c·du/dt y
    // c·dw/dt en el subpaso (la histéresis de la carcasa). Parado, du/dt = s_x; en régimen
    // estacionario, du/dt = 0: no cambia la curva ni la rigidez en curva.
    const float u0 = state.u;
    const float w0 = state.w;
    const float inv_h = 1.0f / h;

    // Predicción con el giro sin cambiar, para el giro semi-implícito.
    const float s_x0 = state.omega * r - in.vx;
    float u_p = state.u * rx.e + s_x0 * rx.phi;
    float w_p = state.w * ry.e + s_y * ry.phi;
    const bool limited = limit_footprint(t, rho_max, u_p, w_p);
    const TireForce elastic0 =
        tire_force(t, u_p / sigma_x, w_p / sigma_y, in.load, car.nominal_load);
    float fx0 = elastic0.fx + c_x * fade * (u_p - u0) * inv_h;
    const float fy0 = elastic0.fy + c_y * fade * (w_p - w0) * inv_h;
    // ∂F_x/∂ω: la huella recortada no responde al giro.
    float slope =
        limited ? 0.0f : (elastic_slope_x(t, u_p, w_p, limit) + c_x * fade * inv_h) * r * rx.phi;
    const float predicted = magnitude(fx0, fy0);
    if (predicted > limit) {
        fx0 *= limit / predicted;
        slope = 0.0f;
    }
    if (!implicit) {
        slope = 0.0f;
    }
    const float k = h / (inertia + h * r * slope);
    state.omega = coulomb(state.omega + k * (in.drive_torque - r * fx0), k * in.stop_torque);

    // Deformación con el giro nuevo y su recorte: la huella desliza y no acumula más allá de
    // ρ_max.
    const float s_x = state.omega * r - in.vx;
    state.u = state.u * rx.e + s_x * rx.phi;
    state.w = state.w * ry.e + s_y * ry.phi;
    limit_footprint(t, rho_max, state.u, state.w);
    const TireForce elastic =
        tire_force(t, state.u / sigma_x, state.w / sigma_y, in.load, car.nominal_load);
    force = elastic;
    force.fx = elastic.fx + c_x * fade * (state.u - u0) * inv_h;
    force.fy = elastic.fy + c_y * fade * (state.w - w0) * inv_h;
    const float total = magnitude(force.fx, force.fy);
    if (total > limit) {
        force.fx *= limit / total;
        force.fy *= limit / total;
    }
    return force;
}

} // namespace rally::sim
