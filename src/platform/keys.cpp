#include "platform/keys.h"

namespace rally::platform {

namespace {

constexpr KeyChord k(Key key) { return KeyChord{key, false}; }
constexpr KeyChord alt(Key key) { return KeyChord{key, true}; }

constexpr std::uint8_t kZone = kContextZone;
constexpr std::uint8_t kBlind = kContextBlind;
constexpr std::uint8_t kArena = kContextArena;
constexpr std::uint8_t kAll = kContextAll;

// Orden: el de los bloques y, dentro de cada uno, el de la ayuda y el documento.
constexpr KeyBinding kTable[] = {
    // Conducir
    {Action::Throttle,
     {k(Key::W), k(Key::Up)},
     Action::None,
     {},
     PadControl::RightTrigger,
     kAll,
     KeyBlock::Drive,
     "Acelerador",
     ""},
    {Action::Brake,
     {k(Key::S), k(Key::Down)},
     Action::None,
     {},
     PadControl::LeftTrigger,
     kAll,
     KeyBlock::Drive,
     "Freno",
     ""},
    {Action::SteerLeft,
     {k(Key::A), k(Key::Left)},
     Action::SteerRight,
     {k(Key::D), k(Key::Right)},
     PadControl::LeftStickX,
     kAll,
     KeyBlock::Drive,
     "Girar",
     ""},
    {Action::Handbrake,
     {k(Key::Space), {}},
     Action::None,
     {},
     PadControl::FaceSouth,
     kAll,
     KeyBlock::Drive,
     "Freno de mano",
     "ruedas traseras; mientras se tira, sin par motor"},
    {Action::ShiftDown,
     {k(Key::Q), {}},
     Action::None,
     {},
     PadControl::LeftShoulder,
     kAll,
     KeyBlock::Drive,
     "Bajar marcha",
     "caja R, N, 1 a 5: de 1 a N (en automático, solo parado) y de N a R parado (menos de "
     "2 km/h); en secuencial, una marcha (no si pasaría del limitador)"},
    {Action::ShiftUp,
     {k(Key::E), {}},
     Action::None,
     {},
     PadControl::RightShoulder,
     kAll,
     KeyBlock::Drive,
     "Subir marcha",
     "de R a N y de N a 1 (no si pasaría del limitador); en secuencial, una marcha; en "
     "automático, desde 1 cambia solo"},
    {Action::Reset,
     {k(Key::R), {}},
     Action::None,
     {},
     PadControl::FaceNorth,
     kAll,
     KeyBlock::Drive,
     "Recolocar",
     "en la zona, al punto de recolocación más cercano, parado y con su orientación; en el "
     "recinto, a la salida mirando al frente"},
    {Action::RecenterWheel,
     {k(Key::X), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::Drive,
     "Recentrar volante (ratón)",
     "también el botón central del ratón; solo con la dirección con ratón"},
    {Action::SteerAssist,
     {k(Key::F6), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::Drive,
     "Ayuda de dirección",
     "la activa o la quita; limita las ruedas delanteras al ángulo de máximo agarre desde "
     "30 km/h; reconstruye el "
     "mundo y empieza otra grabación; se guarda en ajustes.txt"},
    {Action::Gearbox,
     {k(Key::F2), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::Drive,
     "Caja: automática / secuencial",
     "reconstruye el mundo y empieza otra grabación; se guarda en ajustes.txt"},
    {Action::SteerDevice,
     {k(Key::F7), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::Drive,
     "Dirección: teclado / ratón",
     "con ratón, el desplazamiento horizontal es el volante (cursor capturado; se suelta en "
     "pausa o al perder el foco y el volante vuelve al centro); A/D no giran; no reconstruye el "
     "mundo; se guarda en ajustes.txt"},
    // Cámara y asiento
    {Action::Camera,
     {k(Key::Tab), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::CameraSeat,
     "Cámara",
     "ojo del conductor (el del archivo del coche más el asiento), persecución y fija (recinto) "
     "o toda la zona, por turnos"},
    {Action::HeadMotion,
     {k(Key::F9), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::CameraSeat,
     "Movimiento de la cabeza",
     "sí / no: el ojo se mueve con las aceleraciones del coche (solo la cámara del ojo); se "
     "guarda en ajustes.txt"},
    {Action::SeatUp,
     {k(Key::PageUp), {}},
     Action::SeatDown,
     {k(Key::PageDown), {}},
     PadControl::None,
     kAll,
     KeyBlock::CameraSeat,
     "Asiento: arriba / abajo",
     "1 cm por pulsación, se repite al mantener; solo la vista del ojo, no la simulación ni la "
     "grabación; límite ± 15 cm en cada eje; se guarda en ajustes.txt junto al exe"},
    {Action::SeatForward,
     {k(Key::Home), {}},
     Action::SeatBack,
     {k(Key::End), {}},
     PadControl::None,
     kAll,
     KeyBlock::CameraSeat,
     "Asiento: adelante / atrás",
     ""},
    {Action::SeatRight,
     {k(Key::Insert), {}},
     Action::SeatLeft,
     {k(Key::Delete), {}},
     PadControl::None,
     kAll,
     KeyBlock::CameraSeat,
     "Asiento: a los lados",
     "derecha / izquierda"},
    // Zona
    {Action::NextSurface,
     {k(Key::G), {}},
     Action::None,
     {},
     PadControl::None,
     kZone,
     KeyBlock::Zone,
     "Superficie de la pista ciega",
     "la siguiente; reconstruye el mundo"},
    {Action::NextTrial,
     {k(Key::G), {}},
     Action::None,
     {},
     PadControl::None,
     kBlind,
     KeyBlock::Zone,
     "A ciegas: siguiente prueba",
     "de la serie de 15 (--blind-seed)"},
    {Action::BlindMode,
     {k(Key::M), {}},
     Action::None,
     {},
     PadControl::None,
     kZone | kBlind,
     KeyBlock::Zone,
     "Modo a ciegas sí / no",
     "todas las superficies del mismo color, sin microrrelieve ni nombres; en pantalla, «Prueba "
     "k/15»; cada prueba sale en la pista ciega"},
    {Action::RoadSurface,
     {k(Key::U), {}},
     Action::None,
     {},
     PadControl::None,
     kZone,
     KeyBlock::Zone,
     "Superficie de la carretera de prueba",
     "grava / asfalto; reconstruye el mundo con el coche en la salida de la carretera"},
    {Action::ZoneStart,
     {k(Key::Y), {}},
     Action::None,
     {},
     PadControl::None,
     kZone,
     KeyBlock::Zone,
     "Salida: recta / carretera",
     "dónde sale el coche (también al reconstruir el mundo con G, F2, F6 o Retroceso); "
     "reconstruye el mundo"},
    // Grabación
    {Action::Capture,
     {k(Key::C), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::Recording,
     "Captura del fotograma",
     "captura_NNN.png junto al exe"},
    {Action::FrameLog,
     {k(Key::L), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::Recording,
     "Registro por fotograma",
     "fotogramas_NNN.csv junto al exe"},
    // Depuración
    {Action::DebugPanel,
     {k(Key::F3), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::Debug,
     "Panel de depuración",
     "mando, entradas, impacto, coche, grabación, zona, cámara, asiento y ritmo de fotogramas; en "
     "el modo a ciegas, sin superficie ni configuración"},
    {Action::Perf,
     {k(Key::F1), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::Debug,
     "Contador de rendimiento",
     "CPU, GPU, draw calls, memoria y gráfica de fotogramas"},
    {Action::Graphs,
     {k(Key::T), {}},
     Action::None,
     {},
     PadControl::None,
     kZone | kArena,
     KeyBlock::Debug,
     "Gráficas de la telemetría",
     "últimos 10 s, por páginas: 1 · manejo (beta, guiñada, velocidad y entradas aplicadas), 2 · "
     "tren motriz, 3 · suspensión (carga y compresión por rueda) y ocultas; las páginas sin "
     "canales se saltan; en el modo a ciegas no hace nada"},
    {Action::FarOrigin,
     {k(Key::O), {}},
     Action::None,
     {},
     PadControl::None,
     kArena,
     KeyBlock::Debug,
     "Recinto a 20 km del origen",
     "y de vuelta"},
    {Action::Forest,
     {k(Key::B), {}},
     Action::None,
     {},
     PadControl::None,
     kArena,
     KeyBlock::Debug,
     "Bosque de medida (recinto)",
     "escena de medida del bosque, por turnos: apagado, uno a uno, DrawMeshInstanced y "
     "persistente"},
    {Action::WorldFloat,
     {k(Key::J), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::Debug,
     "Dibujar en float del mundo",
     "depuración del render relativo"},
    {Action::FrameRate,
     {k(Key::F), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::Debug,
     "Monitor / 30 fps",
     "a la frecuencia del monitor o a 30 fps"},
    {Action::Vsync,
     {k(Key::V), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::Debug,
     "Sincronización vertical",
     ""},
    {Action::Interpolation,
     {k(Key::I), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::Debug,
     "Interpolación",
     ""},
    {Action::Snap,
     {k(Key::N), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::Debug,
     "Ajuste del dt al refresco",
     ""},
    // General
    {Action::Help,
     {k(Key::H), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::General,
     "Ayuda de teclas",
     "por páginas: página siguiente y, tras la última, oculta"},
    {Action::Pause,
     {k(Key::P), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::General,
     "Pausa",
     ""},
    {Action::Volume,
     {k(Key::F4), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::General,
     "Volumen",
     "100, 70, 40 y 0 %, por turnos; se guarda en ajustes.txt"},
    {Action::ReloadCar,
     {k(Key::F5), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::General,
     "Releer el archivo del coche",
     "reconstruye el mundo con el coche en el punto de recolocación más cercano (en el recinto, "
     "en su salida); si el archivo tiene un error, lo muestra con su línea y sigue el coche "
     "anterior"},
    {Action::NextCar,
     {k(Key::F8), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::General,
     "Configuración siguiente",
     "mid_awd_turbo, front_awd_turbo_5cyl, front_rwd_na y front_awd_turbo_groupa, por turnos, "
     "de la carpeta del coche; reconstruye el mundo; a ciegas o con --hide-config, la pantalla "
     "no dice cuál"},
    {Action::ResetWorld,
     {k(Key::Backspace), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::General,
     "Reiniciar el mundo",
     "cierra la grabación y empieza otra"},
    {Action::ToggleArena,
     {k(Key::K), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::General,
     "Zona de pruebas / recinto",
     "el recinto es el de la fase 1; reconstruye el mundo y empieza otra grabación"},
    {Action::Fullscreen,
     {k(Key::F11), alt(Key::Enter)},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::General,
     "Pantalla completa sin bordes",
     ""},
    {Action::Exit,
     {k(Key::Escape), {}},
     Action::None,
     {},
     PadControl::None,
     kAll,
     KeyBlock::General,
     "Salir",
     ""},
};

const KeyBinding* find(Action action, std::span<const KeyBinding> table, bool& pair) {
    for (const KeyBinding& b : table) {
        if (b.action == action) {
            pair = false;
            return &b;
        }
        if (b.pair_action == action) {
            pair = true;
            return &b;
        }
    }
    return nullptr;
}

std::string chord_text(const KeyChord& chord, bool short_labels) {
    std::string text = chord.alt ? "Alt+" : "";
    text += short_labels ? key_label_short(chord.key) : key_label(chord.key);
    return text;
}

} // namespace

std::string_view key_label(Key key) {
    switch (key) {
#define RALLY_KEY_LABEL(name, raylib, value, label, short_label)                                   \
    case Key::name:                                                                                \
        return label;
        RALLY_KEYS(RALLY_KEY_LABEL)
#undef RALLY_KEY_LABEL
    case Key::None:
        break;
    }
    return "";
}

std::string_view key_label_short(Key key) {
    switch (key) {
#define RALLY_KEY_SHORT(name, raylib, value, label, short_label)                                   \
    case Key::name:                                                                                \
        return short_label;
        RALLY_KEYS(RALLY_KEY_SHORT)
#undef RALLY_KEY_SHORT
    case Key::None:
        break;
    }
    return "";
}

std::string_view key_block_name(KeyBlock block) {
    switch (block) {
    case KeyBlock::Drive:
        return "Conducir";
    case KeyBlock::CameraSeat:
        return "Cámara y asiento";
    case KeyBlock::Zone:
        return "Zona";
    case KeyBlock::Recording:
        return "Grabación";
    case KeyBlock::Debug:
        return "Depuración";
    case KeyBlock::General:
        return "General";
    case KeyBlock::Count:
        break;
    }
    return "";
}

std::span<const KeyBinding> key_table() { return kTable; }

std::array<KeyChord, 2> chords_for(Action action, std::span<const KeyBinding> table) {
    bool pair = false;
    const KeyBinding* b = find(action, table, pair);
    if (b == nullptr) {
        return {};
    }
    return pair ? b->pair_keys : b->keys;
}

PadControl pad_for(Action action, std::span<const KeyBinding> table) {
    bool pair = false;
    const KeyBinding* b = find(action, table, pair);
    return b != nullptr ? b->pad : PadControl::None;
}

std::uint8_t contexts_for(Action action, std::span<const KeyBinding> table) {
    bool pair = false;
    const KeyBinding* b = find(action, table, pair);
    return b != nullptr ? b->contexts : std::uint8_t{0};
}

int count_key_conflicts(std::span<const KeyBinding> table) {
    struct Use {
        KeyChord chord;
        std::uint8_t contexts;
    };
    // Todas las teclas de la tabla con sus contextos, y los pares repetidos.
    const auto for_each_use = [&table](auto&& visit) {
        for (const KeyBinding& b : table) {
            for (const auto* keys : {&b.keys, &b.pair_keys}) {
                for (const KeyChord& chord : *keys) {
                    if (chord.key != Key::None) {
                        visit(Use{chord, b.contexts});
                    }
                }
            }
        }
    };
    int conflicts = 0;
    int i = 0;
    for_each_use([&](const Use& a) {
        int j = 0;
        for_each_use([&](const Use& b) {
            if (j > i && a.chord.key == b.chord.key && a.chord.alt == b.chord.alt &&
                (a.contexts & b.contexts) != 0) {
                ++conflicts;
            }
            ++j;
        });
        ++i;
    });
    return conflicts;
}

std::string keys_text(const KeyBinding& binding, bool short_labels) {
    std::string text;
    for (std::size_t i = 0; i < binding.keys.size(); ++i) {
        if (binding.keys[i].key == Key::None) {
            continue;
        }
        if (!text.empty()) {
            text += " o ";
        }
        text += chord_text(binding.keys[i], short_labels);
        if (binding.pair_action != Action::None && binding.pair_keys[i].key != Key::None) {
            text += '/';
            text += chord_text(binding.pair_keys[i], short_labels);
        }
    }
    return text;
}

std::string_view pad_text_doc(PadControl pad) {
    switch (pad) {
    case PadControl::LeftStickX:
        return "stick izquierdo";
    case PadControl::RightTrigger:
        return "gatillo derecho";
    case PadControl::LeftTrigger:
        return "gatillo izquierdo";
    case PadControl::FaceSouth:
        return "botón de abajo de la cara derecha (A Xbox, Cruz PlayStation, B Nintendo)";
    case PadControl::FaceNorth:
        return "botón de arriba de la cara derecha (Y Xbox, Triángulo PlayStation, X Nintendo)";
    case PadControl::LeftShoulder:
        return "hombro izquierdo (LB, L1, L)";
    case PadControl::RightShoulder:
        return "hombro derecho (RB, R1, R)";
    case PadControl::None:
        break;
    }
    return "";
}

std::string_view pad_text_short(PadControl pad) {
    switch (pad) {
    case PadControl::LeftStickX:
        return "stick izq.";
    case PadControl::RightTrigger:
        return "gatillo der.";
    case PadControl::LeftTrigger:
        return "gatillo izq.";
    case PadControl::FaceSouth:
        return "abajo (A/Cruz/B)";
    case PadControl::FaceNorth:
        return "arriba (Y/Triángulo/X)";
    case PadControl::LeftShoulder:
        return "hombro izq.";
    case PadControl::RightShoulder:
        return "hombro der.";
    case PadControl::None:
        break;
    }
    return "";
}

std::string binding_label_doc(const KeyBinding& binding) {
    std::string text = keys_text(binding);
    if (binding.pad != PadControl::None) {
        text += " · ";
        text += pad_text_doc(binding.pad);
    }
    return text;
}

std::string binding_label_short(const KeyBinding& binding) {
    std::string text = keys_text(binding, true);
    if (binding.pad != PadControl::None) {
        text += " · ";
        text += pad_text_short(binding.pad);
    }
    return text;
}

std::string keys_markdown(std::span<const KeyBinding> table) {
    std::string out;
    for (int block = 0; block < kKeyBlockCount; ++block) {
        bool header = false;
        for (const KeyBinding& b : table) {
            if (static_cast<int>(b.block) != block) {
                continue;
            }
            if (!header) {
                out += "\n**";
                out += key_block_name(b.block);
                out += "**\n\n| Tecla / mando | Acción |\n| --- | --- |\n";
                header = true;
            }
            out += "| ";
            out += binding_label_doc(b);
            out += " | ";
            out += b.text;
            if (b.detail[0] != '\0') {
                out += ": ";
                out += b.detail;
            }
            out += " |\n";
        }
    }
    return out;
}

} // namespace rally::platform
