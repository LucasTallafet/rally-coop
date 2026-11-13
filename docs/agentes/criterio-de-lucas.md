# Rally Coop — Criterio de Lucas

Escrito por el consultor el 2026-11-12, con la entrevista de ese día (`mensajes/lote-7-entrevista.md`) y las pruebas de los lotes 5 y 6. Lo leen `revisor-diseno` y los carriles.

**Una decisión de gusto que esté cubierta aquí no es una parada:** el carril decide según este documento y lo dice en su informe. Manda `05-decisiones.md` si hay contradicción. Solo lo cambia el consultor.

## 1. Lo que deciden los agentes solos

- Valores sencillos y concretos: colores, valores por defecto, textos de pantalla, tamaños, tiempos de animación. El agente elige el que crea mejor y lo pone en el informe con su valor. Lucas hará un ajuste fino más adelante.
- Las pruebas de lote llevan las preguntas que necesite cada lote: ni se recortan ni se rellenan.

## 2. Lo que siempre decide Lucas

- Cómo se siente el coche, lo que se ve y lo que se oye, cuando el cambio se nota jugando.
- Todo lo que cambie `01`–`05`.

## 3. El coche (para la 2.10; nadie lo ajusta antes del diagnóstico)

- «Pesa demasiado poco» quiere decir esto: al entrar en una curva algo pasado de velocidad, cuesta bajar la velocidad sin perder la trayectoria. El balanceo y el hundimiento del morro al frenar están bien.
- El freno motor retiene poco.
- En el final del derrape falta control del morro respecto al eje trasero. Quiere algo más de control, no mucho: los trompos por ir demasiado rápido tienen que seguir ocurriendo.
- El control de tracción le gusta como está.
- La sensibilidad de dirección que usa está en torno al 60 %.
- La vibración del mando es demasiada.
- El sonido del motor suena «como una moto». Se afina en la fase 6.
- Las cuatro configuraciones siguen hasta el primer tramo de verdad.

## 4. El tramo y el suelo

- El tramo de 3–5 km: mezcla, con prioridad a la montaña, el bosque y la tierra. Al principio, tramos cortos; después, de todo tipo.
- Salirse castiga: cunetas, árboles, muros y piedras. A veces esos elementos castigan también el atajar una curva.
- La irregularidad se tiene que notar: baches, bordes rotos, peralte que cambia y ondulaciones largas. El asfalto en mal estado es irregularidad sobre el asfalto, no una superficie nueva.
- La calzada de 6 m vale por ahora.
- Los dientes de sierra en los bordes entre superficies y las siluetas dentadas se ven «de juego mal hecho»: un borde entre dos superficies se dibuja con su geometría exacta.

## 5. Tablet, notas y panel

- Orientación del mapa: hacia delante. El retraso de ~6° del rumbo en las horquillas no molesta. El zoom de 100, 200 y 400 m cambia bien.
- Notas: números del 1 al 6 y la H, el sentido de la curva, avisos críticos y la distancia después de la curva.
- La distancia: se prueban dos formas, conmutables. La rueda de 8 valores de `01` §5.3, y un contador con los metros y tres botones, [−], [cantar] y [+], que parte de 50 m y cambia de 10 en 10. El juego nunca rellena la distancia real.
- Diales con mando, y con ratón y teclado. La pantalla táctil queda fuera de la 1.0.
- Panel del salpicadero: a la izquierda del volante, entre la puerta y el volante. Se ajusta jugando.
- Sonido por lado: pitidos, de momento.
- En la cinta y el perfil echa en falta, para más adelante, los avisos críticos.

## 6. Prioridades

- Lo que más le importa de la 1.0 es el generador de tramos: rico, creíble y ligeramente denso. No se adelanta: la fase 3 se valida antes jugando a dos.
- La puntuación importará cuando el juego sea divertido y jugable.
- Rendimiento: ajustes gráficos, y 60 fps en bajo en su PC como mínimo.
- Estilo visual, para la fase 6: low-poly facetado con relieve, rocas, árboles y montañas de fondo.
- Su compañero empieza a probar cuando exista el primer tramo de verdad.
