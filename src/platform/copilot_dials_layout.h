#pragma once

#include "platform/copilot_wheel.h"
#include "platform/screen_layout.h"

namespace rally::platform {

// Dónde cae cada parte de los diales dentro de sus dos columnas (Layer::DialsLeft y DialsRight),
// calculado sin raylib para probar que cabe y que no se pisa, y para que el ratón sepa qué hay bajo
// el puntero. Izquierda: el lado en grande, la rueda del dial principal (14 posiciones: media rueda
// a cada lado), el botón de borrar y las dos líneas de «última orden» y «pendientes». Derecha: la
// forma de la distancia, la rueda del secundario y, debajo, la de distancias o el contador.

struct DialCircle {
    int cx = 0;
    int cy = 0;
    int r = 0;
};

struct CounterGeometry {
    UiRect display; // los metros
    UiRect minus;
    UiRect plus;
    UiRect sing; // [cantar]
};

struct DialsGeometry {
    // Columna izquierda
    UiRect side_label;
    DialCircle main;
    UiRect erase;
    UiRect last_line;
    UiRect pending_line;
    // Columna derecha
    UiRect form_button; // «distancia: rueda / contador», se pulsa para cambiar
    UiRect secondary_title;
    DialCircle secondary;
    UiRect distance_title;
    DialCircle distance; // la rueda
    CounterGeometry counter;
};

DialsGeometry dials_geometry(const UiRect& left, const UiRect& right, const UiMetrics& m);

bool inside(const UiRect& r, double x, double y);
bool inside(const DialCircle& c, double x, double y);

struct DialPoint {
    int x = 0;
    int y = 0;
};
// Dónde se escribe la posición `index` de una rueda: a `radial` (0..1) del radio, en su ángulo. Con
// la media rueda (principal), `side` = −1 la dibuja a la izquierda y +1 a la derecha.
DialPoint dial_item_point(const DialCircle& circle, const WheelSpec& spec, int index, int side,
                          float radial);

// Vector de elección de un punto de la pantalla respecto al centro de la rueda (x a la derecha,
// y arriba, módulo 1 en el borde).
void dial_vector(const DialCircle& circle, double px, double py, float& x, float& y);

} // namespace rally::platform
