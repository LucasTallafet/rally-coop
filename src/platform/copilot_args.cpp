#include "platform/copilot_args.h"

#include <charconv>
#include <cstring>
#include <string_view>
#include <system_error>

namespace rally::platform {

namespace {

bool is_ipv4(std::string_view text) {
    int parts = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('.', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view part = text.substr(start, end - start);
        int value = 0;
        const std::from_chars_result r =
            std::from_chars(part.data(), part.data() + part.size(), value);
        if (part.empty() || r.ec != std::errc{} || r.ptr != part.data() + part.size() ||
            value < 0 || value > 255) {
            return false;
        }
        ++parts;
        start = end + 1;
    }
    return parts == 4;
}

} // namespace

bool parse_copilot_args(int argc, char** argv, CopilotArgs& out, std::string& error) {
    out = CopilotArgs{};
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--copiloto") != 0) {
            continue;
        }
        if (out.enabled) {
            error = "--copiloto está repetido";
            return false;
        }
        out.enabled = true;
        // El argumento siguiente es la dirección si no es otra opción.
        if (i + 1 >= argc || std::strncmp(argv[i + 1], "--", 2) == 0) {
            continue;
        }
        const std::string_view text = argv[++i];
        std::string_view address = text;
        const std::size_t colon = text.find(':');
        if (colon != std::string_view::npos) {
            address = text.substr(0, colon);
            const std::string_view port_text = text.substr(colon + 1);
            int port = 0;
            const std::from_chars_result r =
                std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
            if (port_text.empty() || r.ec != std::errc{} ||
                r.ptr != port_text.data() + port_text.size() || port < 1 || port > 65535) {
                error = "puerto no válido en «" + std::string(text) + "» (de 1 a 65535)";
                return false;
            }
            out.port = static_cast<std::uint16_t>(port);
        }
        if (!is_ipv4(address)) {
            error = "dirección no válida en «" + std::string(text) +
                    "» (una IP como 127.0.0.1, con el puerto opcional tras «:»)";
            return false;
        }
        out.address = std::string(address);
    }
    return true;
}

} // namespace rally::platform
