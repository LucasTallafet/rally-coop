#pragma once

#include "sim/car_controller.h"

#include <string>

namespace rally::platform {

// Las líneas «motor», «embrague», «ejes» y «autobl.» de F3 con campos de longitud fija: el signo de
// los pares siempre presente, las rpm con 4 cifras y los estados en texto rellenados hasta el más
// largo de su lista (con espacios y espacios duros, hasta la misma medida con text_width). Con las
// cifras medidas como 8 y el «-» como «+» (debug_text_width), cada una de las cuatro mide siempre
// lo mismo. «motor» y «embrague» son la línea del motor partida en dos (lote 5, tramo-modo): a
// 1280×720 la entera no cabía.
std::string debug_motor_line(const sim::DrivetrainOutput& d);
std::string debug_clutch_line(const sim::DrivetrainOutput& d);
std::string debug_axles_line(const sim::DrivetrainOutput& d);
// Tercera línea del bloque: autoblocantes y LCP (partida de «ejes» para que quepa a 1280×720).
std::string debug_lsd_line(const sim::DrivetrainOutput& d);

} // namespace rally::platform
