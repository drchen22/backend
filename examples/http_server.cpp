#include <async/io_context.hpp>
#include <net/http_server.hpp>
#include <print>
#include <csignal>

static io_context *g_ctx = nullptr;

static void signal_handler(int) {
    if (g_ctx) {
        g_ctx->stop();
    }
}

int main() {
    auto file = load_static_file("/home/chen/work/backend/examples/index.html");
    if (file.response.empty()) {
        return 1;
    }

    io_context ctx;
    g_ctx = &ctx;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    ctx.co_spawn(http_server(ctx, 8080, file));
    ctx.start();
    ctx.join();

    std::println("server stopped");
    return 0;
}
