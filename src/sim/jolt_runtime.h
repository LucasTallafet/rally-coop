#pragma once

namespace rally::sim {

// True si el código del juego se compiló con las mismas definiciones de Jolt que la
// biblioteca (versión, doble precisión, determinismo, asserts, ...). Si no coinciden,
// Jolt falla en ejecución de formas difíciles de diagnosticar.
bool jolt_abi_matches();

// Inicializa Jolt para el proceso (asignador, fábrica y tipos registrados).
// Una sola instancia viva a la vez.
class JoltRuntime {
public:
    JoltRuntime();
    ~JoltRuntime();
    JoltRuntime(const JoltRuntime&) = delete;
    JoltRuntime& operator=(const JoltRuntime&) = delete;
};

} // namespace rally::sim
