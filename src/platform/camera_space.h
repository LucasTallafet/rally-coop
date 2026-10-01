#pragma once

#include "platform/interpolation.h"

namespace rally::platform {

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Render relativo a la cámara: la posición del mundo y la de la cámara se restan en doble y
// solo la diferencia, pequeña cerca de la cámara, pasa a float. Así lo cercano conserva la
// precisión aunque el mundo esté a kilómetros del origen. Restar después de pasar cada una
// a float pierde hasta un paso del float del mundo (1,95 mm a 20 km).
// Es código de render: no forma parte de la simulación.
Vec3f relative_to_camera(const Vec3d& world, const Vec3d& camera);

// v girado por el cuaternión unitario q.
Vec3d rotate(const Quatd& q, const Vec3d& v);

// Pose de algo sujeto a un cuerpo: posición del cuerpo más el desplazamiento local girado
// con él, y la misma orientación.
Pose attach(const Pose& body, const Vec3d& local_offset);

} // namespace rally::platform
