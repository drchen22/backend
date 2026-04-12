#pragma once

#include <async/mutex.hpp>
#include <coroutine>
#include <functional>
#include <queue>

class condition_variable {
public:
    condition_variable() noexcept = default;

    condition_variable(const condition_variable &) = delete;
    condition_variable &operator=(const condition_variable &) = delete;

    template <class Pred>
    class wait_awaiter {
    public:
        wait_awaiter(
            condition_variable &cv, class mutex &mtx, Pred pred) noexcept
            : cv_(cv), mtx_(mtx), pred_(std::move(pred)) {}

        bool await_ready() const noexcept { return pred_(); }

        bool await_suspend(std::coroutine_handle<> current) noexcept;

        constexpr void await_resume() const noexcept {}

    private:
        condition_variable &cv_;
        class mutex &mtx_;
        Pred pred_;
        std::coroutine_handle<> coro_{nullptr};
        io_context *resume_ctx_{nullptr};
    };

    template <class Pred>
    wait_awaiter<Pred> wait(class mutex &mtx, Pred pred) {
        return wait_awaiter<Pred>(*this, mtx, std::move(pred));
    }

    void notify_one() noexcept {
        if (!waiters_.empty()) {
            auto w = waiters_.front();
            waiters_.pop();
            if (w.resume_ctx) {
                w.resume_ctx->co_spawn_auto(w.coro);
            } else {
                w.coro.resume();
            }
        }
    }

    void notify_all() noexcept {
        while (!waiters_.empty()) {
            auto w = waiters_.front();
            waiters_.pop();
            if (w.resume_ctx) {
                w.resume_ctx->co_spawn_auto(w.coro);
            } else {
                w.coro.resume();
            }
        }
    }

private:
    struct waiter_info {
        std::coroutine_handle<> coro;
        io_context *resume_ctx;
    };

    void enqueue(std::coroutine_handle<> coro, io_context *ctx) {
        waiters_.push({coro, ctx});
    }

    std::queue<waiter_info> waiters_;
};

template <class Pred>
bool condition_variable::wait_awaiter<Pred>::await_suspend(
    std::coroutine_handle<> current) noexcept {
    coro_ = current;
    resume_ctx_ = detail::this_thread.ctx;
    cv_.enqueue(current, resume_ctx_);
    mtx_.unlock();
    return true;
}
