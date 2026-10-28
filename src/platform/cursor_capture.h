#pragma once

namespace rally::platform {

// Captura del cursor de raylib (DisableCursor / EnableCursor) con el desplazamiento relativo del
// ratón. La usan la dirección con ratón y la mirada del conductor (platform/input) y la mirada del
// copiloto (platform/copilot_app). Al capturar se descarta el primer desplazamiento: es el salto
// del cursor al centro.
class CursorCapture {
public:
    CursorCapture() = default;
    ~CursorCapture();
    CursorCapture(const CursorCapture&) = delete;
    CursorCapture& operator=(const CursorCapture&) = delete;

    // Una vez por fotograma: captura o suelta el cursor según `want` (quien llama ya cuenta con
    // el foco y la pausa: platform/input_shaping, mouse_use).
    void update(bool want);
    bool captured() const { return captured_; }
    // El estado de captura cambió en el último update.
    bool changed() const { return changed_; }
    // Desplazamiento del fotograma (px; x a la derecha, y hacia abajo). Falso, y sin tocar dx ni
    // dy, si no está capturado o es el primer fotograma tras capturar.
    bool take_delta(float& dx, float& dy);

private:
    bool captured_ = false;
    bool changed_ = false;
    bool skip_delta_ = false;
};

} // namespace rally::platform
