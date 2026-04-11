#include <catch2/catch_test_macros.hpp>
#include <async/stop_token.hpp>
#include <async/task.hpp>
#include <async/io_context.hpp>
#include <atomic>
#include <chrono>

using namespace std::chrono_literals;

TEST_CASE("stop_source basic operations", "[stop_token]") {
    stop_source src;

    REQUIRE(src.stop_possible());
    REQUIRE_FALSE(src.stop_requested());

    REQUIRE(src.request_stop());
    REQUIRE(src.stop_requested());

    REQUIRE_FALSE(src.request_stop());
}

TEST_CASE("stop_source copy shares state", "[stop_token]") {
    stop_source src1;
    stop_source src2 = src1;

    REQUIRE(src1.stop_possible());
    REQUIRE(src2.stop_possible());

    (void)src1.request_stop();

    REQUIRE(src1.stop_requested());
    REQUIRE(src2.stop_requested());
}

TEST_CASE("stop_source move transfers ownership", "[stop_token]") {
    stop_source src1;
    stop_source src2 = std::move(src1);

    REQUIRE_FALSE(src1.stop_possible());
    REQUIRE(src2.stop_possible());

    (void)src2.request_stop();
    REQUIRE(src2.stop_requested());
}

TEST_CASE("stop_token from stop_source", "[stop_token]") {
    stop_source src;
    stop_token tok = src.get_token();

    REQUIRE(tok.stop_possible());
    REQUIRE_FALSE(tok.stop_requested());

    (void)src.request_stop();
    REQUIRE(tok.stop_requested());
}

TEST_CASE("stop_token default construction", "[stop_token]") {
    stop_token tok;

    REQUIRE_FALSE(tok.stop_possible());
    REQUIRE_FALSE(tok.stop_requested());
}

TEST_CASE("stop_token copy and move", "[stop_token]") {
    stop_source src;
    stop_token tok1 = src.get_token();
    stop_token tok2 = tok1;

    REQUIRE(tok1.stop_possible());
    REQUIRE(tok2.stop_possible());

    (void)src.request_stop();
    REQUIRE(tok1.stop_requested());
    REQUIRE(tok2.stop_requested());

    stop_token tok3 = std::move(tok2);
    REQUIRE(tok3.stop_requested());
}

TEST_CASE("stop_token independent of source lifetime", "[stop_token]") {
    stop_token tok;
    {
        stop_source src;
        tok = src.get_token();
        REQUIRE(tok.stop_possible());
    }

    REQUIRE_FALSE(tok.stop_requested());
}

TEST_CASE("stop_callback fires on request_stop", "[stop_token]") {
    stop_source src;
    bool fired = false;

    stop_callback cb(src.get_token(), [&] { fired = true; });

    REQUIRE_FALSE(fired);
    (void)src.request_stop();
    REQUIRE(fired);
}

TEST_CASE("stop_callback fires immediately if already stopped", "[stop_token]") {
    stop_source src;
    (void)src.request_stop();

    bool fired = false;
    stop_callback cb(src.get_token(), [&] { fired = true; });
    REQUIRE(fired);
}

TEST_CASE("stop_callback deregisters on destruction", "[stop_token]") {
    stop_source src;
    int count = 0;

    {
        stop_callback cb(src.get_token(), [&] { count++; });
    }

    (void)src.request_stop();
    REQUIRE(count == 0);
}

TEST_CASE("stop_callback multiple callbacks", "[stop_token]") {
    stop_source src;
    int a = 0, b = 0, c = 0;

    stop_callback cb1(src.get_token(), [&] { a = 1; });
    stop_callback cb2(src.get_token(), [&] { b = 2; });
    stop_callback cb3(src.get_token(), [&] { c = 3; });

    (void)src.request_stop();

    REQUIRE(a == 1);
    REQUIRE(b == 2);
    REQUIRE(c == 3);
}

TEST_CASE("stop_callback with stop_token constructor", "[stop_token]") {
    stop_source src;
    auto tok = src.get_token();
    bool fired = false;

    stop_callback cb(tok, [&] { fired = true; });
    (void)src.request_stop();
    REQUIRE(fired);
}

TEST_CASE("stop_token survives source destruction", "[stop_token]") {
    stop_token tok;
    {
        stop_source src;
        tok = src.get_token();
        (void)src.request_stop();
    }
    REQUIRE(tok.stop_requested());
}

TEST_CASE("stop_callback with lambda from token", "[stop_token]") {
    stop_source src;
    stop_token tok = src.get_token();

    int val = 0;
    stop_callback cb(tok, [&val] { val = 42; });

    (void)src.request_stop();
    REQUIRE(val == 42);
}

Task<void> child_with_cancellation(stop_token token, std::atomic<int> &counter) {
    stop_callback cb(token, [&counter] { counter.fetch_add(100, std::memory_order_relaxed); });

    while (!token.stop_requested()) {
        co_await this_io_context()->yield();
    }

    counter.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

Task<void> parent_cancels_child(std::atomic<int> &counter) {
    stop_source src;
    auto token = src.get_token();

    co_spawn(child_with_cancellation(token, counter));

    co_await this_io_context()->yield();
    co_await this_io_context()->yield();

    (void)src.request_stop();
    co_return;
}

TEST_CASE("stop_token coroutine cancellation", "[stop_token]") {
    io_context ctx;
    ctx.start();

    std::atomic<int> counter{0};
    ctx.co_spawn(parent_cancels_child(counter));
    ctx.stop();
    ctx.join();

    REQUIRE(counter.load() == 101);
}

Task<void> multi_level_level3(stop_token token, std::atomic<int> &counter) {
    stop_callback cb(token, [&] { counter.fetch_add(4, std::memory_order_relaxed); });

    for (int i = 0; i < 100 && !token.stop_requested(); ++i) {
        co_await this_io_context()->yield();
    }

    counter.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

Task<void> multi_level_level2(stop_token token, std::atomic<int> &counter) {
    stop_callback cb(token, [&] { counter.fetch_add(2, std::memory_order_relaxed); });

    co_spawn(multi_level_level3(token, counter));

    for (int i = 0; i < 100 && !token.stop_requested(); ++i) {
        co_await this_io_context()->yield();
    }

    counter.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

Task<void> multi_level_level1(std::atomic<int> &counter) {
    stop_source src;

    co_spawn(multi_level_level2(src.get_token(), counter));

    co_await this_io_context()->yield();
    co_await this_io_context()->yield();

    (void)src.request_stop();
    co_return;
}

TEST_CASE("stop_token multi-level coroutine cancellation", "[stop_token]") {
    io_context ctx;
    ctx.start();

    std::atomic<int> counter{0};
    ctx.co_spawn(multi_level_level1(counter));
    ctx.stop();
    ctx.join();

    REQUIRE(counter.load() == 8);
}

Task<void> token_lifetime_task(std::atomic<bool> &task_done) {
    stop_token tok;
    {
        stop_source src;
        tok = src.get_token();
        (void)src.request_stop();
    }

    REQUIRE(tok.stop_requested());
    task_done.store(true, std::memory_order_release);
    co_return;
}

TEST_CASE("stop_token valid after source destroyed in coroutine", "[stop_token]") {
    io_context ctx;
    ctx.start();

    std::atomic<bool> task_done{false};
    ctx.co_spawn(token_lifetime_task(task_done));
    ctx.stop();
    ctx.join();

    REQUIRE(task_done.load());
}

Task<void> no_cancel_task(stop_token token, std::atomic<int> &counter) {
    stop_callback cb(token, [&] { counter.fetch_add(100, std::memory_order_relaxed); });

    for (int i = 0; i < 3; ++i) {
        co_await this_io_context()->yield();
    }

    counter.fetch_add(1, std::memory_order_relaxed);
    co_return;
}

Task<void> parent_no_cancel(std::atomic<int> &counter) {
    stop_source src;
    co_spawn(no_cancel_task(src.get_token(), counter));

    for (int i = 0; i < 5; ++i) {
        co_await this_io_context()->yield();
    }

    co_return;
}

TEST_CASE("stop_token no cancellation path", "[stop_token]") {
    io_context ctx;
    ctx.start();

    std::atomic<int> counter{0};
    ctx.co_spawn(parent_no_cancel(counter));
    ctx.stop();
    ctx.join();

    REQUIRE(counter.load() == 1);
}

Task<void> callback_with_capture(stop_token token, int &result) {
    int local = 10;
    stop_callback cb(token, [&result, &local] { result = local * 2; });

    for (int i = 0; i < 100 && !token.stop_requested(); ++i) {
        co_await this_io_context()->yield();
    }

    co_return;
}

Task<void> test_callback_capture(int &result) {
    stop_source src;
    co_spawn(callback_with_capture(src.get_token(), result));

    co_await this_io_context()->yield();
    co_await this_io_context()->yield();

    (void)src.request_stop();
    co_return;
}

TEST_CASE("stop_callback captures references correctly", "[stop_token]") {
    io_context ctx;
    ctx.start();

    int result = 0;
    ctx.co_spawn(test_callback_capture(result));
    ctx.stop();
    ctx.join();

    REQUIRE(result == 20);
}

TEST_CASE("multiple stop_sources share same state", "[stop_token]") {
    stop_source src1;
    stop_source src2 = src1;

    (void)src2.request_stop();

    REQUIRE(src1.stop_requested());
    REQUIRE(src2.stop_requested());

    auto tok1 = src1.get_token();
    auto tok2 = src2.get_token();

    REQUIRE(tok1.stop_requested());
    REQUIRE(tok2.stop_requested());
}

TEST_CASE("stop_source swap", "[stop_token]") {
    stop_source src1;
    stop_source src2;

    (void)src1.request_stop();

    src1.swap(src2);

    REQUIRE_FALSE(src1.stop_requested());
    REQUIRE(src2.stop_requested());
}

TEST_CASE("stop_token swap", "[stop_token]") {
    stop_source src;
    stop_token tok1 = src.get_token();
    stop_token tok2;

    tok1.swap(tok2);

    REQUIRE_FALSE(tok1.stop_possible());
    REQUIRE(tok2.stop_possible());

    (void)src.request_stop();
    REQUIRE(tok2.stop_requested());
}
