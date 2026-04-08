#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>
#include <async/io_context.hpp>
#include <async/lazy_io.hpp>
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

static const char *test_file_path = "/tmp/lazy_io_test_file";

static Task<void> test_file_io(io_context &ctx) {
    const char *data = "hello lazy io";

    int fd = co_await lazy::openat(
        AT_FDCWD, test_file_path,
        O_CREAT | O_RDWR | O_TRUNC, 0644);
    REQUIRE(fd >= 0);

    int written = co_await lazy::write(
        fd, data, std::strlen(data), 0);
    REQUIRE(written == static_cast<int>(std::strlen(data)));

    int sync_res = co_await lazy::fsync(fd, 0);
    REQUIRE(sync_res == 0);

    char buf[64]{};
    int n = co_await lazy::read(fd, buf, sizeof(buf), 0);
    REQUIRE(n == static_cast<int>(std::strlen(data)));
    REQUIRE(std::memcmp(buf, data, std::strlen(data)) == 0);

    co_await lazy::close(fd);
    ::unlink(test_file_path);

    ctx.stop();
    co_return;
}

TEST_CASE("lazy file IO: openat/write/fsync/read/close", "[lazy_io]") {
    io_context ctx;
    ctx.co_spawn(test_file_io(ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_socket_io(io_context &ctx) {
    int sv[2];
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    REQUIRE(ret == 0);

    const char *msg = "hello from lazy";

    int sent = co_await lazy::send(sv[0], msg, std::strlen(msg), 0);
    REQUIRE(sent == static_cast<int>(std::strlen(msg)));

    char buf[64]{};
    int received = co_await lazy::recv(sv[1], buf, sizeof(buf), 0);
    REQUIRE(received == static_cast<int>(std::strlen(msg)));
    REQUIRE(std::memcmp(buf, msg, std::strlen(msg)) == 0);

    co_await lazy::shutdown(sv[0], SHUT_RDWR);
    co_await lazy::shutdown(sv[1], SHUT_RDWR);
    co_await lazy::close(sv[0]);
    co_await lazy::close(sv[1]);

    ctx.stop();
    co_return;
}

TEST_CASE("lazy socket IO: send/recv/shutdown/close", "[lazy_io]") {
    io_context ctx;
    ctx.co_spawn(test_socket_io(ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_multiple_lazy_ops(io_context &ctx) {
    int sv[2];
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    REQUIRE(ret == 0);

    const char *msg1 = "first";
    const char *msg2 = "second";

    co_await lazy::send(sv[0], msg1, std::strlen(msg1), 0);

    char buf1[32]{};
    co_await lazy::recv(sv[1], buf1, sizeof(buf1), 0);
    REQUIRE(std::memcmp(buf1, msg1, std::strlen(msg1)) == 0);

    co_await lazy::send(sv[0], msg2, std::strlen(msg2), 0);

    char buf2[32]{};
    co_await lazy::recv(sv[1], buf2, sizeof(buf2), 0);
    REQUIRE(std::memcmp(buf2, msg2, std::strlen(msg2)) == 0);

    co_await lazy::close(sv[0]);
    co_await lazy::close(sv[1]);

    ctx.stop();
    co_return;
}

TEST_CASE("lazy multiple sequential operations", "[lazy_io]") {
    io_context ctx;
    ctx.co_spawn(test_multiple_lazy_ops(ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_nop(io_context &ctx) {
    int res = co_await lazy::nop();
    REQUIRE(res == 0);
    ctx.stop();
    co_return;
}

TEST_CASE("lazy nop operation", "[lazy_io]") {
    io_context ctx;
    ctx.co_spawn(test_nop(ctx));
    ctx.start();
    ctx.join();
}

int main(int argc, char *argv[]) {
    return Catch::Session().run(argc, argv);
}
