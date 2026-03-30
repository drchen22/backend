#pragma once
#include "async/task.hpp"
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
class io_context;

namespace detail {
inline thread_local io_context *tls_ctx = nullptr;
}

class io_context {
public:
    io_context() = default;

    ~io_context() {
        stop();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    io_context(const io_context &) = delete;
    io_context &operator=(const io_context &) = delete;

    void start() {
        worker_ = std::thread([this] { run(); });
    }

    void join() {
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void stop() {
        stop_requested_.store(true, std::memory_order_release);
        cv_.notify_all();
    }

    // 只需要 co_spawn(Task<void>)：把协程塞进 context 执行（异常吞掉，保证 work 计数回收）
    void co_spawn(Task<void> t) {
        work_.fetch_add(1, std::memory_order_relaxed);

        auto wrapper = runner(std::move(t), this);
        auto h = wrapper.get_handle();
        wrapper.detach();
        post(h);
    }

    struct yield_awaitable {
        io_context &ctx;
        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) noexcept { ctx.post(h); }
        void await_resume() noexcept {}
    };

    yield_awaitable yield() noexcept { return yield_awaitable{*this}; }

private:
    friend io_context *this_io_context() noexcept;

    void post(std::coroutine_handle<> h) {
        {
            std::lock_guard lk(m_);
            queue_.push_back(h);
        }
        cv_.notify_one();
    }

    void work_done() {
        work_.fetch_sub(1, std::memory_order_relaxed);
        cv_.notify_all();
    }

    void run() {
        detail::tls_ctx = this;

        for (;;) {
            std::coroutine_handle<> h{};
            {
                std::unique_lock lk(m_);
                cv_.wait(lk, [&] {
                    return stop_requested_.load(std::memory_order_acquire) || !queue_.empty() ||
                           work_.load(std::memory_order_acquire) == 0;
                });

                if (queue_.empty()) {
                    if (stop_requested_.load(std::memory_order_acquire) ||
                        work_.load(std::memory_order_acquire) == 0) {
                        break;
                    }
                    continue;
                }

                h = queue_.front();
                queue_.pop_front();
            }

            if (h && !h.done()) {
                h.resume();
            }
        }

        detail::tls_ctx = nullptr;
    }

    static Task<void> runner(Task<void> t, io_context *ctx) {
        try {
            // TODO 为什么会报错
            // co_await std::move(t);
            co_await t;
        } catch (...) {
            // 顶层 fire-and-forget：这里选择吞掉异常，避免把 io_context 卡死。
        }
        ctx->work_done();
        co_return;
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::coroutine_handle<>> queue_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<std::size_t> work_{0};
    std::thread worker_;
};

inline io_context *this_io_context() noexcept { return detail::tls_ctx; }
