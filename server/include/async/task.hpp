#pragma once

#include <cassert>
#include <concepts>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <memory>
#include <type_traits>



template <class T>
struct [[nodiscard]] Task;

namespace detail {
template <class T>
class task_promise_base;


template <class T>
struct task_final_awaiter {
    constexpr bool await_ready() noexcept {
        return false;
    }

    template <std::derived_from<task_promise_base<T>> Promise>
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<Promise> current) noexcept {
        return current.promise().parent_coro;
    }

    // 永远不会被resume
    void await_resume() noexcept {}
};

/*
 * @brief 当 current_task 完成时，唤醒其夫协程（调用者协程） VOID特化
 */
template <>
struct task_final_awaiter<void> {

    constexpr bool await_ready() noexcept {
        return false;
    }

    template <std::derived_from<task_promise_base<void>> Promise>
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<Promise> current) noexcept {
        auto &promise = current.promise();
        std::coroutine_handle<> continuation = promise.parent_coro;

        // 如果是 detached 状态，直接销毁当前协程
        if (promise.is_detached_flag == Promise::is_detached) {
            current.destroy();
        }
        // 返回父协程句柄
        return continuation;
    }

    // 永远不会被resume
    void await_resume() noexcept {}
};

template <class T>
class task_promise_base {
public:
    std::coroutine_handle<> parent_coro{std::noop_coroutine()};

    void set_parent(std::coroutine_handle<> parent) noexcept {
        parent_coro = parent;
    }

    task_promise_base() noexcept = default;
    constexpr std::suspend_always initial_suspend() noexcept {
        return {};
    }

    // 最终suspend时，唤醒父协程
    constexpr task_final_awaiter<T> final_suspend() noexcept {
        return {};
    }
    task_promise_base(const task_promise_base &) = delete;
        task_promise_base(task_promise_base &&) = delete;
        task_promise_base &operator=(const task_promise_base &) = delete;
        task_promise_base &operator=(task_promise_base &&) = delete;
};

template <class T>
class task_promise final : public task_promise_base<T> {
public:
    union {
        T value;
        std::exception_ptr exception_;
    };
    enum class value_state : uint8_t {
        mono,
        value,
        exception
    } state;

    Task<T> get_return_object() noexcept;

    task_promise() noexcept : state(value_state::mono) {};

    ~task_promise() noexcept {
        switch (state) {
        [[likely]] case value_state::value:
            value.~T();
            break;
        case value_state::mono:      break;
        case value_state::exception: exception_.~exception_ptr(); break;
        default:                     break;
        }
    }

    // 返回值处理
    template <typename Value>
        requires std::convertible_to<Value &&, T>
    void return_value(Value &&result) noexcept(
        std::is_nothrow_constructible_v<T, Value &&>) {
        std::construct_at(std::addressof(value), std::forward<Value>(result));
        state = value_state::value;
    }

    // 异常处理
    void unhandled_exception() noexcept {
        exception_ = std::current_exception();
        state = value_state::exception;
    }

    // 获取结果
    T &result() & {
        if (state == value_state::exception) [[unlikely]] {
            std::rethrow_exception(exception_);
        }
        assert(state == value_state::value);
        return value;
    }

    // 获取结果
    T &&result() && {
        if (state == value_state::exception) [[unlikely]] {
            std::rethrow_exception(exception_);
        }
        assert(state == value_state::value);
        return std::move(value);
    }
};

template <>
class task_promise<void> final : public task_promise_base<void> {
public:
    union {
        uintptr_t is_detached_flag;
        std::exception_ptr exception_;
    };

    // 全1
    static inline constexpr uintptr_t is_detached = -1ULL;

    task_promise() noexcept : is_detached_flag(0) {}

    ~task_promise() noexcept {
        if (is_detached_flag != is_detached) {
            exception_.~exception_ptr();
        }
    }

    Task<void> get_return_object() noexcept;

    constexpr void return_void() noexcept {}

    // 异常处理
    void unhandled_exception() noexcept {
        if (is_detached_flag == is_detached) {
            // detached 状态下，异常直接抛出
            std::rethrow_exception(exception_);
        } else {
            // 非 detached 状态下，异常存储起来，等待调用者获取
            exception_ = std::current_exception();
        }
    }

    // 获取结果（检查异常）
    void result() {
        if (this->exception_) [[unlikely]] {
            std::rethrow_exception(this->exception_);
        }
    }
};

template <class T>
class task_promise<T &> final : public task_promise_base<T &> {
public:
    task_promise() noexcept {};

    Task<T &> get_return_object() noexcept;

    // 异常处理
    void unhandled_exception() noexcept {
        this->exception_ = std::current_exception();
    }

    void return_value(T &result) noexcept {
        value_ = std::addressof(result);
    }

    // 获取结果
    T &result() {
        if (this->exception_) [[unlikely]] {
            std::rethrow_exception(this->exception_);
        }
        return *value_;
    }

private:
    T *value_ = nullptr;
    std::exception_ptr exception_;
};


template <class T>
struct task_awaiter {
    T _hdl;

    bool await_ready() {
        return _hdl.done();
    }

    void await_suspend(std::coroutine_handle<> awaiting) {
        _hdl.promise().parent_coro = awaiting;
    }

    decltype(auto) await_resume() {
        using PromiseType =
            typename std::remove_reference_t<decltype(_hdl.promise())>;
        if constexpr (std::is_void_v<typename PromiseType::value_type>) {
            return;
        } else {
            return _hdl.promise().value;
        }
    }
};
} // namespace detail

template <class T = void>
struct [[nodiscard]] Task {
    using value_type = T;
    using promise_type = detail::task_promise<T>;
    using PromiseTypeHandle = std::coroutine_handle<promise_type>;

private:
    struct awaiter_base {
        std::coroutine_handle<promise_type> handle_;

        explicit awaiter_base(
            std::coroutine_handle<promise_type> current) noexcept
            : handle_(current) {}

        [[nodiscard]] inline bool await_ready() const noexcept {
            return !handle_ || handle_.done();
        }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> awaiting_coro) noexcept {
            handle_.promise().set_parent(awaiting_coro);
            return handle_;
        }
    };

public:
    // 构造函数
    Task() noexcept = default;

    explicit Task(std::coroutine_handle<promise_type> current)
        : handel_(current) {}

    // 移动构造
    Task(Task &&other) noexcept : handel_(other.handel_) {
        other.handel_ = nullptr;
    }

    // 禁止拷贝构造
    Task(Task const &) = delete;
    Task &operator=(Task const &) = delete;

    // 移动赋值
    Task &operator=(Task &&other) noexcept {
        if (this != std::addressof(other)) [[likely]] {
            handel_ = other.handel_;
            other.handel_ = nullptr;
        }
        return *this;
    }

    // 析构函数
    ~Task() {
        if (handel_) {
            handel_.destroy();
        }
    }

    auto operator co_await() const & noexcept {
        struct awaiter : awaiter_base {
            using awaiter_base::awaiter_base;

            decltype(auto) await_resume() {
                assert(this->handle_ && "broken_promise");
                return this->handle_.promise().result();
            }
        };

        return awaiter{handel_};
    }

    auto operator co_await() const && noexcept {
        struct awaiter : awaiter_base {
            using awaiter_base::awaiter_base;

            decltype(auto) await_resume() {
                assert(this->handle_ && "broken_promise");
                return std::move(this->handle_.promise().result());
            }
        };

        return awaiter{handel_};
    }

    [[nodiscard]] auto when_ready() const noexcept {
        struct awaiter : awaiter_base {
            using awaiter_base::awaiter_base;

            constexpr void await_resume() const noexcept {}
        };

        return awaiter{handel_};
    }

    std::coroutine_handle<promise_type> get_handle() const noexcept {
        return handel_;
    }

    // 分离任务，不等待其完成
    void detach() noexcept {
        if constexpr (std::is_void_v<value_type>) {
            handel_.promise().is_detached_flag = promise_type::is_detached;
        }
        handel_ = nullptr;
    }

    friend void swap(Task &lhs, Task &rhs) noexcept {
        std::swap(lhs.handel_, rhs.handel_);
    }

private:
    PromiseTypeHandle handel_;
};

namespace detail {

template <typename T>
inline Task<T> task_promise<T>::get_return_object() noexcept {
    return Task<T>{std::coroutine_handle<task_promise>::from_promise(*this)};
}

inline Task<void> task_promise<void>::get_return_object() noexcept {
    return Task<void>{std::coroutine_handle<task_promise>::from_promise(*this)};
}

template <typename T>
inline Task<T &> task_promise<T &>::get_return_object() noexcept {
    return Task<T &>{std::coroutine_handle<task_promise>::from_promise(*this)};
}

} // namespace detail
