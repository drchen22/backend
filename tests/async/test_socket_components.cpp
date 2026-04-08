#include <catch2/catch_test_macros.hpp>
#include <net/acceptor.hpp>
#include <net/inet_address.hpp>
#include <net/socket.hpp>
#include <async/io_context.hpp>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

TEST_CASE("inet_address default constructor", "[inet_address]") {
    net::inet_address addr;
    REQUIRE(addr.family() == AF_INET);
    REQUIRE(addr.port() == 0);
}

TEST_CASE("inet_address port-only constructor", "[inet_address]") {
    net::inet_address addr(8080);
    REQUIRE(addr.family() == AF_INET);
    REQUIRE(addr.port() == 8080);
    REQUIRE(addr.is_ipv4());
    REQUIRE_FALSE(addr.is_ipv6());
}

TEST_CASE("inet_address ip+port constructor", "[inet_address]") {
    net::inet_address addr("127.0.0.1", 9090);
    REQUIRE(addr.family() == AF_INET);
    REQUIRE(addr.port() == 9090);
    REQUIRE(addr.ip() == "127.0.0.1");
}

TEST_CASE("inet_address to_string", "[inet_address]") {
    net::inet_address addr("192.168.1.1", 443);
    REQUIRE(addr.to_string() == "192.168.1.1:443");
}

TEST_CASE("inet_address resolve localhost", "[inet_address]") {
    auto addr = net::inet_address::resolve("127.0.0.1", 80);
    REQUIRE(addr.family() == AF_INET);
    REQUIRE(addr.port() == 80);
}

TEST_CASE("socket create_tcp", "[socket]") {
    int fd = net::socket::create_tcp(AF_INET);
    REQUIRE(fd >= 0);
    ::close(fd);
}

TEST_CASE("socket move semantics", "[socket]") {
    int raw_fd = net::socket::create_tcp(AF_INET);
    REQUIRE(raw_fd >= 0);

    net::socket s1(raw_fd);
    REQUIRE(s1.fd() == raw_fd);
    REQUIRE(s1.valid());

    net::socket s2(std::move(s1));
    REQUIRE(s2.fd() == raw_fd);
    REQUIRE_FALSE(s1.valid());

    int released = s2.release();
    REQUIRE(released == raw_fd);
    REQUIRE_FALSE(s2.valid());
    ::close(released);
}

TEST_CASE("acceptor creates and listens", "[acceptor]") {
    net::inet_address addr(0);
    net::acceptor ac(addr);
    REQUIRE(ac.fd() >= 0);
}

static Task<void> server_echo(net::acceptor &ac) {
    int conn_fd = co_await ac.accept();
    REQUIRE(conn_fd >= 0);

    net::socket cli(conn_fd);
    char buf[64]{};
    int n = co_await cli.recv(buf, sizeof(buf));
    REQUIRE(n > 0);
    REQUIRE(std::string_view(buf, static_cast<size_t>(n)) == "hello");

    co_await cli.send("world", 5);
    co_await cli.close();
}

static Task<void> client_connect_send_recv() {
    net::socket sock(net::socket::create_tcp(AF_INET));
    REQUIRE(sock.valid());

    net::inet_address server_addr("127.0.0.1", 19999);
    co_await sock.connect(server_addr);
    co_await sock.send("hello", 5);

    char buf[64]{};
    int n = co_await sock.recv(buf, sizeof(buf));
    REQUIRE(n > 0);
    REQUIRE(std::string_view(buf, static_cast<size_t>(n)) == "world");

    co_await sock.close();
}

TEST_CASE("acceptor+socket echo via io_context", "[acceptor][socket]") {
    net::acceptor ac(net::inet_address(19999));

    io_context ctx;
    ctx.start();

    ctx.co_spawn(server_echo(ac));
    ctx.co_spawn(client_connect_send_recv());

    std::this_thread::sleep_for(200ms);
    ctx.stop();
    ctx.join();
}
