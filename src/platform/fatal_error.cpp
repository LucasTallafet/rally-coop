#include "platform/fatal_error.h"

#include <cstdio>
#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace rally::platform {

void show_fatal_error(const char* message) {
    std::fprintf(stderr, "%s\n", message);

    std::wstring wide;
    const int length = MultiByteToWideChar(CP_UTF8, 0, message, -1, nullptr, 0);
    if (length > 0) {
        wide.resize(static_cast<size_t>(length));
        MultiByteToWideChar(CP_UTF8, 0, message, -1, wide.data(), length);
    }
    MessageBoxW(nullptr, wide.c_str(), L"Rally Coop", MB_OK | MB_ICONERROR);
}

} // namespace rally::platform
