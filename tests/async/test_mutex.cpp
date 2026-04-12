#include <catch2/catch_test_macros.hpp>
#include <async/io_context.hpp>
#include <async/mutex.hpp>
#include <async/spin_mutex.hpp>
#include <async/condition_variable.hpp>
#include <atomic>

static int shared_value = 0;

static Task<void> test_basic_lock(mutex &mtx, io_context &ctx) {
    co_await mtx.lock();
    shared_value = 42;
    mtx.unlock();
    ctx.stop();
    co_return;
}

TEST_CASE("mutex: basic lock/unlock", "[mutex]") {
    shared_value = 0;
    io_context ctx;
    mutex mtx;
    ctx.co_spawn(test_basic_lock(mtx, ctx));
    ctx.start();
    ctx.join();
    REQUIRE(shared_value == 42);
}

static Task<void> test_lock_guard_raii(mutex &mtx, io_context &ctx) {
    {
        auto lock = co_await mtx.lock_guard();
        shared_value = 100;
    }
    REQUIRE(shared_value == 100);
    ctx.stop();
    co_return;
}

TEST_CASE("mutex: lock_guard RAII", "[mutex]") {
    shared_value = 0;
    io_context ctx;
    mutex mtx;
    ctx.co_spawn(test_lock_guard_raii(mtx, ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_try_lock(mutex &mtx, io_context &ctx) {
    REQUIRE(mtx.try_lock() == true);
    shared_value = 77;
    mtx.unlock();

    REQUIRE(mtx.try_lock() == true);
    mtx.unlock();

    ctx.stop();
    co_return;
}

TEST_CASE("mutex: try_lock", "[mutex]") {
    io_context ctx;
    mutex mtx;
    ctx.co_spawn(test_try_lock(mtx, ctx));
    ctx.start();
    ctx.join();
}

static Task<void> test_contention_worker(
    mutex &mtx, io_context &, int iterations, std::atomic<int> &counter) {
    for (int i = 0; i < iterations; ++i) {
        co_await mtx.lock();
        counter.fetch_add(1, std::memory_order_relaxed);
        mtx.unlock();
    }
    co_return;
}

static Task<void> test_contention_driver(
    mutex &mtx, io_context ctx[], int num_ctx, int coroutines_per_ctx,
    int iterations, std::atomic<int> &counter) {
    for (int i = 0; i < coroutines_per_ctx; ++i) {
        for (int j = 0; j < num_ctx; ++j) {
            ctx[j].co_spawn(test_contention_worker(
                mtx, ctx[j], iterations, std::ref(counter)));
        }
    }
    co_return;
}

TEST_CASE("mutex: multi-context contention", "[mutex]") {
    constexpr int num_ctx = 4;
    constexpr int coroutines_per_ctx = 5;
    constexpr int iterations = 50;

    io_context ctx[num_ctx];
    mutex mtx;
    std::atomic<int> counter{0};

    for (int i = 0; i < coroutines_per_ctx; ++i) {
        for (int j = 0; j < num_ctx; ++j) {
            ctx[j].co_spawn(test_contention_worker(
                mtx, ctx[j], iterations, std::ref(counter)));
        }
    }

    for (int i = 0; i < num_ctx; ++i) {
        ctx[i].start();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    for (int i = 0; i < num_ctx; ++i) {
        ctx[i].stop();
    }
    for (int i = 0; i < num_ctx; ++i) {
        ctx[i].join();
    }

    int expected = num_ctx * coroutines_per_ctx * iterations;
    REQUIRE(counter.load(std::memory_order_acquire) == expected);
}

static Task<void> test_spin_mutex_basic(spin_mutex &smtx, io_context &ctx) {
    co_await smtx.lock();
    shared_value = 55;
    smtx.unlock();
    ctx.stop();
    co_return;
}

TEST_CASE("spin_mutex: basic lock/unlock", "[spin_mutex]") {
    shared_value = 0;
    io_context ctx;
    spin_mutex smtx;
    ctx.co_spawn(test_spin_mutex_basic(smtx, ctx));
    ctx.start();
    ctx.join();
    REQUIRE(shared_value == 55);
}

static Task<void> test_spin_mutex_try_lock(spin_mutex &smtx, io_context &ctx) {
    REQUIRE(smtx.try_lock() == true);
    shared_value = 66;
    smtx.unlock();
    ctx.stop();
    co_return;
}

TEST_CASE("spin_mutex: try_lock", "[spin_mutex]") {
    io_context ctx;
    spin_mutex smtx;
    ctx.co_spawn(test_spin_mutex_try_lock(smtx, ctx));
    ctx.start();
    ctx.join();
    REQUIRE(shared_value == 66);
}

static Task<void> test_cv_producer(
    mutex &mtx, condition_variable &cv, bool &ready,
    int &data, io_context &ctx) {
    co_await mtx.lock();
    data = 42;
    ready = true;
    mtx.unlock();
    cv.notify_one();
    ctx.stop();
    co_return;
}

static Task<void> test_cv_consumer(
    mutex &mtx, condition_variable &cv, bool &ready,
    int &data, io_context &ctx) {
    co_await mtx.lock();
    while (!ready) {
        co_await cv.wait(mtx, [&ready] { return ready; });
        co_await mtx.lock();
    }
    REQUIRE(data == 42);
    mtx.unlock();
    ctx.stop();
    co_return;
}

TEST_CASE("condition_variable: producer-consumer", "[cv]") {
    io_context ctx[2];
    mutex mtx;
    condition_variable cv;
    bool ready = false;
    int data = 0;

    ctx[0].co_spawn(test_cv_consumer(mtx, cv, ready, data, ctx[0]));
    ctx[1].co_spawn(test_cv_producer(mtx, cv, ready, data, ctx[1]));

    ctx[0].start();
    ctx[1].start();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    ctx[0].stop();
    ctx[1].stop();
    ctx[0].join();
    ctx[1].join();

    REQUIRE(data == 42);
}

static Task<void> test_cv_notify_all_worker(
    mutex &mtx, condition_variable &cv, std::atomic<int> &counter,
    int target, io_context &ctx) {
    co_await mtx.lock();
    while (counter.load(std::memory_order_acquire) < target) {
        co_await cv.wait(mtx, [&counter, target] {
            return counter.load(std::memory_order_acquire) >= target;
        });
        co_await mtx.lock();
    }
    counter.fetch_add(1, std::memory_order_acq_rel);
    mtx.unlock();
    ctx.stop();
    co_return;
}

static Task<void> test_cv_notify_all_signaler(
    mutex &mtx, condition_variable &cv, std::atomic<int> &counter,
    int target, io_context &ctx) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    co_await mtx.lock();
    counter.store(target, std::memory_order_release);
    mtx.unlock();
    cv.notify_all();
    ctx.stop();
    co_return;
}

TEST_CASE("condition_variable: notify_all", "[cv]") {
    constexpr int num_waiters = 5;
    io_context ctx[3];
    mutex mtx;
    condition_variable cv;
    std::atomic<int> counter{0};

    for (int i = 0; i < num_waiters; ++i) {
        ctx[i % 3].co_spawn(test_cv_notify_all_worker(
            mtx, cv, counter, 1, ctx[i % 3]));
    }
    ctx[0].co_spawn(test_cv_notify_all_signaler(mtx, cv, counter, 1, ctx[0]));

    for (int i = 0; i < 3; ++i) {
        ctx[i].start();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    for (int i = 0; i < 3; ++i) {
        ctx[i].stop();
    }
    for (int i = 0; i < 3; ++i) {
        ctx[i].join();
    }

    REQUIRE(counter.load(std::memory_order_acquire) == num_waiters + 1);
}
