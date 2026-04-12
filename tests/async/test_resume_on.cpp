#include <async/io_context.hpp>
#include <async/lazy_io.hpp>
#include <atomic>
#include <catch2/catch_test_macros.hpp>

static Task<void> test_basic_migration(io_context &src, io_context &dst) {
    REQUIRE(this_io_context() == &src);

    co_await lazy::resume_on(dst);

    REQUIRE(this_io_context() == &dst);

    src.stop();
    dst.stop();
    co_return;
}

TEST_CASE("resume_on: basic migration between two contexts", "[resume_on]") {
    io_context ctx[2];
    ctx[0].co_spawn(test_basic_migration(ctx[0], ctx[1]));
    ctx[0].start();
    ctx[1].start();
    ctx[0].join();
    ctx[1].join();
}

static Task<void> test_same_context(io_context &ctx) {
    REQUIRE(this_io_context() == &ctx);

    co_await lazy::resume_on(ctx);

    REQUIRE(this_io_context() == &ctx);

    ctx.stop();
    co_return;
}

TEST_CASE("resume_on: same context is no-op", "[resume_on]") {
    io_context ctx;
    ctx.co_spawn(test_same_context(ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_round_trip(io_context &ctx0, io_context &ctx1,
                                  io_context &ctx2) {
    REQUIRE(this_io_context() == &ctx0);

    co_await lazy::resume_on(ctx1);
    REQUIRE(this_io_context() == &ctx1);

    co_await lazy::resume_on(ctx2);
    REQUIRE(this_io_context() == &ctx2);

    co_await lazy::resume_on(ctx0);
    REQUIRE(this_io_context() == &ctx0);

    ctx0.stop();
    ctx1.stop();
    ctx2.stop();
    co_return;
}

TEST_CASE("resume_on: round trip across three contexts", "[resume_on]") {
    io_context ctx[3];
    ctx[0].co_spawn(test_round_trip(ctx[0], ctx[1], ctx[2]));
    ctx[0].start();
    ctx[1].start();
    ctx[2].start();
    ctx[0].join();
    ctx[1].join();
    ctx[2].join();
}

static std::atomic<int> shared_counter{0};

static Task<void> test_producer_consumer(io_context &io_ctx,
                                         io_context &cpu_ctx, int iterations) {
    for (int i = 0; i < iterations; ++i) {
        co_await lazy::resume_on(cpu_ctx);
        shared_counter.fetch_add(1, std::memory_order_relaxed);
        co_await lazy::resume_on(io_ctx);
    }
    io_ctx.stop();
    cpu_ctx.stop();
    co_return;
}

TEST_CASE("resume_on: producer-consumer pattern", "[resume_on]") {
    shared_counter.store(0, std::memory_order_relaxed);
    constexpr int iterations = 100;

    io_context io_ctx, cpu_ctx;
    io_ctx.co_spawn(test_producer_consumer(io_ctx, cpu_ctx, iterations));
    io_ctx.start();
    cpu_ctx.start();
    io_ctx.join();
    cpu_ctx.join();

    REQUIRE(shared_counter.load(std::memory_order_acquire) == iterations);
}

static Task<void> test_multi_coroutine_migration(io_context ctx[],
                                                 std::atomic<int> &arrived,
                                                 std::atomic<int> &done,
                                                 int count, int coro_id) {
    REQUIRE(this_io_context() == &ctx[0]);

    co_await lazy::resume_on(ctx[2]);
    REQUIRE(this_io_context() == &ctx[2]);

    arrived.fetch_add(1, std::memory_order_acq_rel);
    while (arrived.load(std::memory_order_acquire) < count) {
        co_await ctx[2].yield();
    }

    co_await lazy::resume_on(ctx[0]);
    REQUIRE(this_io_context() == &ctx[0]);

    if (done.fetch_add(1, std::memory_order_acq_rel) == count - 1) {
        for (int i = 0; i < 4; ++i) {
            ctx[i].stop();
        }
    }
    co_return;
}

TEST_CASE("resume_on: multiple coroutines migrate concurrently",
          "[resume_on]") {
    io_context ctx[4];
    std::atomic<int> arrived{0};
    std::atomic<int> done{0};
    constexpr int count = 3;

    for (int i = 0; i < count; ++i) {
        ctx[0].co_spawn(
            test_multi_coroutine_migration(ctx, arrived, done, count, i));
    }

    for (int i = 0; i < 4; ++i) {
        ctx[i].start();
    }
    for (int i = 0; i < 4; ++i) {
        ctx[i].join();
    }

    REQUIRE(done.load(std::memory_order_acquire) == count);
}
