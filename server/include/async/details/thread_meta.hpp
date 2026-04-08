#pragma once

#include <async/config/io_context.hpp>
#include <cstddef>

class io_context;
class worker_meta;

namespace detail {

struct alignas(config::cache_line_size) thread_meta {
    io_context *ctx{nullptr};
    worker_meta *worker{nullptr};
    std::size_t ctx_id{0};
};

inline thread_local thread_meta this_thread{};

} // namespace detail
