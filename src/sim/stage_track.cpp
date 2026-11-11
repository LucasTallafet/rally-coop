#include "sim/stage_track.h"

#include "sim/check.h"
#include "sim/fnv1a.h"
#include "sim/rmath.h"
#include "sim/stage_ground.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace rally::sim {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// Muestras del eje: cada pieza en partes iguales de ≤ 0,5 m.
constexpr double kSampleSpacing = 0.5;
// El índice usa una muestra de cada 8 (≤ 4 m): todo punto del eje está a ≤ 2 m de una.
constexpr std::size_t kIndexStride = 8;
constexpr double kIndexSampleReach = 0.5 * kSampleSpacing * static_cast<double>(kIndexStride);
constexpr double kCellSize = 10.0;
// Margen de seguridad de la lista cercana: el semiancho máximo sale de las muestras, y un punto
// de [width] puede caer entre dos.
constexpr double kNearSafety = 1.0;

// Pie perpendicular: componente longitudinal de (p − P) por debajo de esto.
constexpr double kPerpendicularTolerance = 1.0e-4;
// Newton en clotoides: hasta que el paso es menor que esto (m), con un tope de iteraciones.
constexpr double kNewtonTolerance = 1.0e-10;
constexpr int kNewtonMaxIterations = 60;
// g′ por debajo de esto no se usa para Newton (bisección).
constexpr double kNewtonMinSlope = 1.0e-3;

// Ventana incremental (§2.4): [s − 20, s + d + 20].
constexpr double kWindowMargin = 20.0;

// Validación (§2.6): margen de la malla en curva, |Δs| de pasadas distintas y separación mínima.
constexpr double kFoldMargin = 0.5;
constexpr double kPassGap = 50.0;
constexpr double kMinCoreGap = 1.5;

std::int64_t cell_key(std::int32_t ix, std::int32_t iz) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(static_cast<std::uint32_t>(iz))
                                     << 32) |
           static_cast<std::int64_t>(static_cast<std::uint32_t>(ix));
}

std::int32_t cell_coord(double v, double size) {
    return static_cast<std::int32_t>(rmath::floor(v / size));
}

// Número con dos decimales (para los errores «s = …»).
std::string fixed2(double value) {
    std::array<char, 64> buffer{};
    const std::to_chars_result r = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                                 value, std::chars_format::fixed, 2);
    return std::string(buffer.data(), r.ptr);
}

StageParseResult fail_at(double s, const std::string& message) {
    StageParseResult result;
    result.error = "s = " + fixed2(s) + ": " + message;
    return result;
}

double side_core(const StagePose& p, int side) {
    return (side > 0 ? p.half_width_right : p.half_width_left) + kStageShoulderWidth;
}

} // namespace

// --- Geometría -----------------------------------------------------------------------------------

StagePose StageTrack::pose(double s) const {
    const double length = axis_.length();
    if (!(s >= s_begin())) {
        s = s_begin(); // también NaN
    } else if (s > s_end()) {
        s = s_end();
    }
    if (s >= 0.0 && s <= length) {
        return axis_.pose(s);
    }
    // Prolongaciones: recta con el rumbo, la pendiente y el resto de valores del extremo.
    const double end = s < 0.0 ? 0.0 : length;
    StagePose p = axis_.pose(end);
    const double ds = s - end;
    p.s = s;
    p.x += ds * rmath::sin(p.heading);
    p.z -= ds * rmath::cos(p.heading);
    p.y += ds * p.grade;
    p.curvature = 0.0;
    p.vertical_curvature = 0.0;
    return p;
}

double StageTrack::core_half_width(double s, int side) const { return side_core(pose(s), side); }

StageProjection StageTrack::finish(double x, double z, double s, double margin) const {
    const StagePose p = pose(s);
    const double dx = x - p.x;
    const double dz = z - p.z;
    const double sin_h = rmath::sin(p.heading);
    const double cos_h = rmath::cos(p.heading);
    // Avance T = (sin θ, −cos θ); derecha N = (cos θ, sin θ).
    const double along = dx * sin_h - dz * cos_h;
    StageProjection out;
    out.s = p.s;
    out.t = dx * cos_h + dz * sin_h;
    out.distance = rmath::sqrt(dx * dx + dz * dz);
    out.on_road = rmath::abs(along) <= kPerpendicularTolerance &&
                  rmath::abs(out.t) <= side_core(p, out.t >= 0.0 ? 1 : -1) + margin;
    return out;
}

template <typename F>
void StageTrack::for_each_candidate(double x, double z, double lo, double hi, F&& f) const {
    // Ventana acotada al tramo (NaN da el extremo).
    lo = lo >= s_begin() ? rmath::min(lo, s_end()) : s_begin();
    hi = hi <= s_end() ? rmath::max(hi, lo) : s_end();
    const auto first = std::upper_bound(pieces_.begin(), pieces_.end(), lo,
                                        [](double v, const Piece& p) { return v < p.s0; });
    std::size_t j =
        first == pieces_.begin() ? 0 : static_cast<std::size_t>(first - pieces_.begin()) - 1;
    for (; j < pieces_.size() && pieces_[j].s0 <= hi; ++j) {
        const Piece& piece = pieces_[j];
        const double a = rmath::max(lo, piece.s0);
        const double b = rmath::min(hi, piece.s1);
        if (a > b) {
            continue;
        }
        switch (piece.kind) {
        case Piece::Kind::Straight: {
            const double tx = rmath::sin(piece.heading0);
            const double tz = -rmath::cos(piece.heading0);
            const double u = (x - piece.x0) * tx + (z - piece.z0) * tz;
            const double s = rmath::clamp(piece.s0 + u, a, b);
            const double px = piece.x0 + (s - piece.s0) * tx;
            const double pz = piece.z0 + (s - piece.s0) * tz;
            f(Candidate{s, (x - px) * (x - px) + (z - pz) * (z - pz)});
            break;
        }
        case Piece::Kind::Arc: {
            const double r = rmath::abs(piece.radius);
            const double sign = piece.radius > 0.0 ? 1.0 : -1.0;
            const auto at = [&](double s) {
                const double h = piece.heading0 + (s - piece.s0) / piece.radius;
                const double px = piece.cx - piece.radius * rmath::cos(h);
                const double pz = piece.cz - piece.radius * rmath::sin(h);
                return Candidate{s, (x - px) * (x - px) + (z - pz) * (z - pz)};
            };
            f(at(a));
            const double vx = x - piece.cx;
            const double vz = z - piece.cz;
            const double v2 = vx * vx + vz * vz;
            if (v2 > 0.0) {
                // Rumbo del pie: el centro queda a la derecha (R > 0) o a la izquierda del avance.
                const double foot_heading = rmath::atan2(-sign * vz, -sign * vx);
                // Ángulo recorrido por el arco hasta el pie, llevado a [ángulo de a, + 2π).
                const double phi_a = (a - piece.s0) / r;
                const double phi_b = (b - piece.s0) / r;
                double phi = (foot_heading - piece.heading0) * sign;
                phi -= kTwoPi * rmath::floor((phi - phi_a) / kTwoPi);
                const double dv = rmath::sqrt(v2) - r;
                for (; phi <= phi_b; phi += kTwoPi) {
                    const double s = piece.s0 + phi * r;
                    if (s > a && s < b) {
                        f(Candidate{s, dv * dv});
                    }
                }
            }
            if (b > a) {
                f(at(b));
            }
            break;
        }
        case Piece::Kind::Spiral: {
            // Secuencia: a, las muestras de dentro de (a, b) y b. En cada mínimo local, Newton
            // protegido por bisección entre sus vecinos.
            const std::size_t first_sample = piece.sample_first;
            const std::size_t last_sample = first_sample + piece.sample_count;
            const double step = (piece.s1 - piece.s0) / static_cast<double>(piece.sample_count);
            std::size_t k0 =
                first_sample +
                static_cast<std::size_t>(rmath::clamp(rmath::floor((a - piece.s0) / step), 0.0,
                                                      static_cast<double>(piece.sample_count)));
            while (k0 <= last_sample && sample_s_[k0] <= a) {
                ++k0;
            }
            std::size_t k1 =
                first_sample +
                static_cast<std::size_t>(rmath::clamp(rmath::ceil((b - piece.s0) / step), 0.0,
                                                      static_cast<double>(piece.sample_count)));
            while (k1 >= k0 && k1 > first_sample && sample_s_[k1] >= b) {
                --k1;
            }
            const bool has_inner = k0 <= last_sample && k1 >= k0 && sample_s_[k1] < b;
            const std::size_t inner = has_inner ? k1 - k0 + 1 : 0;
            const std::size_t count = inner + (b > a ? 2 : 1);
            // Elemento i de la secuencia: s y distancia².
            const StagePose pa = pose(a);
            const StagePose pb = pose(b);
            const auto seq_s = [&](std::size_t i) {
                return i == 0 ? a : (i <= inner ? sample_s_[k0 + i - 1] : b);
            };
            const auto seq_d2 = [&](std::size_t i) {
                double px = 0.0;
                double pz = 0.0;
                if (i == 0) {
                    px = pa.x;
                    pz = pa.z;
                } else if (i <= inner) {
                    px = sample_x_[k0 + i - 1];
                    pz = sample_z_[k0 + i - 1];
                } else {
                    px = pb.x;
                    pz = pb.z;
                }
                return (x - px) * (x - px) + (z - pz) * (z - pz);
            };
            // g(s) = (P − p)·T, derivada de la distancia²/2; g′ = 1 − κ·t.
            struct Eval {
                double g;
                double slope;
                double d2;
            };
            const auto eval = [&](double s) {
                const StagePose p = pose(s);
                const double dx = x - p.x;
                const double dz = z - p.z;
                const double sin_h = rmath::sin(p.heading);
                const double cos_h = rmath::cos(p.heading);
                const double along = dx * sin_h - dz * cos_h;
                const double lateral = dx * cos_h + dz * sin_h;
                return Eval{-along, 1.0 - p.curvature * lateral, dx * dx + dz * dz};
            };
            double prev = seq_d2(0);
            double here = prev;
            for (std::size_t i = 0; i < count; ++i) {
                const double next = i + 1 < count ? seq_d2(i + 1) : here;
                const bool left_ok = i == 0 || here <= prev;
                const bool right_ok = i + 1 == count || here <= next;
                if (left_ok && right_ok) {
                    double lo_s = seq_s(i == 0 ? 0 : i - 1);
                    double hi_s = seq_s(i + 1 < count ? i + 1 : i);
                    if (lo_s == hi_s) {
                        f(Candidate{lo_s, here});
                    } else {
                        const Eval e_lo = eval(lo_s);
                        const Eval e_hi = eval(hi_s);
                        if (e_lo.g >= 0.0) {
                            f(Candidate{lo_s, e_lo.d2});
                        } else if (e_hi.g <= 0.0) {
                            f(Candidate{hi_s, e_hi.d2});
                        } else {
                            double u = seq_s(i);
                            if (!(u > lo_s && u < hi_s)) {
                                u = 0.5 * (lo_s + hi_s);
                            }
                            Eval e = eval(u);
                            for (int it = 0; it < kNewtonMaxIterations && e.g != 0.0; ++it) {
                                if (e.g < 0.0) {
                                    lo_s = u;
                                } else {
                                    hi_s = u;
                                }
                                double next_u = e.slope > kNewtonMinSlope ? u - e.g / e.slope
                                                                          : 0.5 * (lo_s + hi_s);
                                if (!(next_u > lo_s && next_u < hi_s)) {
                                    next_u = 0.5 * (lo_s + hi_s);
                                }
                                if (rmath::abs(next_u - u) <= kNewtonTolerance) {
                                    break;
                                }
                                u = next_u;
                                e = eval(u);
                            }
                            f(Candidate{u, e.d2});
                        }
                    }
                }
                prev = here;
                here = next;
            }
            break;
        }
        }
    }
}

StageProjection StageTrack::project(double x, double z, double s_lo, double s_hi) const {
    Candidate best{0.0, std::numeric_limits<double>::infinity()};
    for_each_candidate(x, z, s_lo, s_hi, [&best](const Candidate& c) {
        if (c.d2 < best.d2) {
            best = c;
        }
    });
    if (!(best.d2 < std::numeric_limits<double>::infinity())) {
        best.s = rmath::clamp(s_lo, s_begin(), s_end()); // NaN en la entrada
    }
    return finish(x, z, best.s);
}

const StageTrack::Cell* StageTrack::find_cell(double x, double z) const {
    const double fx = rmath::floor(x / kCellSize);
    const double fz = rmath::floor(z / kCellSize);
    // Fuera del rango de int32 (o NaN): ninguna celda.
    constexpr double kLimit = 2.0e9;
    if (!(rmath::abs(fx) < kLimit && rmath::abs(fz) < kLimit)) {
        return nullptr;
    }
    const std::int64_t key = cell_key(static_cast<std::int32_t>(fx), static_cast<std::int32_t>(fz));
    const auto it = std::lower_bound(cells_.begin(), cells_.end(), key,
                                     [](const Cell& c, std::int64_t k) { return c.key < k; });
    return it != cells_.end() && it->key == key ? &*it : nullptr;
}

StageProjection StageTrack::acquire(double x, double z) const {
    Candidate best{0.0, std::numeric_limits<double>::infinity()};
    const auto keep = [&best](const Candidate& c) {
        if (c.d2 < best.d2 || (c.d2 == best.d2 && c.s < best.s)) {
            best = c;
        }
    };
    if (const Cell* cell = find_cell(x, z)) {
        for (std::uint32_t i = 0; i < cell->far_count; ++i) {
            const Interval& iv = intervals_[cell->far_first + i];
            for_each_candidate(x, z, iv.s0, iv.s1, keep);
        }
    }
    // Lo listado cubre todo el eje a menos de 60 m de la celda: si lo mejor está más lejos (o
    // no hay celda), el punto está fuera de la franja y se busca en todo el tramo.
    if (!(best.d2 <= kStageStripHalfWidth * kStageStripHalfWidth)) {
        best = Candidate{0.0, std::numeric_limits<double>::infinity()};
        for_each_candidate(x, z, s_begin(), s_end(), keep);
    }
    if (!(best.d2 < std::numeric_limits<double>::infinity())) {
        best.s = s_begin(); // NaN en la entrada
    }
    return finish(x, z, best.s);
}

bool StageTrack::on_any_road(double x, double z, StageProjection& out, double margin) const {
    const Cell* cell = find_cell(x, z);
    if (cell == nullptr) {
        return false;
    }
    const double reach = core_max_ + kNearSafety;
    bool found = false;
    StageProjection best;
    for (std::uint32_t i = 0; i < cell->near_count; ++i) {
        const Interval& iv = intervals_[cell->near_first + i];
        for_each_candidate(x, z, iv.s0, iv.s1, [&](const Candidate& c) {
            if (c.d2 > reach * reach) {
                return;
            }
            const StageProjection p = finish(x, z, c.s, margin);
            if (p.on_road && (!found || p.distance < best.distance)) {
                best = p;
                found = true;
            }
        });
    }
    if (found) {
        out = best;
    }
    return found;
}

int StageTrack::other_feet(double x, double z, double s_self, double gap, StageProjection* out,
                           int capacity) const {
    const Cell* cell = find_cell(x, z);
    if (cell == nullptr) {
        return 0;
    }
    int count = 0;
    for (std::uint32_t i = 0; i < cell->far_count; ++i) {
        const Interval& iv = intervals_[cell->far_first + i];
        for_each_candidate(x, z, iv.s0, iv.s1, [&](const Candidate& c) {
            if (!(rmath::abs(c.s - s_self) > gap)) {
                return;
            }
            const StagePose p = pose(c.s);
            const double along =
                (x - p.x) * rmath::sin(p.heading) - (z - p.z) * rmath::cos(p.heading);
            if (rmath::abs(along) > kPerpendicularTolerance) {
                return; // un extremo de pieza o de intervalo, no un pie
            }
            // El mismo pie desde dos piezas o dos intervalos: una vez.
            for (int k = 0; k < rmath::min(count, capacity); ++k) {
                if (out[k].s == c.s) {
                    return;
                }
            }
            if (count < capacity) {
                out[count] = finish(x, z, c.s);
            }
            ++count; // los que no caben se cuentan: quien llama ve el desborde
        });
    }
    return count;
}

std::size_t StageTrack::index_bytes() const {
    return pieces_.size() * sizeof(Piece) +
           (sample_s_.size() + sample_x_.size() + sample_z_.size()) * sizeof(double) +
           cells_.size() * sizeof(Cell) + intervals_.size() * sizeof(Interval);
}

// --- Carga ---------------------------------------------------------------------------------------

StageParseResult StageTrack::build(std::string_view text, StageTrack& out) {
    StageTrack track;
    StageParseResult parsed = StageAxis::parse(text, track.axis_);
    if (!parsed.ok) {
        return parsed;
    }
    std::string error = track.prepare();
    if (error.empty()) {
        // Validación 3 (formato 2): la banda de entorno cabe entre las pasadas; se guarda para
        // StageGround::build.
        error = stage_band_attach(track);
    }
    if (!error.empty()) {
        StageParseResult result;
        result.error = std::move(error);
        return result;
    }
    out = std::move(track);
    StageParseResult result;
    result.ok = true;
    return result;
}

std::string StageTrack::prepare() {
    const double length = axis_.length();

    // Piezas: entrada, primitivas y frenada.
    {
        const StagePose p0 = axis_.pose(0.0);
        Piece entry;
        entry.s0 = -kStageEntryLength;
        entry.s1 = 0.0;
        entry.heading0 = p0.heading;
        entry.x0 = p0.x - kStageEntryLength * rmath::sin(p0.heading);
        entry.z0 = p0.z + kStageEntryLength * rmath::cos(p0.heading);
        pieces_.push_back(entry);
    }
    const std::vector<StagePrimitive>& prims = axis_.primitives();
    for (std::size_t i = 0; i < prims.size(); ++i) {
        const StagePrimitive& prim = prims[i];
        const double s1 = i + 1 < prims.size() ? prims[i + 1].s0 : length;
        // Un arco que gira más de π se parte en piezas iguales de ≤ π (el pie de un arco se
        // busca a ±π de su mitad).
        std::size_t parts = 1;
        if (prim.curvature0 == prim.curvature1 && prim.curvature0 != 0.0) {
            parts = static_cast<std::size_t>(
                rmath::max(1.0, rmath::ceil(rmath::abs(prim.curvature0) * (s1 - prim.s0) / kPi)));
        }
        for (std::size_t k = 0; k < parts; ++k) {
            Piece piece;
            piece.s0 = k == 0 ? prim.s0
                              : prim.s0 + (s1 - prim.s0) * static_cast<double>(k) /
                                              static_cast<double>(parts);
            piece.s1 = k + 1 == parts ? s1
                                      : prim.s0 + (s1 - prim.s0) * static_cast<double>(k + 1) /
                                                      static_cast<double>(parts);
            const StagePose p = axis_.pose(piece.s0);
            piece.x0 = p.x;
            piece.z0 = p.z;
            piece.heading0 = p.heading;
            if (prim.curvature0 != prim.curvature1) {
                piece.kind = Piece::Kind::Spiral;
                piece.curvature0 = prim.curvature0;
                piece.curvature1 = prim.curvature1;
            } else if (prim.curvature0 != 0.0) {
                piece.kind = Piece::Kind::Arc;
                piece.curvature0 = prim.curvature0;
                piece.curvature1 = prim.curvature1;
                piece.radius = 1.0 / prim.curvature0;
                piece.cx = p.x + piece.radius * rmath::cos(p.heading);
                piece.cz = p.z + piece.radius * rmath::sin(p.heading);
            }
            pieces_.push_back(piece);
        }
    }
    {
        const StagePose pl = axis_.pose(length);
        Piece runoff;
        runoff.s0 = length;
        runoff.s1 = length + kStageRunoffLength;
        runoff.x0 = pl.x;
        runoff.z0 = pl.z;
        runoff.heading0 = pl.heading;
        pieces_.push_back(runoff);
    }

    // Muestras de todo [s_begin, s_end] y, de paso, rumbo y núcleos (para la validación).
    std::vector<double> sample_heading;
    std::vector<double> core_left;
    std::vector<double> core_right;
    const auto add_sample = [&](double s) {
        const StagePose p = pose(s);
        sample_s_.push_back(s);
        sample_x_.push_back(p.x);
        sample_z_.push_back(p.z);
        sample_heading.push_back(p.heading);
        core_left.push_back(side_core(p, -1));
        core_right.push_back(side_core(p, 1));
        core_max_ = rmath::max(core_max_, rmath::max(core_left.back(), core_right.back()));
    };
    for (Piece& piece : pieces_) {
        const double span = piece.s1 - piece.s0;
        const auto count =
            static_cast<std::uint32_t>(rmath::max(1.0, rmath::ceil(span / kSampleSpacing)));
        piece.sample_first = static_cast<std::uint32_t>(sample_s_.size());
        piece.sample_count = count;
        for (std::uint32_t k = 0; k < count; ++k) {
            add_sample(piece.s0 + span * static_cast<double>(k) / static_cast<double>(count));
        }
    }
    add_sample(s_end());
    const std::size_t samples = sample_s_.size();

    // Validación 0: ningún arco gira una vuelta o más (tercera respuesta del consultor a
    // tramo-fisica, §4): se cerraría sobre sí mismo.
    for (std::size_t i = 0; i < prims.size(); ++i) {
        const StagePrimitive& prim = prims[i];
        if (prim.curvature0 != prim.curvature1 || prim.curvature0 == 0.0) {
            continue;
        }
        const double s1 = i + 1 < prims.size() ? prims[i + 1].s0 : length;
        const double turn = rmath::abs(prim.curvature0) * (s1 - prim.s0);
        if (turn >= 2.0 * kPi - 1.0e-9) {
            return fail_at(prim.s0,
                           "el arco gira " + fixed2(turn * 180.0 / kPi) + "°, más de una vuelta")
                .error;
        }
    }

    // Validación 1: la malla no se pliega en ninguna curva.
    for (const Piece& piece : pieces_) {
        if (piece.kind == Piece::Kind::Straight) {
            continue;
        }
        for (std::uint32_t k = 0; k <= piece.sample_count; ++k) {
            const std::size_t g = piece.sample_first + k;
            const double s = sample_s_[g];
            const double u = (s - piece.s0) / (piece.s1 - piece.s0);
            const double curvature = piece.curvature0 + (piece.curvature1 - piece.curvature0) * u;
            if (curvature == 0.0) {
                continue;
            }
            const double inner = curvature > 0.0 ? core_right[g] : core_left[g];
            if (rmath::abs(curvature) * (inner + kFoldMargin) >= 1.0) {
                return fail_at(s, "la curva de R " + fixed2(1.0 / rmath::abs(curvature)) +
                                      " m es más cerrada que su núcleo interior + 0.50 m (" +
                                      fixed2(inner + kFoldMargin) + " m)")
                    .error;
            }
        }
    }

    // Validación 2: los núcleos de puntos con |Δs| > 50 m están a ≥ 1,5 m en planta.
    {
        const double reach = 2.0 * core_max_ + kMinCoreGap;
        const double size = rmath::max(kCellSize, reach + 1.0);
        std::vector<std::pair<std::int64_t, std::uint32_t>> buckets;
        buckets.reserve(samples);
        for (std::size_t g = 0; g < samples; ++g) {
            buckets.emplace_back(
                cell_key(cell_coord(sample_x_[g], size), cell_coord(sample_z_[g], size)),
                static_cast<std::uint32_t>(g));
        }
        std::sort(buckets.begin(), buckets.end());
        for (std::size_t i = 0; i < samples; ++i) {
            const std::int32_t ix = cell_coord(sample_x_[i], size);
            const std::int32_t iz = cell_coord(sample_z_[i], size);
            const double nix = rmath::cos(sample_heading[i]);
            const double niz = rmath::sin(sample_heading[i]);
            for (std::int32_t dz = -1; dz <= 1; ++dz) {
                for (std::int32_t dx = -1; dx <= 1; ++dx) {
                    const std::int64_t key = cell_key(ix + dx, iz + dz);
                    auto it = std::lower_bound(buckets.begin(), buckets.end(), key,
                                               [](const std::pair<std::int64_t, std::uint32_t>& e,
                                                  std::int64_t k) { return e.first < k; });
                    for (; it != buckets.end() && it->first == key; ++it) {
                        const std::size_t j = it->second;
                        if (!(sample_s_[j] - sample_s_[i] > kPassGap)) {
                            continue;
                        }
                        const double ddx = sample_x_[j] - sample_x_[i];
                        const double ddz = sample_z_[j] - sample_z_[i];
                        const double d2 = ddx * ddx + ddz * ddz;
                        if (d2 >= reach * reach) {
                            continue;
                        }
                        const double nj_x = rmath::cos(sample_heading[j]);
                        const double nj_z = rmath::sin(sample_heading[j]);
                        const double ci =
                            ddx * nix + ddz * niz >= 0.0 ? core_right[i] : core_left[i];
                        const double cj =
                            -ddx * nj_x - ddz * nj_z >= 0.0 ? core_right[j] : core_left[j];
                        const double gap = rmath::sqrt(d2) - ci - cj;
                        if (gap < kMinCoreGap) {
                            return fail_at(sample_s_[i],
                                           "el núcleo está a " + fixed2(gap) + " m del de s = " +
                                               fixed2(sample_s_[j]) + " (mínimo 1.50 m)")
                                .error;
                        }
                    }
                }
            }
        }
    }

    // Índice: por celda de 10 m, los intervalos de s a menos de 60 m (lejanos) y a menos del
    // mayor núcleo (cercanos). Una muestra de cada kIndexStride.
    std::vector<std::size_t> index_samples;
    for (std::size_t g = 0; g < samples; g += kIndexStride) {
        index_samples.push_back(g);
    }
    if (index_samples.back() != samples - 1) {
        index_samples.push_back(samples - 1);
    }
    struct Group {
        std::int64_t key;
        std::uint32_t first;
        std::uint32_t count;
    };
    const auto collect = [&](double margin, std::vector<Interval>& intervals,
                             std::vector<Group>& groups) {
        const double reach = margin + kIndexSampleReach;
        std::vector<std::pair<std::int64_t, std::uint32_t>> entries;
        for (std::size_t o = 0; o < index_samples.size(); ++o) {
            const double x = sample_x_[index_samples[o]];
            const double z = sample_z_[index_samples[o]];
            const std::int32_t ix0 = cell_coord(x - reach, kCellSize);
            const std::int32_t ix1 = cell_coord(x + reach, kCellSize);
            const std::int32_t iz0 = cell_coord(z - reach, kCellSize);
            const std::int32_t iz1 = cell_coord(z + reach, kCellSize);
            for (std::int32_t iz = iz0; iz <= iz1; ++iz) {
                const double cz0 = iz * kCellSize;
                const double ddz = rmath::max(0.0, rmath::max(cz0 - z, z - (cz0 + kCellSize)));
                for (std::int32_t ix = ix0; ix <= ix1; ++ix) {
                    const double cx0 = ix * kCellSize;
                    const double ddx = rmath::max(0.0, rmath::max(cx0 - x, x - (cx0 + kCellSize)));
                    if (ddx * ddx + ddz * ddz <= reach * reach) {
                        entries.emplace_back(cell_key(ix, iz), static_cast<std::uint32_t>(o));
                    }
                }
            }
        }
        std::sort(entries.begin(), entries.end());
        const std::size_t last = index_samples.size() - 1;
        for (std::size_t e = 0; e < entries.size();) {
            const std::int64_t key = entries[e].first;
            Group group{key, static_cast<std::uint32_t>(intervals.size()), 0};
            while (e < entries.size() && entries[e].first == key) {
                // Una racha de muestras consecutivas, alargada una muestra por cada lado.
                std::size_t run_first = entries[e].second;
                std::size_t run_last = run_first;
                ++e;
                while (e < entries.size() && entries[e].first == key &&
                       entries[e].second == run_last + 1) {
                    run_last = entries[e].second;
                    ++e;
                }
                const double s0 = sample_s_[index_samples[run_first == 0 ? 0 : run_first - 1]];
                const double s1 = sample_s_[index_samples[rmath::min(run_last + 1, last)]];
                if (group.count > 0 && intervals.back().s1 >= s0) {
                    intervals.back().s1 = s1;
                } else {
                    intervals.push_back(Interval{s0, s1});
                    ++group.count;
                }
            }
            groups.push_back(group);
        }
    };
    std::vector<Interval> far_intervals;
    std::vector<Group> far_groups;
    collect(kStageStripHalfWidth, far_intervals, far_groups);
    std::vector<Interval> near_intervals;
    std::vector<Group> near_groups;
    collect(core_max_ + kNearSafety, near_intervals, near_groups);

    intervals_ = far_intervals;
    cells_.reserve(far_groups.size());
    for (const Group& g : far_groups) {
        Cell cell;
        cell.key = g.key;
        cell.far_first = g.first;
        cell.far_count = g.count;
        cells_.push_back(cell);
    }
    const auto near_offset = static_cast<std::uint32_t>(intervals_.size());
    intervals_.insert(intervals_.end(), near_intervals.begin(), near_intervals.end());
    for (const Group& g : near_groups) {
        // La lista cercana cae dentro de la lejana (su margen es menor): la celda existe.
        const auto it = std::lower_bound(cells_.begin(), cells_.end(), g.key,
                                         [](const Cell& c, std::int64_t k) { return c.key < k; });
        if (it != cells_.end() && it->key == g.key) {
            it->near_first = near_offset + g.first;
            it->near_count = g.count;
        }
    }
    intervals_.shrink_to_fit();
    return {};
}

// --- Pasada --------------------------------------------------------------------------------------

void hash_stage_run(Fnv1a64& hash, const StageRunState& state) {
    hash.add_f64(state.s);
    hash.add_f64(state.t);
    hash.add_f64(state.reset_s);
    hash.add_f64(state.exit_s);
    hash.add_f64(state.off_travel);
    hash.add_f64(state.last_x);
    hash.add_f64(state.last_z);
    hash.add_u64(static_cast<std::uint64_t>(state.steps));
    hash.add_u64(static_cast<std::uint64_t>(state.start_step));
    hash.add_u64(static_cast<std::uint64_t>(state.finish_step));
    hash.add_u32(static_cast<std::uint32_t>(state.resets));
    hash.add_u32(static_cast<std::uint32_t>(state.shortcuts));
    hash.add_u32(static_cast<std::uint32_t>(state.split_count));
    hash.add_byte(state.on_road ? 1 : 0);
    for (const std::int64_t step : state.split_steps) {
        hash.add_u64(static_cast<std::uint64_t>(step));
    }
}

StageRun::StageRun(const StageTrack& track) : track_(&track) {
    std::memset(&state_, 0, sizeof(state_)); // relleno incluido
    const StagePose p = track.pose(0.0);
    state_.reset_s = track.s_begin();
    state_.last_x = p.x;
    state_.last_z = p.z;
    state_.start_step = -1;
    state_.finish_step = -1;
    state_.on_road = true;
}

void StageRun::placed(StagePlace why, double s, double x, double z) {
    StageRunState& st = state_;
    // Start fija reset_s: a media pasada lo haría bajar.
    RALLY_CHECK(why != StagePlace::Start || st.steps == 0,
                "StageRun::placed: Start solo antes del primer paso");
    st.s = rmath::clamp(s, track_->s_begin(), track_->s_end());
    st.t = 0.0;
    st.on_road = true;
    st.off_travel = 0.0;
    st.last_x = x;
    st.last_z = z;
    if (why == StagePlace::Start) {
        st.reset_s = st.s;
    } else if (why == StagePlace::Button) {
        ++st.resets;
    }
}

StageRun::Step StageRun::step(double x, double z) {
    const StageTrack& track = *track_;
    StageRunState& st = state_;
    const double dx = x - st.last_x;
    const double dz = z - st.last_z;
    const double d = rmath::sqrt(dx * dx + dz * dz);
    st.last_x = x;
    st.last_z = z;

    // Ventana incremental; si el mínimo cae en un borde que no es un extremo, se adquiere.
    const double lo = rmath::max(st.s - kWindowMargin, track.s_begin());
    const double hi = rmath::min(st.s + d + kWindowMargin, track.s_end());
    StageProjection proj = track.project(x, z, lo, hi);
    if ((proj.s == lo && lo > track.s_begin()) || (proj.s == hi && hi < track.s_end())) {
        proj = track.acquire(x, z);
    }

    // 1. Carretera, salida, vuelta y atajo.
    if (st.on_road && proj.on_road) {
        st.s = proj.s;
        st.t = proj.t;
    } else {
        if (st.on_road) {
            st.exit_s = st.s; // la del último paso en carretera
            st.off_travel = d;
            st.on_road = false;
        } else {
            st.off_travel += d;
        }
        StageProjection back;
        if (track.on_any_road(x, z, back)) {
            if (back.s - st.exit_s > st.off_travel + kShortcutMargin) {
                ++st.shortcuts;
                st.s = proj.s;
                st.t = proj.t;
                return Step{true, st.exit_s};
            }
            st.s = back.s;
            st.t = back.t;
            st.on_road = true;
        } else {
            st.s = proj.s;
            st.t = proj.t;
        }
    }

    // 2. Punto de recolocación, salida, parciales y meta, con reset_s.
    if (st.on_road) {
        st.reset_s = rmath::max(st.reset_s, st.s);
    }
    if (st.start_step < 0 && st.reset_s > kRunStartDistance) {
        st.start_step = st.steps;
    }
    const double length = track.axis().length();
    while (st.split_count < kMaxSplits) {
        const double split = static_cast<double>(st.split_count + 1) * kSplitSpacing;
        if (split > length - kSplitFinishGap || st.reset_s < split) {
            break;
        }
        st.split_steps[st.split_count] = st.steps;
        ++st.split_count;
    }
    if (st.finish_step < 0 && st.reset_s >= length) {
        st.finish_step = st.steps;
    }
    return Step{false, st.reset_s};
}

} // namespace rally::sim
