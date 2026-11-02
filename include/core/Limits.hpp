#pragma once

#include <cstddef>
#include <cstdint>

namespace limits
{
    constexpr std::size_t MAX_HEADER_BYTES = 8 * 1024;
    constexpr std::size_t MAX_HEADER_COUNT = 100;
    constexpr std::size_t MAX_BODY_BYTES = 1 * 1024 * 1024;
    constexpr std::size_t MAX_READ_BUFFER = 2 * 1024 * 1024;
    constexpr std::size_t MAX_WRITE_BUFFER = 4 * 1024 * 1024;
    constexpr std::size_t MAX_CONNECTIONS = 512;

    constexpr std::uint64_t MAX_IDLE_MS = 60 * 1000;
    constexpr std::uint64_t IDLE_CHECK_INTERVAL_MS = 1000;
}