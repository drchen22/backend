#pragma once

#include "async/details/io_awaitable.hpp"
#include "async/io_context.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <liburing.h>
#include <netinet/in.h>
#include <sys/socket.h>

inline io_awaitable async_accept(io_context &ctx, int fd) {
    io_awaitable aw{};
    auto *sqe = io_uring_get_sqe(ctx.ring());
    io_uring_prep_accept(sqe, fd, nullptr, nullptr, 0);
    aw.sqe_ = sqe;
    return aw;
}

inline io_awaitable async_read(
    io_context &ctx, int fd, void *buf, std::size_t len) {
    io_awaitable aw{};
    auto *sqe = io_uring_get_sqe(ctx.ring());
    io_uring_prep_read(sqe, fd, buf, static_cast<unsigned>(len), 0);
    aw.sqe_ = sqe;
    return aw;
}

inline io_awaitable async_write(
    io_context &ctx, int fd, const void *buf, std::size_t len) {
    io_awaitable aw{};
    auto *sqe = io_uring_get_sqe(ctx.ring());
    io_uring_prep_write(sqe, fd, buf, static_cast<unsigned>(len), 0);
    aw.sqe_ = sqe;
    return aw;
}

inline io_awaitable async_close(io_context &ctx, int fd) {
    io_awaitable aw{};
    auto *sqe = io_uring_get_sqe(ctx.ring());
    io_uring_prep_close(sqe, fd);
    aw.sqe_ = sqe;
    return aw;
}
