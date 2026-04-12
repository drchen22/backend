#pragma once

#include <coroutine>
#include <cstdlib>
#include <utility>

class trival_task {
public:
    class promise_type final {
    public:
        trival_task get_return_object() noexcept {
            return trival_task{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        auto final_suspend() noexcept;

        void return_void() noexcept {}

        void unhandled_exception() noexcept { std::abort(); }

        std::coroutine_handle<> continuation{std::noop_coroutine()};
    };

    trival_task() noexcept = default;

    explicit trival_task(std::coroutine_handle<promise_type> h) noexcept
        : handle_(h) {}

    trival_task(trival_task &&other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}

    trival_task &operator=(trival_task &&other) noexcept {
        if (this != &other) [[likely]] {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    trival_task(const trival_task &) = delete;
    trival_task &operator=(const trival_task &) = delete;

    ~trival_task() {
        if (handle_) {
            handle_.destroy();
        }
    }

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> awaiting) noexcept {
        if (handle_) {
            handle_.promise().continuation = awaiting;
            return handle_;
        }
        return awaiting;
    }

    constexpr void await_resume() const noexcept {}

private:
    std::coroutine_handle<promise_type> handle_{nullptr};
};

struct trival_final_awaiter {
    static bool await_ready() noexcept { return false; }

    static std::coroutine_handle<>
    await_suspend(
        std::coroutine_handle<trival_task::promise_type> h) noexcept {
        return h.promise().continuation;
    }

    static void await_resume() noexcept {}
};

inline auto trival_task::promise_type::final_suspend() noexcept {
    return trival_final_awaiter{};
}
