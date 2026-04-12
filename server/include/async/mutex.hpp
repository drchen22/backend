#pragma once

#include <async/details/lock_guard.hpp>
#include <async/details/thread_meta.hpp>
#include <async/io_context.hpp>
#include <atomic>
#include <coroutine>
#include <cstdint>

class mutex;

namespace detail {

class lock_awaiter {
public:
    explicit lock_awaiter(::mutex &mtx) noexcept : mtx_(mtx) {}

    bool await_ready() const noexcept { return ready_; }

    bool await_suspend(std::coroutine_handle<> current) noexcept;

    constexpr void await_resume() const noexcept {}

    void unlock_ahead() noexcept;

protected:
    friend class ::mutex;

    ::mutex &mtx_;
    lock_awaiter *next_{nullptr};
    std::coroutine_handle<> awoken_coro_{nullptr};
    io_context *resume_ctx_{nullptr};
    bool ready_{false};
};

class lock_guard_awaiter : public lock_awaiter {
public:
    using lock_awaiter::lock_awaiter;

    detail::lock_guard<::mutex> await_resume() noexcept;
};

} // namespace detail

class mutex {
public:
    mutex() noexcept = default;

    mutex(const mutex &) = delete;
    mutex &operator=(const mutex &) = delete;

    detail::lock_awaiter lock() noexcept {
        return detail::lock_awaiter{*this};
    }

    detail::lock_guard_awaiter lock_guard() noexcept {
        return detail::lock_guard_awaiter{*this};
    }

    bool try_lock() noexcept {
        uintptr_t expected = not_locked;
        return awaiting_.compare_exchange_strong(
            expected, locked_no_awaiting, std::memory_order_acquire);
    }

    void unlock() noexcept;

private:
    friend class detail::lock_awaiter;
    friend class detail::lock_guard_awaiter;

    static constexpr uintptr_t locked_no_awaiting = 0;
    static constexpr uintptr_t not_locked = 1;

    void register_awaiting(detail::lock_awaiter *awaiting) noexcept {
        awaiting->resume_ctx_ = detail::this_thread.ctx;
    }

    void enqueue(detail::lock_awaiter *awaiting) noexcept;

    std::atomic<uintptr_t> awaiting_{not_locked};
};

// ============================================================
// Inline implementations — after mutex is fully defined
// ============================================================

namespace detail {

inline bool lock_awaiter::await_suspend(std::coroutine_handle<> current) noexcept {
    awoken_coro_ = current;
    mtx_.enqueue(this);
    return !ready_;
}

inline void lock_awaiter::unlock_ahead() noexcept { mtx_.unlock(); }

inline auto lock_guard_awaiter::await_resume() noexcept
    -> detail::lock_guard<::mutex> {
    return detail::lock_guard<::mutex>(mtx_);
}

} // namespace detail

inline void mutex::enqueue(detail::lock_awaiter *awaiting) noexcept {
    register_awaiting(awaiting);

    uintptr_t current = awaiting_.load(std::memory_order_acquire);

    while (true) {
        if (current == not_locked) {
            if (awaiting_.compare_exchange_weak(
                    current, locked_no_awaiting,
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                awaiting->ready_ = true;
                return;
            }
            continue;
        }

        awaiting->next_ = reinterpret_cast<detail::lock_awaiter *>(current);
        if (awaiting_.compare_exchange_weak(
                current, reinterpret_cast<uintptr_t>(awaiting),
                std::memory_order_release, std::memory_order_relaxed)) {
            return;
        }
    }
}

inline void mutex::unlock() noexcept {
    uintptr_t current = awaiting_.load(std::memory_order_acquire);

    while (true) {
        if (current == locked_no_awaiting) {
            if (awaiting_.compare_exchange_weak(
                    current, not_locked,
                    std::memory_order_release, std::memory_order_relaxed)) {
                return;
            }
            continue;
        }

        auto *head = reinterpret_cast<detail::lock_awaiter *>(current);
        auto *next = head->next_;

        uintptr_t new_val = next ? reinterpret_cast<uintptr_t>(next) : locked_no_awaiting;
        if (awaiting_.compare_exchange_weak(
                current, new_val,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            auto *ctx = head->resume_ctx_;
            if (ctx) {
                ctx->co_spawn_auto(head->awoken_coro_);
            } else {
                head->awoken_coro_.resume();
            }
            return;
        }
    }
}
