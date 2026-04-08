#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>
#include <async/io_context.hpp>
#include <async/lazy_io.hpp>
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

// ============================================================
// 链式 IO 测试
// ============================================================

static Task<void> test_link_nop_nop(io_context &ctx) {
    auto result = co_await (lazy::nop() && lazy::nop());
    REQUIRE(result == 0);

    ctx.stop();
    co_return;
}

TEST_CASE("chained IO: nop && nop", "[chain_io]") {
    io_context ctx;
    ctx.co_spawn(test_link_nop_nop(ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_link_send_recv(io_context &ctx) {
    int sv[2];
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    REQUIRE(ret == 0);

    const char *msg = "hello chained io";
    char buf[64]{};

    auto result = co_await (
        lazy::send(sv[0], msg, std::strlen(msg), 0)
        && lazy::recv(sv[1], buf, sizeof(buf), 0));

    REQUIRE(result == static_cast<int>(std::strlen(msg)));
    REQUIRE(std::memcmp(buf, msg, std::strlen(msg)) == 0);

    co_await lazy::close(sv[0]);
    co_await lazy::close(sv[1]);

    ctx.stop();
    co_return;
}

TEST_CASE("chained IO: send && recv", "[chain_io]") {
    io_context ctx;
    ctx.co_spawn(test_link_send_recv(ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_link_timeout_protects_io(io_context &ctx) {
    int sv[2];
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    REQUIRE(ret == 0);

    auto result = co_await (
        lazy::timeout(std::chrono::milliseconds(100))
        && lazy::recv(sv[1], nullptr, 0, 0));

    REQUIRE(result == -ECANCELED);

    co_await lazy::close(sv[0]);
    co_await lazy::close(sv[1]);

    ctx.stop();
    co_return;
}

TEST_CASE("chained IO: timeout && recv (timeout protects IO)", "[chain_io]") {
    io_context ctx;
    ctx.co_spawn(test_link_timeout_protects_io(ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_link_timeout_cancelled_by_io(io_context &ctx) {
    int sv[2];
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    REQUIRE(ret == 0);

    const char *msg = "cancel test";

    co_await lazy::send(sv[0], msg, std::strlen(msg), 0);

    char buf[64]{};

    // recv && timeout: recv 先执行，成功后 timeout 也执行
    // 由于 recv 立即完成（数据已就绪），timeout 随后启动并很快超时
    auto result = co_await (
        lazy::recv(sv[1], buf, sizeof(buf), 0)
        && lazy::timeout(std::chrono::milliseconds(50)));

    REQUIRE(result == -ETIME);

    co_await lazy::close(sv[0]);
    co_await lazy::close(sv[1]);

    ctx.stop();
    co_return;
}

TEST_CASE("chained IO: recv && timeout (IO completes, timeout fires)",
          "[chain_io]") {
    io_context ctx;
    ctx.co_spawn(test_link_timeout_cancelled_by_io(ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_link_three_ops(io_context &ctx) {
    int sv[2];
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    REQUIRE(ret == 0);

    const char *msg1 = "first";
    const char *msg2 = "second";

    co_await lazy::send(sv[0], msg1, std::strlen(msg1), 0);

    char buf1[32]{};
    co_await lazy::recv(sv[1], buf1, sizeof(buf1), 0);
    REQUIRE(std::memcmp(buf1, msg1, std::strlen(msg1)) == 0);

    char buf2[32]{};
    auto result = co_await (
        lazy::send(sv[0], msg2, std::strlen(msg2), 0)
        && lazy::recv(sv[1], buf2, sizeof(buf2), 0)
        && lazy::nop());

    REQUIRE(result == 0);
    REQUIRE(std::memcmp(buf2, msg2, std::strlen(msg2)) == 0);

    co_await lazy::close(sv[0]);
    co_await lazy::close(sv[1]);

    ctx.stop();
    co_return;
}

TEST_CASE("chained IO: three operations chained", "[chain_io]") {
    io_context ctx;
    ctx.co_spawn(test_link_three_ops(ctx));
    ctx.start();
    ctx.join();
}

// ============================================================
// 超时集成测试
// ============================================================

static Task<void> test_timeout_relative(io_context &ctx) {
    auto start = std::chrono::steady_clock::now();
    int res = co_await lazy::timeout(std::chrono::milliseconds(50));
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);

    REQUIRE(res == -ETIME);
    REQUIRE(ms.count() >= 40);

    ctx.stop();
    co_return;
}

TEST_CASE("timeout: relative duration", "[timeout]") {
    io_context ctx;
    ctx.co_spawn(test_timeout_relative(ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_timeout_absolute(io_context &ctx) {
    auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    auto start = std::chrono::steady_clock::now();
    int res = co_await lazy::timeout(deadline);
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);

    REQUIRE(res == -ETIME);
    REQUIRE(ms.count() >= 40);

    ctx.stop();
    co_return;
}

TEST_CASE("timeout: absolute time_point", "[timeout]") {
    io_context ctx;
    ctx.co_spawn(test_timeout_absolute(ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_multiple_timers(io_context &ctx) {
    auto start = std::chrono::steady_clock::now();

    int res1 = co_await lazy::timeout(std::chrono::milliseconds(50));
    REQUIRE(res1 == -ETIME);

    int res2 = co_await lazy::timeout(std::chrono::milliseconds(50));
    REQUIRE(res2 == -ETIME);

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
    REQUIRE(ms.count() >= 90);

    ctx.stop();
    co_return;
}

TEST_CASE("timeout: multiple sequential timers", "[timeout]") {
    io_context ctx;
    ctx.co_spawn(test_multiple_timers(ctx));
    ctx.start();
    ctx.join();
}
