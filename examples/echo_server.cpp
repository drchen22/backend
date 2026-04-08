#include <async/io_context.hpp>
#include <csignal>
#include <net/acceptor.hpp>
#include <net/socket.hpp>
#include <print>

static io_context *g_ctx = nullptr;

static void signal_handler(int) {
    if (g_ctx) {
        g_ctx->stop();
    }
}

static Task<void> echo_connection(net::socket sock) {
    char buf[4096];
    while (true) {
        int n = co_await sock.recv(buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        int sent = co_await sock.send(buf, static_cast<std::size_t>(n));
        if (sent <= 0) {
            break;
        }
    }
    co_await sock.close();
}

static Task<void> echo_server(uint16_t port) {
    net::acceptor ac{net::inet_address{port}};
    std::println("echo server listening on port {}", port);

    while (true) {
        int conn_fd = co_await ac.accept();
        if (conn_fd < 0) {
            continue;
        }
        auto *ctx = this_io_context();
        ctx->co_spawn(echo_connection(net::socket(conn_fd)));
    }
}

int main(int argc, char *argv[]) {
    uint16_t port = 9090;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    io_context ctx;
    g_ctx = &ctx;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    ctx.co_spawn(echo_server(port));
    ctx.start();
    ctx.join();

    std::println("server stopped");
    return 0;
}
