#pragma once

namespace rally::platform {

struct Vec3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// Cuaternión unitario (mismo orden que Jolt: x, y, z, w).
struct Quatd {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
};

// Estado que dibuja el render: posición y orientación de un cuerpo.
struct Pose {
    Vec3d position;
    Quatd rotation;
};

// Estado de render entre los dos últimos pasos de física. alpha en [0, 1]: 0 es `from`
// (el penúltimo paso) y 1 es `to` (el último). Posición lineal; rotación con nlerp por el
// camino más corto, normalizada. Entre dos pasos de 1/240 s el giro es tan pequeño que
// nlerp y slerp coinciden a efectos prácticos, y nlerp no necesita acos.
// Es código de render: no forma parte de la simulación.
Pose interpolate(const Pose& from, const Pose& to, double alpha);

} // namespace rally::platform
