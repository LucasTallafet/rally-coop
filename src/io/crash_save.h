#pragma once

#include <string>

namespace rally::io {

class Recorder;

// Guardado de emergencia: si salta un RALLY_CHECK, el manejador de la plataforma llama a
// save_crash_recording() antes de mostrar el error, y lo que llevaba grabado el Recorder
// registrado se escribe en su ruta marcado como incompleto (sin hash final). Lo grabado son
// los pasos terminados: el que abortó no está.

// Registra la grabación en curso y su archivo (fuera del paso). nullptr la quita.
void set_crash_recording(const Recorder* recorder, std::string path);

// Escribe la grabación registrada como incompleta. false si no hay ninguna activa o no se
// pudo escribir. Se puede llamar desde el manejador de RALLY_CHECK.
bool save_crash_recording();

} // namespace rally::io
