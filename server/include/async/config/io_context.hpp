#pragma once

#include <bit>
#include <cstddef>

namespace config {

inline constexpr std::size_t cache_line_size = 64;

inline constexpr std::size_t swap_capacity = 4096;

inline constexpr std::size_t submission_threshold =
    static_cast<std::size_t>(-1);

inline constexpr std::size_t io_uring_entries = 512;

} // namespace config
