#pragma once

#include "platform/interpolation.h"
#include "sim/car_data.h"
#include "sim/sim_world.h"

#include <raylib.h>

#include <array>
#include <cstddef>

namespace rally::platform {

// Dibujo de los cuerpos y del coche de la escena (low-poly, colores planos), dentro de
// BeginMode3D. Lo comparten el juego (demo_world) y la ventana del copiloto (copilot_app), para que
// los dos dibujen igual. Todo se dibuja relativo a `origin` (el ojo, o 0 con el render en float).

// Color del cuerpo `index` de la descripción (el suelo, los muros y los cuerpos dinámicos).
Color body_color(const sim::WorldDesc& desc, std::size_t index);

// Caja con sombreado plano por cara.
void draw_box(const Pose& pose, const sim::BoxDesc& box, Color color, const Vec3d& origin);

// Recinto de la fase 1: rejilla de 1 m sobre el suelo y postes junto a los muros.
void draw_floor_grid(const sim::BoxDesc& floor, const Vec3d& origin);
void draw_posts(const sim::BoxDesc& floor, const Vec3d& origin);

// Coche: la caja de la carrocería y cada rueda como un cilindro de 12 lados con una marca que gira.
void draw_car(const Pose& car_pose, const sim::CarData& car,
              const std::array<sim::SimWorld::WheelPose, sim::kWheelCount>& wheels,
              const Vec3d& origin);

} // namespace rally::platform
