#include "io/atomic_file.h"

#include <cerrno>
#include <cstdio>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace rally::io {

namespace {

std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::FILE* open_for_write(const std::filesystem::path& path) {
#ifdef _WIN32
    std::FILE* file = nullptr;
    return _wfopen_s(&file, path.c_str(), L"wb") == 0 ? file : nullptr;
#else
    return std::fopen(path.c_str(), "wb");
#endif
}

// Lleva al disco lo escrito (la caché de la C y la del sistema).
bool flush_to_disk(std::FILE* file) {
    if (std::fflush(file) != 0) {
        return false;
    }
#ifdef _WIN32
    return _commit(_fileno(file)) == 0;
#else
    return fsync(fileno(file)) == 0;
#endif
}

bool replace(const std::filesystem::path& from, const std::filesystem::path& to,
             std::string& error) {
#ifdef _WIN32
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ==
        0) {
        error = "no se puede reemplazar " + utf8(to) + ": " +
                std::system_category().message(static_cast<int>(GetLastError()));
        return false;
    }
    return true;
#else
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (ec) {
        error = "no se puede reemplazar " + utf8(to) + ": " + ec.message();
        return false;
    }
    return true;
#endif
}

} // namespace

bool write_file_atomic(const std::filesystem::path& path, std::string_view bytes,
                       std::string& error) {
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::FILE* file = open_for_write(temporary);
    if (file == nullptr) {
        error =
            "no se puede crear " + utf8(temporary) + ": " + std::generic_category().message(errno);
        return false;
    }
    const bool written =
        std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size() && flush_to_disk(file);
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed) {
        error = "no se puede escribir " + utf8(temporary);
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    if (!replace(temporary, path, error)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

} // namespace rally::io
