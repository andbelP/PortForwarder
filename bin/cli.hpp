#pragma once

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

inline std::uint16_t ParsePort(std::string_view text) {
    unsigned int port = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), port);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        port == 0 || port > 65535) {
        throw std::invalid_argument("Invalid port '" + std::string(text) +
                                    "': expected a number from 1 to 65535");
    }
    return static_cast<std::uint16_t>(port);
}
