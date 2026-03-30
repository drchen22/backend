#pragma once

#include "async/async_io.hpp"
#include "async/io_context.hpp"
#include "net/listen.hpp"
#include <array>
#include <cstddef>
#include <print>
#include <string>

struct static_file {
    std::string content;
    std::string response;
};

inline static_file load_static_file(const std::string &path) {
    static_file sf;

    auto *f = fopen(path.c_str(), "rb");
    if (!f) {
        std::println(stderr, "failed to open {}", path);
        return sf;
    }

    fseek(f, 0, SEEK_END);
    auto len = ftell(f);
    fseek(f, 0, SEEK_SET);
    sf.content.resize(len);
    fread(sf.content.data(), 1, len, f);
    fclose(f);

    sf.response = std::format(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: {}\r\n"
        "\r\n{}",
        sf.content.size(), sf.content);

    return sf;
}

inline Task<void> handle_connection(
    io_context &ctx, int fd, const static_file &file) {
    std::array<char, 4096> buf{};

    int n = co_await async_read(ctx, fd, buf.data(), buf.size());
    if (n <= 0) {
        co_await async_close(ctx, fd);
        co_return;
    }

    co_await async_write(
        ctx, fd, file.response.data(), file.response.size());
    co_await async_close(ctx, fd);
}

inline Task<void> http_server(
    io_context &ctx, uint16_t port, const static_file &file) {
    int listen_fd = tcp_listen(port);
    std::println("listening on port {}", port);

    while (true) {
        int conn_fd = co_await async_accept(ctx, listen_fd);
        if (conn_fd < 0) {
            continue;
        }
        ctx.co_spawn(handle_connection(ctx, conn_fd, file));
    }
}
