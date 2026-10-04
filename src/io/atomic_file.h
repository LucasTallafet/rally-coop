#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace rally::io {

// Escribe el archivo sin dejarlo nunca a medias: primero <path>.tmp, con sus datos llevados al
// disco (FlushFileBuffers vía _commit en Windows, fsync en el resto), y después lo pone en el
// sitio de path de una vez (MoveFileExW con MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
// en Windows; std::filesystem::rename, que en POSIX ya reemplaza de forma atómica, en el resto).
// Si algo falla, path queda como estaba, devuelve false y deja el motivo en error. Lo usa
// ajustes.txt: un cierre a mitad o un error fatal no lo dejan cortado.
bool write_file_atomic(const std::filesystem::path& path, std::string_view bytes,
                       std::string& error);

} // namespace rally::io
