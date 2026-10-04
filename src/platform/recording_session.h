#pragma once

#include "io/recording.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"

#include <cstdint>
#include <string>

namespace rally::platform {

// Grabación de la partida: empieza al construir el mundo y termina al reiniciarlo o al salir.
// Escribe grabaciones/AAAAMMDD-HHMMSS.rrec junto al exe (con -2, -3... si el nombre ya
// existe). Reserva al empezar sitio para 30 minutos y, si se llena, amplía en prepare(),
// fuera del paso. Mientras graba, queda registrada para el guardado de emergencia
// (io/crash_save.h): si salta un RALLY_CHECK, se escribe marcada como incompleta.
class RecordingSession {
public:
    static constexpr int kReservedMinutes = 30;

    RecordingSession() = default;
    ~RecordingSession();
    RecordingSession(const RecordingSession&) = delete;
    RecordingSession& operator=(const RecordingSession&) = delete;

    // Con el mundo ya construido a partir de `desc` (de él sale el hash de la zona).
    void start(const sim::WorldDesc& desc, const sim::SimWorld& world);
    // Antes de los pasos del fotograma.
    void prepare(int steps) { recorder_.ensure_capacity(steps); }
    // Tras cada paso simulado, con su entrada.
    void record(const sim::SimInput& input) { recorder_.append(input); }
    // Escribe el archivo con el hash final de `world` (nada si no hay pasos).
    void finish(const sim::SimWorld& world);

    bool active() const { return recorder_.active(); }
    const std::string& name() const { return name_; } // nombre del archivo, sin carpeta
    std::uint64_t steps() const { return recorder_.steps(); }
    // Último error al escribir (vacío si ninguno).
    const std::string& error() const { return error_; }

private:
    io::Recorder recorder_;
    std::string name_;
    std::string path_;
    std::string error_;
};

} // namespace rally::platform
