#pragma once

#include "platform/cockpit.h"
#include "platform/interpolation.h"

#include <vector>

namespace rally::platform {

// Dibuja los triángulos del interior (platform/cockpit, en ejes del coche) con la pose del coche
// del fotograma, relativos a `origin` (render relativo). Un solo lote de rlgl (y un cambio de
// estado para no descartar caras): pocas llamadas de dibujo. Cada triángulo se aclara según su
// normal frente a una luz fija en ejes del coche, para ver la forma sin iluminación de la GPU.
void draw_cockpit(const std::vector<CockpitTri>& tris, const Pose& car_pose, const Vec3d& origin);

} // namespace rally::platform
