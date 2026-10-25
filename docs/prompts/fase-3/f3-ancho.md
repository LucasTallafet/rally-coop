# Tarea `f3-ancho` (lote 4, carril plataforma): ancho mínimo del F3 y F3 por páginas en `docs/teclas.md`

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-4.md` §2.1 y §2.8, que mandan sobre este prompt.

## Por qué

En la segunda prueba del lote 3, Lucas: «el F3 tiene que tener un ancho mínimo más grande, sin dejar de ser dinámico, para que los datos no lo muevan tanto». Hoy (`platform/debug_panel.cpp`) el ancho es el de la línea más ancha de la página, medida con su texto real. `WidthHold` lo deja crecer al instante y bajar una vez por segundo, así que el panel cambia de ancho cada vez que una cifra gana o pierde un dígito o un signo.

## Qué hacer

1. **Medir con 8 en cada cifra:** el ancho de cada línea del panel se mide con sus dígitos cambiados por «8», la misma regla que ya usan las muestras de peor caso de cada capa (`layer_samples`). Se dibuja el texto real; solo cambia la medida. Así, una cifra que cambia sin cambiar de longitud no mueve el panel.
2. **Ancho mínimo** del contenido, en caracteres «8» de la fuente del momento (no en píxeles, para que escale con la resolución):
   - lo eliges con las líneas reales de la página 1 en la zona con el coche en marcha, y el plan dice de dónde sale el número;
   - a 1360×768 no puede pasar del 60 % del ancho del área del panel.

   El panel sigue a la medida por encima del mínimo, y `WidthHold` se queda.
3. **Los dos paneles:** el del juego y el de la ventana del copiloto (`copilot_app`) usan el mismo `DebugPanel`: los dos cambian.
4. **`docs/teclas.md`:** la fila de F3 dice que va por páginas («F3: página 1, 2… y oculto»). El texto sale de la tabla de teclas de la pantalla (`demo_screen_keys.cpp`): cambias la tabla y regeneras el documento, como siempre.

Reutiliza `text_width`, `layer_samples`, `WidthHold`, `plan_debug` y `debug_panel_rect`. Si escribes una utilidad nueva, el plan dice qué buscaste y por qué no sirve lo que hay.

## Criterios (tests en `tests/platform/`, Release y Debug)

1. **Cifras que cambian:** un juego de líneas en el que solo cambian los dígitos, sin cambiar la longitud, da siempre el mismo ancho de panel. Por ejemplo, «v 12.3 km/h» frente a «v 99.9 km/h», y cifras de 1 a 9 en todas las posiciones.
2. **Mínimo:** con líneas más cortas que el mínimo, el panel mide el mínimo. Con una más larga, crece hasta ella, y `WidthHold` sigue igual (crece al instante y baja como mucho una vez por segundo).
3. **Tope:** a 1280×720, 1360×768 y 1920×1080, el mínimo cabe en el área del panel y no pasa del 60 % de su ancho a 1360×768.
4. **Capas:** sin solapes en las tres resoluciones (los tests de `screen_layout` de hoy siguen pasando).
5. **`docs/teclas.md`** coincide con la tabla generada (el test de siempre).

**Sin cambios:** la simulación, las grabaciones y el resto de capas.

## Para el informe

- El mínimo elegido (en caracteres y en píxeles a 1360×768 y a 1920×1080) y de dónde sale.
- Para la prueba de Lucas: F3 en la zona conduciendo y en el copiloto, a 1360×768 y a 1920×1080; el panel no cambia de ancho al cambiar las cifras. No hay A/B: se compara con el recuerdo del lote 3 (el cambio es visual y pequeño).
