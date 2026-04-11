#include <catch2/catch_test_macros.hpp>
#include <async/io_context.hpp>
#include <async/lazy_io.hpp>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

// ============================================================
// timeout_at 测试
// ============================================================

static Task<void> test_timeout_at_steady(io_context &ctx) {
    auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    auto start = std::chrono::steady_clock::now();
    int res = co_await lazy::timeout_at(deadline);
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);

    REQUIRE(res == -ETIME);
    REQUIRE(ms.count() >= 40);

    ctx.stop();
    co_return;
}

TEST_CASE("timeout_at: steady_clock absolute", "[timeout]") {
    io_context ctx;
    ctx.co_spawn(test_timeout_at_steady(ctx));
    ctx.start();
    ctx.join();
}

// ============================================================
// lazy_link_timeout 测试
// ============================================================

static Task<void> test_linked_timeout_io_succeeds(io_context &ctx) {
    int sv[2];
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    REQUIRE(ret == 0);

    const char *msg = "hello linked timeout";
    co_await lazy::send(sv[0], msg, std::strlen(msg), 0);

    char buf[64]{};

    auto result = co_await lazy::timeout(
        lazy::recv(sv[1], buf, sizeof(buf), 0),
        std::chrono::seconds(5));

    REQUIRE(result == static_cast<int>(std::strlen(msg)));
    REQUIRE(std::memcmp(buf, msg, std::strlen(msg)) == 0);

    co_await lazy::close(sv[0]);
    co_await lazy::close(sv[1]);

    ctx.stop();
    co_return;
}

TEST_CASE("linked timeout: IO completes before timeout", "[timeout]") {
    io_context ctx;
    ctx.co_spawn(test_linked_timeout_io_succeeds(ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_linked_timeout_io_cancelled(io_context &ctx) {
    int sv[2];
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    REQUIRE(ret == 0);

    char buf[64]{};

    auto result = co_await lazy::timeout(
        lazy::recv(sv[1], buf, sizeof(buf), 0),
        std::chrono::milliseconds(50));

    REQUIRE(result == -ECANCELED);

    co_await lazy::close(sv[0]);
    co_await lazy::close(sv[1]);

    ctx.stop();
    co_return;
}

TEST_CASE("linked timeout: IO cancelled by timeout", "[timeout]") {
    io_context ctx;
    ctx.co_spawn(test_linked_timeout_io_cancelled(ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_linked_timeout_write(io_context &ctx) {
    int sv[2];
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    REQUIRE(ret == 0);

    const char *msg = "linked write test";

    auto result = co_await lazy::timeout(
        lazy::send(sv[0], msg, std::strlen(msg), 0),
        std::chrono::seconds(5));

    REQUIRE(result == static_cast<int>(std::strlen(msg)));

    char buf[64]{};
    co_await lazy::recv(sv[1], buf, sizeof(buf), 0);
    REQUIRE(std::memcmp(buf, msg, std::strlen(msg)) == 0);

    co_await lazy::close(sv[0]);
    co_await lazy::close(sv[1]);

    ctx.stop();
    co_return;
}

TEST_CASE("linked timeout: write succeeds before timeout", "[timeout]") {
    io_context ctx;
    ctx.co_spawn(test_linked_timeout_write(ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_linked_timeout_at(io_context &ctx) {
    int sv[2];
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    REQUIRE(ret == 0);

    const char *msg = "absolute timeout";
    co_await lazy::send(sv[0], msg, std::strlen(msg), 0);

    char buf[64]{};

    auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);

    auto result = co_await lazy::timeout_at(
        lazy::recv(sv[1], buf, sizeof(buf), 0), deadline);

    REQUIRE(result == static_cast<int>(std::strlen(msg)));

    co_await lazy::close(sv[0]);
    co_await lazy::close(sv[1]);

    ctx.stop();
    co_return;
}

TEST_CASE("linked timeout_at: IO completes before absolute deadline",
          "[timeout]") {
    io_context ctx;
    ctx.co_spawn(test_linked_timeout_at(ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_linked_timeout_at_expires(io_context &ctx) {
    int sv[2];
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    REQUIRE(ret == 0);

    char buf[64]{};

    auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(50);

    auto result = co_await lazy::timeout_at(
        lazy::recv(sv[1], buf, sizeof(buf), 0), deadline);

    REQUIRE(result == -ECANCELED);

    co_await lazy::close(sv[0]);
    co_await lazy::close(sv[1]);

    ctx.stop();
    co_return;
}

TEST_CASE("linked timeout_at: IO cancelled by absolute deadline",
          "[timeout]") {
    io_context ctx;
    ctx.co_spawn(test_linked_timeout_at_expires(ctx));
    ctx.start();
    ctx.join();
}

// ============================================================
// 多次 linked timeout 测试
// ============================================================

static Task<void> test_multiple_linked_timeouts(io_context &ctx) {
    int sv[2];
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    REQUIRE(ret == 0);

    const char *msg1 = "first";
    co_await lazy::send(sv[0], msg1, std::strlen(msg1), 0);

    char buf1[32]{};
    auto r1 = co_await lazy::timeout(
        lazy::recv(sv[1], buf1, sizeof(buf1), 0),
        std::chrono::seconds(5));
    REQUIRE(r1 == static_cast<int>(std::strlen(msg1)));

    const char *msg2 = "second";
    co_await lazy::send(sv[0], msg2, std::strlen(msg2), 0);

    char buf2[32]{};
    auto r2 = co_await lazy::timeout(
        lazy::recv(sv[1], buf2, sizeof(buf2), 0),
        std::chrono::seconds(5));
    REQUIRE(r2 == static_cast<int>(std::strlen(msg2)));

    co_await lazy::close(sv[0]);
    co_await lazy::close(sv[1]);

    ctx.stop();
    co_return;
}

TEST_CASE("linked timeout: multiple sequential operations", "[timeout]") {
    io_context ctx;
    ctx.co_spawn(test_multiple_linked_timeouts(ctx));
    ctx.start();
    ctx.join();
}

// ============================================================
// to_kernel_timespec_biased 测试
// ============================================================

TEST_CASE("to_kernel_timespec_biased: clamps to zero", "[timeout]") {
    auto ts = lazy::to_kernel_timespec_biased(std::chrono::nanoseconds(0));
    REQUIRE(ts.tv_sec == 0);
    REQUIRE(ts.tv_nsec == 0);
}

TEST_CASE("to_kernel_timespec_biased: subtracts bias", "[timeout]") {
    auto ts = lazy::to_kernel_timespec_biased(std::chrono::milliseconds(100));
    auto unbiased = lazy::to_kernel_timespec(std::chrono::milliseconds(100));
    REQUIRE(ts.tv_sec == unbiased.tv_sec);
    REQUIRE(ts.tv_nsec == unbiased.tv_nsec - config::timeout_bias_nanosecond);
}
