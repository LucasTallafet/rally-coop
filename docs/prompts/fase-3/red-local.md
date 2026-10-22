# Tarea `red-local` (lote 3, carril juego): biblioteca de red con ENet

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-3.md` §2.5 y §2.6, que mandan sobre este prompt.

## Por qué

La fase 3 pide una «segunda instancia conectada por localhost con ENet, host autoritativo, interpolación por número de paso» (`04`). Esta tarea hace la biblioteca y sus tests. La ventana del copiloto y lo que hace el juego del host son de `copiloto-ventana` (oleada 2, carril plataforma), que usará esta interfaz tal cual.

## Qué hacer

**`src/net/CMakeLists.txt`:**
- `rally_net` STATIC;
- enlaza `rally_sim` (PUBLIC, para leer `SimWorld`) y `enet` (PRIVATE). No hace falta `rally_io`: el host recibe el mundo ya serializado y el copiloto lo devuelve en bytes; `serialize_world` y `parse_world` las llama la plataforma;
- con `NOMINMAX` y `WIN32_LEAN_AND_MEAN` como definiciones PRIVATE y `rally_warnings` PRIVATE.

**`tests/net/CMakeLists.txt`:** `rally_add_test_executable(rally_tests_net …)` más `rally_net`. El integrador ya dejó en el paso 1 los `add_subdirectory` y las reglas de `CheckBuildRules`.

**Interfaz** (los nombres pueden cambiar en el plan; lo que hace cada pieza, no):

```cpp
namespace rally::net {
inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::uint16_t kDefaultPort = 29470;
inline constexpr int kStateEverySteps = 8;            // 30 Hz a 240 Hz
inline constexpr double kInterpolationDelay = 0.100;  // s (24 pasos)
inline constexpr double kMaxExtrapolation = 0.100;    // s; después, quieto
inline constexpr std::size_t kCarStateBytes = 47;

struct CarState {  // decodificado (§2.6 del lote)
    std::uint8_t generation = 0;
    std::uint32_t step = 0;
    double x = 0, y = 0, z = 0;                    // m
    double qx = 0, qy = 0, qz = 0, qw = 1;
    float vx = 0, vy = 0, vz = 0;                  // m/s
    std::array<float, 4> compression{};            // m
    std::array<std::int8_t, 4> surface{};          // sim::Surface, −1 en el aire
    std::int8_t gear = 0;
    float rpm = 0;
    bool reset = false;                            // recolocado después del estado anterior
};
CarState car_state_from(const sim::SimWorld& world, std::uint32_t step,
                        std::uint8_t generation, bool reset);   // solo lee; telemetría encendida (RALLY_CHECK)
void encode(const CarState& state, std::span<std::uint8_t, kCarStateBytes> out);
bool decode(std::span<const std::uint8_t> in, CarState& out);  // false si el tamaño o el tipo no cuadran

struct Pose { double x, y, z, qx, qy, qz, qw; };
struct BufferStats { std::uint64_t received, missing; double delay_s; bool extrapolating; std::uint32_t host_step; };

class StateBuffer {            // copiloto: anillo de tamaño fijo, sin reservas en push ni sample
public:
    void reset();                                        // mundo nuevo
    void push(const CarState& state, double arrival_s);  // descarta duplicados, viejos y de otra generación
    bool sample(double now_s, Pose& out) const;          // false si aún no hay ningún estado
    BufferStats stats() const;
};

struct Impairment { double delay_s = 0, jitter_s = 0, loss = 0; };   // red simulada (Z en el copiloto)
struct WorldInfo { std::uint8_t generation; std::uint64_t zone_hash; std::string world_bytes; std::string car_text; };
struct LinkStats { bool connected; double rtt_s; std::uint64_t sent, received; };

class Host {                   // el juego que conduce
public:
    bool start(std::uint16_t port, std::string& error);  // 127.0.0.1; port 0 = uno libre (tests)
    std::uint16_t port() const;
    void set_world(std::string world_bytes, std::string car_text, std::uint64_t zone_hash);  // generación + 1
    void after_steps(const sim::SimWorld& world, std::uint32_t step, bool reset_since_last);  // envía si cruza un múltiplo de 8
    void service();            // una vez por fotograma
    bool copilot_connected() const;
    LinkStats stats() const;
};

class Copilot {
public:
    bool connect(std::string_view address, std::uint16_t port, std::string& error);
    void service(double now_s);                    // recibe; sin conexión, reintenta cada 1 s
    std::optional<WorldInfo> take_world();         // mundo nuevo para construir (una vez cada uno)
    const StateBuffer& buffer() const;
    void set_impairment(const Impairment& impairment, std::uint64_t seed);  // PCG32; afecta a lo que se recibe
    LinkStats stats() const;
    std::string_view status() const;               // «conectado», «sin conexión, reintentando», el motivo del rechazo…
};
}
```

**Detalles fijados:**
- **Rotación**: «las tres más pequeñas». La componente mayor se omite y se hace positiva (q y −q son la misma), y las otras tres van en i16 con escala 32767·√2. Al decodificar, la omitida se recupera con `rmath::sqrt`.
- **Paso estimado del host en el copiloto:** `step_est(now) = now·240 − offset`, con `offset` = el mínimo de (llegada·240 − paso) de los estados de los últimos 2 s. Objetivo: `step_est(now) − 24`.
  - Interpolación lineal de la posición y nlerp de la rotación entre los dos estados que rodean el objetivo.
  - Si no hay estado posterior: extrapolación con la velocidad hasta `kMaxExtrapolation` y después quieto.
  - Con `reset` en el estado posterior, se toma ese estado sin mezclar.
- **Red simulada:** el copiloto retiene cada paquete recibido hasta llegada + delay + U(−jitter, +jitter) y descarta una fracción `loss`. Usa PCG32 sembrado y un anillo fijo de 256 paquetes, sin reservas. Solo afecta al canal no fiable; el fiable llega siempre.
- **El copiloto también se ata a `127.0.0.1`** (puerto 0) y no a todas las interfaces (`enet_host_create(NULL, …)` lo haría). Así Windows no pregunta por el cortafuegos en ninguna de las dos instancias.
- **Sin reloj en `src/net`:** el tiempo llega en `now_s`. Lo exige `criterios-revision.md` §4.1, aunque `CheckSimRules` dejaría usarlo en la red.
- **Nada de `enet.h` ni `<windows.h>` en los encabezados públicos** (`lote-3.md` §2.6). `enet_initialize` y `enet_deinitialize`, una sola vez por proceso; por ejemplo, un objeto estático o uno que posee la plataforma. Dilo en el plan.

## Criterios (tests en `tests/net/`, Release y Debug)

1. **Codificación:**
   - el estado ocupa exactamente 47 B;
   - ida y vuelta con 10 000 estados PCG32 (posición ±20 km, rotación aleatoria, velocidad ±60 m/s): errores dentro de la tabla de `lote-3.md` §2.6 (0,5 mm, 1·10⁻⁴ rad, 0,5 cm/s, 0,05 mm, 0,5 rpm);
   - las cuatro componentes omitidas posibles y el signo de q;
   - `decode` rechaza los tamaños de 0 a 46 y 48, y los tipos distintos de 1.
2. **Interpolación sin red simulada.** Estados a 30 Hz de un coche en un círculo de R 50 m a 20 m/s, con llegada = paso/240 + 5 ms, muestreados a 60 fps: error de posición ≤ 1 cm frente a la trayectoria real en el instante objetivo (la cuerda da ~1,1 mm).
3. **Con red simulada media** (100 ± 30 ms, 5 % de pérdida, semilla fija) durante 60 s:
   - **Muestras que interpolan:** error ≤ 3 cm (la cuerda de un hueco de 4 estados da 1,8 cm).
   - **Muestras que extrapolan** (el estado siguiente al hueco aún no ha llegado): error ≤ 8 cm. Es la tangente durante 100 ms a 20 m/s en R 50: (v·t)²/(2R) = 4 cm, más el resto.
   - El ángulo recorrido en el círculo nunca baja entre dos muestras.
   - El informe da el hueco más largo y cuántas muestras extrapolan.
4. **Recolocar:** un estado con `reset` salta exacto, sin mezclar con el anterior.
5. **Generación:** tras un mundo nuevo, los estados de la generación vieja se descartan. Prueba la vuelta de 255 a 0.
6. **Extrapolación:** sin estados nuevos, la posición sigue con la velocidad 100 ms y después se queda quieta.
7. **ENet de verdad por `127.0.0.1`,** con `Host::start(0)` y el puerto que da `port()`, en un solo proceso; el test reparte `service()` hasta un máximo de 5 s por fase:
   - el saludo acepta;
   - el mundo llega con los bytes exactos y su `zone_hash`;
   - 300 estados llegan en orden, con como mucho 3 perdidos;
   - con otra versión del protocolo, respuesta «aceptado = 0» y el motivo;
   - un segundo copiloto, rechazado;
   - al cerrar el copiloto y volver a conectar, se acepta otra vez.
8. **El host no cambia la simulación:** 2400 pasos con `after_steps` en cada fotograma y sin él dan el mismo `state_hash`.
9. **Sin reservas** en `StateBuffer::push` y `sample` (0 `operator new` en 10 000 llamadas). ENet sí reserva al enviar y recibir: el informe dice cuántas veces por segundo en el host, medido con el contador de `operator new`, y si hace falta algo en la fase 5.

**Sin cambios:** `kSimVersion`, huella, reservas de Jolt, telemetría, CSV y `.rrec`. No tocas `src/io`. Si te falta algo de ahí, para y dilo; `serialize_world` y `parse_world` ya existen.

## Regímenes del copiloto

Cada uno con su test:
- sin conexión todavía;
- conectado sin mundo;
- con mundo y sin estados;
- con estados;
- hueco por pérdida;
- recolocar;
- mundo nuevo;
- host que se va (pasa a «sin conexión, reintentando»).

## Para el informe

- Tamaño del mensaje de mundo con la zona y con el recinto.
- Reservas de ENet por segundo en el host.
- El hueco más largo del criterio 3.
- Nada que probar jugando en esta tarea: la prueba es la de `copiloto-ventana`.
