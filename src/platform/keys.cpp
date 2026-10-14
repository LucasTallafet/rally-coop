#include "platform/keys.h"

#include "platform/key_tables.h"

#include <algorithm>

namespace rally::platform {

namespace {

const KeyBinding* find(ActionRef action, std::span<const KeyBinding> table, bool& pair) {
    for (const KeyBinding& b : table) {
        if (b.table != action.table) {
            continue;
        }
        if (b.action == action.index) {
            pair = false;
            return &b;
        }
        if (b.pair_action == action.index) {
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

std::vector<KeyBinding> join_key_tables(std::span<const KeyTable* const> tables) {
    std::vector<KeyBinding> joined;
    for (const KeyTable* t : tables) {
        for (KeyBinding b : t->rows) {
            b.table = t;
            joined.push_back(b);
        }
    }
    std::stable_sort(joined.begin(), joined.end(), [](const KeyBinding& a, const KeyBinding& b) {
        return a.block != b.block ? a.block < b.block : a.order < b.order;
    });
    return joined;
}

std::span<const KeyBinding> key_table() {
    static const std::vector<KeyBinding> joined = join_key_tables(key_tables());
    return joined;
}

std::array<KeyChord, 2> chords_for(ActionRef action, std::span<const KeyBinding> table) {
    bool pair = false;
    const KeyBinding* b = find(action, table, pair);
    if (b == nullptr) {
        return {};
    }
    return pair ? b->pair_keys : b->keys;
}

PadControl pad_for(ActionRef action, std::span<const KeyBinding> table) {
    bool pair = false;
    const KeyBinding* b = find(action, table, pair);
    return b != nullptr ? b->pad : PadControl::None;
}

std::uint8_t contexts_for(ActionRef action, std::span<const KeyBinding> table) {
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
        if (binding.pair_action != kNoAction && binding.pair_keys[i].key != Key::None) {
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
