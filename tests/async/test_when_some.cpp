#include <catch2/catch_test_macros.hpp>
#include <async/when/when_some.hpp>
#include <async/io_context.hpp>
#include <chrono>
#include <string>

Task<int> make_int(int v) {
    co_return v;
}

Task<std::string> make_string(std::string s) {
    co_return std::move(s);
}

Task<void> make_void() {
    co_return;
}

Task<int> make_throw() {
    throw std::runtime_error("test error");
}

Task<int> slow_int(int v, int ms) {
    auto *ctx = this_io_context();
    if (ctx) {
        // 简单的延时模拟：通过 yield 让出执行
        for (int i = 0; i < ms; ++i) {
            co_await ctx->yield();
        }
    }
    co_return v;
}

TEST_CASE("some - basic with 2 of 3", "[when_some]") {
    io_context ctx;
    ctx.start();

    auto runner = [&]() -> Task<void> {
        auto results =
            co_await some(2, make_int(1), make_int(2), make_int(3));

        REQUIRE(results.size() == 2);
        REQUIRE(results[0].first < 3);
        REQUIRE(results[1].first < 3);

        for (const auto &[idx, var] : results) {
            REQUIRE(idx < 3);
            int val = std::get<0>(var);
            REQUIRE((val == 1 || val == 2 || val == 3));
        }
    };

    ctx.co_spawn(runner());
    ctx.stop();
    ctx.join();
}

TEST_CASE("some - min_complete equals task count", "[when_some]") {
    io_context ctx;
    ctx.start();

    auto runner = [&]() -> Task<void> {
        auto results =
            co_await some(3, make_int(10), make_int(20), make_int(30));

        REQUIRE(results.size() == 3);
    };

    ctx.co_spawn(runner());
    ctx.stop();
    ctx.join();
}

TEST_CASE("some - with void tasks", "[when_some]") {
    io_context ctx;
    ctx.start();

    auto runner = [&]() -> Task<void> {
        auto results = co_await some(2, make_void(), make_void(), make_void());

        REQUIRE(results.size() == 2);
        for (const auto &[idx, var] : results) {
            REQUIRE(var.valueless_by_exception() == false);
        }
    };

    ctx.co_spawn(runner());
    ctx.stop();
    ctx.join();
}

TEST_CASE("some - mixed types", "[when_some]") {
    io_context ctx;
    ctx.start();

    auto runner = [&]() -> Task<void> {
        auto results =
            co_await some(2, make_int(42), make_string("hello"), make_void());

        REQUIRE(results.size() == 2);
        for (const auto &[idx, var] : results) {
            REQUIRE((std::holds_alternative<int>(var) ||
                     std::holds_alternative<std::string>(var) ||
                     std::holds_alternative<std::monostate>(var)));
        }
    };

    ctx.co_spawn(runner());
    ctx.stop();
    ctx.join();
}

TEST_CASE("some - exception propagates", "[when_some]") {
    io_context ctx;
    ctx.start();

    auto runner = [&]() -> Task<void> {
        REQUIRE_THROWS_AS(
            co_await some(1, make_throw(), make_int(1)),
            std::runtime_error);
    };

    ctx.co_spawn(runner());
    ctx.stop();
    ctx.join();
}

TEST_CASE("some - min_complete is 1 behaves like any", "[when_some]") {
    io_context ctx;
    ctx.start();

    auto runner = [&]() -> Task<void> {
        auto results = co_await some(1, make_int(100), make_int(200));

        REQUIRE(results.size() == 1);
        REQUIRE(results[0].first < 2);
        int val = std::get<0>(results[0].second);
        REQUIRE((val == 100 || val == 200));
    };

    ctx.co_spawn(runner());
    ctx.stop();
    ctx.join();
}
