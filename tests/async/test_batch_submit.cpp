#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>
#include <async/details/worker_meta.hpp>
#include <async/io_context.hpp>
#include <async/lazy_io.hpp>
#include <net/socket.hpp>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

TEST_CASE("worker_meta: initial state", "[worker_meta]") {
    worker_meta wm;
    REQUIRE(wm.pending_submits() == 0);
    REQUIRE(wm.inflight_count() == 0);
    REQUIRE(wm.submit_threshold() == 1);
}

TEST_CASE("worker_meta: threshold configuration", "[worker_meta]") {
    worker_meta wm(8);
    REQUIRE(wm.submit_threshold() == 8);
    wm.set_submit_threshold(16);
    REQUIRE(wm.submit_threshold() == 16);
}

TEST_CASE("worker_meta: reset clears counters", "[worker_meta]") {
    worker_meta wm;
    wm.notify_io_inflight();
    wm.notify_io_inflight();
    REQUIRE(wm.inflight_count() == 2);
    wm.reset();
    REQUIRE(wm.pending_submits() == 0);
    REQUIRE(wm.inflight_count() == 0);
}

TEST_CASE("worker_meta: notify_io tracking", "[worker_meta]") {
    worker_meta wm;
    wm.notify_io_inflight();
    wm.notify_io_inflight();
    wm.notify_io_inflight();
    REQUIRE(wm.inflight_count() == 3);
    wm.notify_io_completed();
    REQUIRE(wm.inflight_count() == 2);
}

TEST_CASE("worker_meta: flush on threshold", "[worker_meta]") {
    io_uring ring;
    io_uring_queue_init(64, &ring, 0);

    worker_meta wm(3);

    io_uring_sqe *sqe1 = io_uring_get_sqe(&ring);
    io_uring_prep_nop(sqe1);
    io_uring_sqe_set_data64(sqe1, 0);
    int r1 = wm.notify_sqe_ready(&ring);
    REQUIRE(r1 == 0);
    REQUIRE(wm.pending_submits() == 1);

    io_uring_sqe *sqe2 = io_uring_get_sqe(&ring);
    io_uring_prep_nop(sqe2);
    io_uring_sqe_set_data64(sqe2, 0);
    int r2 = wm.notify_sqe_ready(&ring);
    REQUIRE(r2 == 0);
    REQUIRE(wm.pending_submits() == 2);

    io_uring_sqe *sqe3 = io_uring_get_sqe(&ring);
    io_uring_prep_nop(sqe3);
    io_uring_sqe_set_data64(sqe3, 0);
    int r3 = wm.notify_sqe_ready(&ring);
    REQUIRE(r3 >= 3);
    REQUIRE(wm.pending_submits() == 0);

    io_uring_cq_advance(&ring, 3);
    io_uring_queue_exit(&ring);
}

TEST_CASE("worker_meta: manual flush", "[worker_meta]") {
    io_uring ring;
    io_uring_queue_init(64, &ring, 0);

    worker_meta wm(100);

    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_nop(sqe);
    io_uring_sqe_set_data64(sqe, 0);
    wm.notify_sqe_ready(&ring);
    REQUIRE(wm.pending_submits() == 1);

    int ret = wm.flush(&ring);
    REQUIRE(ret >= 1);
    REQUIRE(wm.pending_submits() == 0);

    io_uring_cq_advance(&ring, 1);
    io_uring_queue_exit(&ring);
}

static Task<void> test_batch_file_io(io_context &ctx) {
    const char *path = "/tmp/batch_test_file";
    const char *data1 = "batch data 1";
    const char *data2 = "batch data 2";

    int fd = co_await lazy::openat(
        AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    REQUIRE(fd >= 0);

    int w1 = co_await lazy::write(fd, data1, std::strlen(data1), 0);
    REQUIRE(w1 == static_cast<int>(std::strlen(data1)));

    int w2 = co_await lazy::write(fd, data2, std::strlen(data2), w1);
    REQUIRE(w2 == static_cast<int>(std::strlen(data2)));

    char buf[64]{};
    int n = co_await lazy::read(fd, buf, sizeof(buf), 0);
    REQUIRE(n == static_cast<int>(std::strlen(data1) + std::strlen(data2)));

    co_await lazy::close(fd);
    ::unlink(path);

    ctx.stop();
    co_return;
}

TEST_CASE("batch submission: multiple file IO ops", "[batch]") {
    io_context ctx(256, 4);
    ctx.co_spawn(test_batch_file_io(ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_batch_socket_io(io_context &ctx) {
    int sv[2];
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    REQUIRE(ret == 0);

    net::socket s0(sv[0]);
    net::socket s1(sv[1]);

    const char *msg1 = "hello";
    const char *msg2 = " world";

    int sent1 = co_await s0.send(msg1, std::strlen(msg1), 0);
    REQUIRE(sent1 == static_cast<int>(std::strlen(msg1)));

    int sent2 = co_await s0.send(msg2, std::strlen(msg2), 0);
    REQUIRE(sent2 == static_cast<int>(std::strlen(msg2)));

    char buf[64]{};
    int n = co_await s1.recv(buf, sizeof(buf), 0);
    REQUIRE(n == static_cast<int>(std::strlen(msg1) + std::strlen(msg2)));
    REQUIRE(std::memcmp(buf, "hello world", 11) == 0);

    co_await s0.shutdown();
    co_await s1.shutdown();
    co_await s0.close();
    co_await s1.close();

    ctx.stop();
    co_return;
}

TEST_CASE("batch submission: socket wrapper with batching", "[batch]") {
    io_context ctx(256, 4);
    ctx.co_spawn(test_batch_socket_io(ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_explicit_submit(io_context &ctx) {
    const char *path = "/tmp/explicit_submit_test";
    const char *data = "explicit submit";

    int fd = co_await lazy::openat(
        AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    REQUIRE(fd >= 0);

    int written = co_await lazy::write(fd, data, std::strlen(data), 0);
    REQUIRE(written == static_cast<int>(std::strlen(data)));

    int ret = lazy::submit();
    REQUIRE(ret >= 0);

    char buf[64]{};
    int n = co_await lazy::read(fd, buf, sizeof(buf), 0);
    REQUIRE(n == static_cast<int>(std::strlen(data)));

    co_await lazy::close(fd);
    ::unlink(path);

    ctx.stop();
    co_return;
}

TEST_CASE("batch submission: explicit lazy::submit()", "[batch]") {
    io_context ctx(256, 100);
    ctx.co_spawn(test_explicit_submit(ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_high_threshold(io_context &ctx) {
    int sv[2];
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    REQUIRE(ret == 0);

    const char *msg = "high threshold test";
    co_await lazy::send(sv[0], msg, std::strlen(msg), 0);

    char buf[64]{};
    co_await lazy::recv(sv[1], buf, sizeof(buf), 0);
    REQUIRE(std::memcmp(buf, msg, std::strlen(msg)) == 0);

    co_await lazy::close(sv[0]);
    co_await lazy::close(sv[1]);

    ctx.stop();
    co_return;
}

TEST_CASE("batch submission: high threshold still works", "[batch]") {
    io_context ctx(256, 64);
    ctx.co_spawn(test_high_threshold(ctx));
    ctx.start();
    ctx.join();
}


