#pragma once

#include "game/notes.h"
#include "platform/cockpit.h"
#include "platform/note_panel.h"
#include "sim/car_data.h"

#include <array>
#include <vector>

namespace rally::platform {

// Geometría del panel de notas del salpicadero (lote 7, tarea panel; docs/01 §5.4): una fila de
// tres huecos, curva · distancia · curva, como geometría del interior (como la pantalla de la
// marcha de platform/cockpit), no como una capa de la pantalla. Triángulos de color plano en ejes
// del coche; sin raylib.
//
// Va a la izquierda del volante, entre la puerta y la pantalla de la marcha (Lucas, 2026-11-12):
// en el plano de esa pantalla (mismo display_ahead y display_below, de cara al ojo), con su borde
// derecho a kPanelGap de la pantalla y el izquierdo a kPanelDoorGap de la puerta. Todo sale de las
// claves de [cockpit] que ya hay; un coche sin ellas (el de referencia de los tests) no lleva
// panel.
//
// Accesibilidad (docs/01 §5.4): el color nunca va solo. Cada nota lleva su número o letra, la
// flecha está más curvada cuanto más cerrada es la curva y los peligros y los modificadores tienen
// icono propio.

inline constexpr double kPanelGap = 0.010;     // m entre el panel y la pantalla de la marcha
inline constexpr double kPanelDoorGap = 0.005; // m entre el panel y la puerta
inline constexpr double kPanelHeightScale = 1.2; // × display_height
inline constexpr double kPanelMinWidth = 0.12;   // m: más estrecho no cabe y no se dibuja
// Reparto del ancho: curva, distancia, curva.
inline constexpr std::array<double, 3> kPanelSlotShare = {0.30, 0.40, 0.30};

// El plano del panel. `valid` es falso si el coche no tiene el interior.
struct NotePanelFrame {
    bool valid = false;
    Vec3d center;  // centro del rectángulo, en ejes del coche
    Vec3d u;       // a la derecha del plano
    Vec3d v;       // arriba del plano
    double width = 0.0;
    double height = 0.0;
    // Coordenadas x de los bordes de los tres huecos en el plano (de −width/2 a width/2).
    std::array<double, 4> slot_edge{};

    Vec3d at(double x, double y) const {
        return {center.x + u.x * x + v.x * y, center.y + u.y * x + v.y * y,
                center.z + u.z * x + v.z * y};
    }
};
NotePanelFrame note_panel_frame(const sim::CarData& car);

// Añade el panel con lo que enseña `view` (vacío: el fondo y los tres huecos). No hace nada si el
// coche no tiene el interior.
void append_note_panel(std::vector<CockpitTri>& out, const sim::CarData& car, const PanelView& view);

// La escala de colores de docs/01 §5.1: 6 azul, 5 cian, 4 verde, 3 amarillo, 2 naranja, 1 rojo, H
// morado. Sin grado (peligro solo): ámbar.
CockpitColor grade_color(game::NoteGrade grade);

// Cuánto gira la flecha de la nota (rad): crece al bajar el número, hasta la horquilla (~180°).
double arrow_turn(game::NoteGrade grade);

// La flecha en coordenadas unitarias (x a la derecha, y arriba; empieza en el origen mirando
// arriba): puntos del eje (el primero es el origen) y la punta. A la izquierda, x se invierte.
inline constexpr int kArrowSegments = 10;
struct ArrowShape {
    std::array<std::array<double, 2>, kArrowSegments + 1> axis{};
    std::array<double, 2> tip{};
    double end_heading = 0.0; // rad desde arriba, + hacia el sentido de la curva
};
ArrowShape arrow_shape(game::NoteGrade grade, game::NoteSide side);

// Máscara de 7 segmentos (bit 0 a, 1 b, 2 c, 3 d, 4 e, 5 f, 6 g) de un carácter del panel
// («0»–«9», «H», «y»); 0 si no lo hay.
unsigned segment_mask(char c);

} // namespace rally::platform
