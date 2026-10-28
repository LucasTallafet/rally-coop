// Mirar con la cabeza (lote 4, mirar-cabeza): el estado HeadLook, el giro del cuello y la
// composición con F9.

#include "new_counter.h"
#include "platform/head_look.h"
#include "platform/head_motion.h"
#include "platform/input_shaping.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

using namespace rally::platform;

namespace {

constexpr double kBig = 10.0; // s: el seguimiento ha llegado (e^-200)

// Un fotograma con el stick crudo (x derecha, y arriba) pasado por la forma del mando.
HeadLookInput stick(float x, float y, double dt_s = kBig) {
    const LookStick shaped = shape_look_stick(x, y);
    HeadLookInput input;
    input.stick_x = shaped.x;
    input.stick_y = shaped.y;
    input.dt_s = dt_s;
    return input;
}

HeadLookInput mouse(float dx, float dy, double dt_s = kBig) {
    HeadLookInput input;
    input.mouse_dx_px = dx;
    input.mouse_dy_px = dy;
    input.dt_s = dt_s;
    return input;
}

constexpr double deg(double rad) { return rad / kDegree; }

} // namespace

TEST_CASE("head look: the stick sets the angle in position, inside the deadzone nothing") {
    HeadLook look;
    look.update(stick(0.14f, 0.05f));
    CHECK(look.yaw() == 0.0);
    CHECK(look.pitch() == 0.0);
    // A fondo: 90° (stick a la derecha = mira a la derecha = guiñada negativa), +15° y −25°.
    look.update(stick(1.0f, 0.0f));
    CHECK(deg(look.yaw()) == doctest::Approx(-90.0).epsilon(1e-9));
    look.update(stick(-1.0f, 0.0f));
    CHECK(deg(look.yaw()) == doctest::Approx(90.0).epsilon(1e-9));
    look.update(stick(0.0f, 1.0f));
    CHECK(std::abs(deg(look.yaw())) <= 1e-9);
    CHECK(deg(look.pitch()) == doctest::Approx(15.0).epsilon(1e-9));
    look.update(stick(0.0f, -1.0f));
    CHECK(deg(look.pitch()) == doctest::Approx(-25.0).epsilon(1e-9));
    // Lineal entre la zona muerta y el tope, en cada eje (a mitad: 0,575).
    look.update(stick(-0.575f, 0.0f));
    CHECK(deg(look.yaw()) == doctest::Approx(45.0).epsilon(1e-6));
    look.update(stick(0.0f, 0.575f));
    CHECK(deg(look.pitch()) == doctest::Approx(7.5).epsilon(1e-6));
    look.update(stick(0.0f, -0.575f));
    CHECK(deg(look.pitch()) == doctest::Approx(-12.5).epsilon(1e-6));
    // Diagonal: cada eje según su componente, con los mismos topes.
    const float d = 0.575f / std::sqrt(2.0f);
    look.update(stick(-d, d));
    CHECK(deg(look.yaw()) == doctest::Approx(45.0 / std::sqrt(2.0)).epsilon(1e-6));
    CHECK(deg(look.pitch()) == doctest::Approx(7.5 / std::sqrt(2.0)).epsilon(1e-6));
    look.update(stick(-1.0f, -1.0f)); // esquina: módulo recortado a 1
    CHECK(std::abs(deg(look.yaw())) <= 90.0 + 1e-9);
    CHECK(deg(look.pitch()) >= -25.0 - 1e-9);
}

TEST_CASE("head look: first order tracking is the same at 30 and 60 fps, to 1e-9 rad") {
    HeadLook slow;
    HeadLook fast;
    // Stick a fondo 0,5 s y soltado otros 0,5 s; los fotogramas de 1/30 y 1/60 coinciden cada 1/30.
    double worst = 0.0;
    for (int frame = 0; frame < 30; ++frame) {
        const float x = frame < 15 ? -1.0f : 0.0f;
        slow.update(stick(x, 0.0f, 1.0 / 30.0));
        fast.update(stick(x, 0.0f, 1.0 / 60.0));
        fast.update(stick(x, 0.0f, 1.0 / 60.0));
        worst = std::max(worst, std::abs(slow.yaw() - fast.yaw()));
    }
    CHECK(worst <= 1e-9);
    CHECK(std::abs(fast.yaw()) > 0.0); // no es un 0 trivial
    // Y con la fórmula cerrada: tras t segundos hacia 90°, 90°·(1 − e^(−t/τ)).
    HeadLook one;
    for (int i = 0; i < 6; ++i) {
        one.update(stick(-1.0f, 0.0f, 1.0 / 60.0));
    }
    CHECK(one.yaw() ==
          doctest::Approx(kLookYawMax * (1.0 - std::exp(-0.1 / kLookTau))).epsilon(1e-12));
}

TEST_CASE("head look: released from 90 degrees it is under 1 degree in 0.25 s") {
    for (const double fps : {30.0, 60.0}) {
        HeadLook look;
        look.update(stick(-1.0f, 0.0f));
        REQUIRE(deg(look.yaw()) == doctest::Approx(90.0));
        double t = 0.0;
        while (std::abs(deg(look.yaw())) >= 1.0 && t < 1.0) {
            look.update(stick(0.0f, 0.0f, 1.0 / fps));
            t += 1.0 / fps;
        }
        INFO("fps ", fps, " t ", t);
        CHECK(t <= 0.25);
        CHECK(t > 0.2); // 0,05·ln 90 = 0,225 s
    }
}

TEST_CASE("head look: the mouse is 0.15 degrees per pixel with the same limits") {
    HeadLook look;
    look.update(mouse(100.0f, 0.0f)); // a la derecha: 15° a la derecha
    CHECK(deg(look.yaw()) == doctest::Approx(-15.0).epsilon(1e-9));
    look.update(mouse(-200.0f, 0.0f)); // 30° a la izquierda: 15° a la izquierda
    CHECK(deg(look.yaw()) == doctest::Approx(15.0).epsilon(1e-9));
    look.update(mouse(0.0f, 100.0f)); // abajo (y de pantalla positivo): 15° abajo
    CHECK(deg(look.pitch()) == doctest::Approx(-15.0).epsilon(1e-9));
    look.update(mouse(0.0f, -200.0f)); // arriba 30°: tope de 15°
    CHECK(deg(look.pitch()) == doctest::Approx(15.0).epsilon(1e-9));
    look.update(mouse(0.0f, 1000.0f)); // tope abajo: −25°
    CHECK(deg(look.pitch()) == doctest::Approx(-25.0).epsilon(1e-9));
    look.update(mouse(-5000.0f, 0.0f)); // tope a la izquierda: 90°
    CHECK(deg(look.yaw()) == doctest::Approx(90.0).epsilon(1e-9));
    look.update(mouse(10000.0f, 0.0f));
    CHECK(deg(look.yaw()) == doctest::Approx(-90.0).epsilon(1e-9));
    // No vuelve solo: sin movimiento se queda.
    look.update(mouse(0.0f, 0.0f));
    CHECK(deg(look.yaw()) == doctest::Approx(-90.0).epsilon(1e-9));
    // El botón central lo devuelve al frente con el mismo seguimiento.
    HeadLookInput center = mouse(0.0f, 0.0f, 0.05);
    center.recenter = true;
    HeadLook half;
    half.update(mouse(-600.0f, 0.0f)); // 90°
    half.update(center);
    CHECK(deg(half.yaw()) == doctest::Approx(90.0 * std::exp(-1.0)).epsilon(1e-9));
    center.dt_s = kBig;
    center.recenter = false;
    half.update(center);
    CHECK(std::abs(half.yaw()) <= 1e-9);
    // Pasarse del tope y volver responde en cuanto se vuelve (no hay que recorrer lo sobrante).
    HeadLook over;
    over.update(mouse(-100000.0f, 0.0f));
    over.update(mouse(100.0f, 0.0f));
    CHECK(deg(over.yaw()) == doctest::Approx(75.0).epsilon(1e-9));
}

TEST_CASE("head look: the stick wins while it is out of its deadzone, then back to the mouse") {
    HeadLook look;
    look.update(mouse(-200.0f, 0.0f)); // 30° a la izquierda con el ratón
    REQUIRE(deg(look.yaw()) == doctest::Approx(30.0));
    HeadLookInput both = stick(1.0f, 0.0f); // stick a fondo a la derecha, y el ratón se mueve
    both.mouse_dx_px = -100.0f;             // otros 15° a la izquierda con el ratón
    look.update(both);
    CHECK(deg(look.yaw()) == doctest::Approx(-90.0).epsilon(1e-9)); // manda el stick
    look.update(stick(0.0f, 0.0f)); // al soltarlo, vuelve a donde dejó el ratón (45°)
    CHECK(deg(look.yaw()) == doctest::Approx(45.0).epsilon(1e-9));
    // Sin haber usado el ratón, al frente.
    HeadLook fresh;
    fresh.update(stick(-1.0f, 1.0f));
    fresh.update(stick(0.0f, 0.0f));
    CHECK(std::abs(fresh.yaw()) <= 1e-9);
    CHECK(std::abs(fresh.pitch()) <= 1e-9);
}

TEST_CASE("head look: frozen, neither stick nor mouse move it, bit for bit, and it resumes") {
    HeadLook look;
    look.update(stick(-0.7f, 0.4f, 0.03));
    const double yaw = look.yaw();
    const double pitch = look.pitch();
    look.set_frozen(true);
    CHECK(look.frozen());
    HeadLookInput moving = stick(1.0f, -1.0f, 0.5);
    moving.mouse_dx_px = 300.0f;
    moving.mouse_dy_px = -80.0f;
    moving.recenter = true;
    for (int i = 0; i < 20; ++i) {
        look.update(moving);
    }
    CHECK(std::bit_cast<std::uint64_t>(look.yaw()) == std::bit_cast<std::uint64_t>(yaw));
    CHECK(std::bit_cast<std::uint64_t>(look.pitch()) == std::bit_cast<std::uint64_t>(pitch));
    look.set_frozen(false);
    look.update(stick(-0.7f, 0.4f, 0.03)); // sigue desde donde estaba, hacia el mismo objetivo
    CHECK(look.yaw() > yaw);
    // Lo que el ratón movió congelado se descartó: soltando el stick vuelve al frente.
    look.update(stick(0.0f, 0.0f));
    CHECK(std::abs(look.yaw()) <= 1e-9);
}

TEST_CASE("head look: the neck pivots 0.10 m behind the eye") {
    const Vec3d none = neck_eye_offset(0.0, 0.0);
    CHECK(none.x == 0.0);
    CHECK(none.y == 0.0);
    CHECK(none.z == 0.0);
    const Vec3d left = neck_eye_offset(kLookYawMax, 0.0); // 90° a la izquierda
    CHECK(std::abs(left.x - (-0.10)) <= 1e-9);            // a la izquierda del coche (x < 0)
    CHECK(std::abs(left.y) <= 1e-9);
    CHECK(std::abs(left.z - 0.10) <= 1e-9); // hacia atrás (z > 0)
    const Vec3d right = neck_eye_offset(-kLookYawMax, 0.0);
    CHECK(std::abs(right.x - 0.10) <= 1e-9);
    CHECK(std::abs(right.z - 0.10) <= 1e-9);
    // 50° a la izquierda (el salpicadero desde el copiloto): 7,7 cm a la izquierda, 3,6 atrás.
    const Vec3d dash = neck_eye_offset(50.0 * kDegree, 0.0);
    CHECK(dash.x == doctest::Approx(-0.0766).epsilon(1e-3));
    CHECK(dash.z == doctest::Approx(0.0357).epsilon(1e-3));
    // El ojo siempre está a 0,10 m del pivote.
    for (const double yaw : {0.3, -1.2, 1.5}) {
        for (const double pitch : {-0.4, 0.0, 0.26}) {
            const Vec3d o = neck_eye_offset(yaw, pitch);
            CHECK(std::sqrt(o.x * o.x + o.y * o.y + (o.z - 0.1) * (o.z - 0.1)) ==
                  doctest::Approx(0.10).epsilon(1e-12));
        }
    }
}

TEST_CASE("head look: the view is the old F9 camera when the head looks ahead") {
    HeadOffset f9;
    f9.forward = 0.01;
    f9.right = -0.02;
    f9.up = 0.005;
    f9.pitch = 1.5 * kDegree;
    f9.roll = -1.2 * kDegree;
    const HeadView view = head_view(0.0, 0.0, &f9);
    // La fórmula de demo_camera antes de la mirada.
    const double cp = std::cos(f9.pitch);
    const double sp = std::sin(f9.pitch);
    const double rc = std::cos(f9.roll);
    const double rs = std::sin(f9.roll);
    CHECK(view.eye_offset.x == f9.right);
    CHECK(view.eye_offset.y == f9.up);
    CHECK(view.eye_offset.z == -f9.forward);
    CHECK(std::abs(view.forward.x) <= 1e-15);
    CHECK(view.forward.y == doctest::Approx(sp).epsilon(1e-15));
    CHECK(view.forward.z == doctest::Approx(-cp).epsilon(1e-15));
    CHECK(view.up.x == doctest::Approx(rs).epsilon(1e-15));
    CHECK(view.up.y == doctest::Approx(cp * rc).epsilon(1e-15));
    CHECK(view.up.z == doctest::Approx(sp * rc).epsilon(1e-15));
    // Sin F9 y sin mirada, la cámara de siempre.
    const HeadView plain = head_view(0.0, 0.0, nullptr);
    CHECK(plain.eye_offset.x == 0.0);
    CHECK(plain.eye_offset.z == 0.0);
    CHECK(plain.forward.z == -1.0);
    CHECK(plain.up.y == 1.0);
}

TEST_CASE("head look: the view is orthonormal and looks where it says") {
    HeadOffset f9;
    f9.pitch = 2.0 * kDegree;
    f9.roll = 2.0 * kDegree;
    const HeadOffset* const heads[] = {nullptr, &f9};
    for (const HeadOffset* h : heads) {
        for (const double yaw : {-1.5, -0.4, 0.0, 0.9, 1.5}) {
            for (const double pitch : {-0.43, 0.0, 0.26}) {
                const HeadView v = head_view(yaw, pitch, h);
                const double f = v.forward.x * v.forward.x + v.forward.y * v.forward.y +
                                 v.forward.z * v.forward.z;
                const double u = v.up.x * v.up.x + v.up.y * v.up.y + v.up.z * v.up.z;
                const double fu =
                    v.forward.x * v.up.x + v.forward.y * v.up.y + v.forward.z * v.up.z;
                CHECK(f == doctest::Approx(1.0).epsilon(1e-12));
                CHECK(u == doctest::Approx(1.0).epsilon(1e-12));
                CHECK(std::abs(fu) <= 1e-12);
            }
        }
    }
    // A la izquierda, la mirada tiene x < 0; arriba, y > 0.
    const HeadView left = head_view(0.5, 0.2, nullptr);
    CHECK(left.forward.x < 0.0);
    CHECK(left.forward.y > 0.0);
}

TEST_CASE("head look: no allocations per frame") {
    HeadLook look;
    const std::uint64_t before = rally::tests::global_new_count();
    double sum = 0.0;
    for (int frame = 0; frame < 20000; ++frame) {
        HeadLookInput input = frame % 3 == 0
                                  ? stick(-0.9f, 0.5f, 1.0 / 60.0)
                                  : mouse(static_cast<float>(frame % 7) - 3.0f, 1.0f, 1.0 / 60.0);
        input.recenter = frame % 1000 == 999;
        look.set_frozen(frame % 500 > 450);
        look.update(input);
        const HeadView v = head_view(look.yaw(), look.pitch(), nullptr);
        sum += v.forward.x + neck_eye_offset(look.yaw(), look.pitch()).z;
    }
    CHECK(rally::tests::global_new_count() == before);
    CHECK(std::isfinite(sum));
}
