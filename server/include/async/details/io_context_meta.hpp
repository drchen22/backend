#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>

namespace detail {

struct io_context_meta {
    std::mutex mtx;
    std::atomic<std::size_t> create_count{0};
};

inline io_context_meta global_context_meta;

} // namespace detail
