#pragma once
#include "promise_base.hpp"
#include <optional>
#include <utility>

template <class T, bool InitialSuspend = true>
struct Generator {
    using promise_type = promise_type_base_without_return<T, Generator, InitialSuspend>;
    using PromiseTypeHandle = std::coroutine_handle<promise_type>;

    using iterator = iterator<promise_type>;

    iterator begin() {
        return iterator(_coroHdl);
    }

    iterator end() {
        return {};
    }

    Generator(Generator const &) = delete;

    Generator(Generator &&other)
        : _coroHdl(std::exchange(other._coroHdl, nullptr)) {}

    ~Generator() {
        if (_coroHdl) {
            _coroHdl.destroy();
        }
    }

private:
    friend promise_type;

    explicit Generator(promise_type *p)
        : _coroHdl(PromiseTypeHandle::from_promise(*p)) {}

    PromiseTypeHandle _coroHdl;
};

// Base promise type for awaitable generators that can receive external signals
template <class T>
struct awaitable_promise_type_base {
    // Stores the most recent signal sent to the coroutine
    std::optional<T> _recentSignal;

    // Awaiter implementation for handling signal reception
    struct awaiter {
        // Reference to the signal storage in the promise
        std::optional<T> &_recentSignal;

        // Check if a signal is ready to be consumed
        bool await_ready() const {
            return _recentSignal.has_value();
        }

        // No suspension needed - we handle signal availability in await_ready
        void await_suspend(std::coroutine_handle<>) {}

        // Retrieve and consume the signal
        T await_resume() {
            // assert(_recentSignal.has_value()); // Debug check for signal presence
            auto tmp = *_recentSignal;      // Extract the signal value
            _recentSignal.reset();          // Clear the signal for next use
            return tmp;                     // Return the signal value
        }
    };

    // Transform awaited values into awaiters for signal-based communication
    [[nodiscard]] awaiter await_transform(T) {
        return awaiter{_recentSignal};
    }
};

// export template <class T, class U, bool InitialSuspend = true>
// struct [[nodiscard]] async_generator {
//     using promise_type =
//         promise_type_base<T, async_generator, InitialSuspend, awaitable_promise_type_base<U>>;
//     using PromiseTypeHandle = std::coroutine_handle<promise_type>;

//     T operator()() {
//         auto tmp{std::move(_coroHdl.promise()._value)};
//         _coroHdl.promise()._value.clear();
//         return tmp;
//     }

//     void SendSignal(U signal) {
//         _coroHdl.promise()._recentSignal = signal;
//         if (not _coroHdl.done()) {
//             _coroHdl.resume();
//         }
//     }

//     async_generator(async_generator const &) = delete;

//     async_generator(async_generator &&rhs)
//         : _coroHdl{std::exchange(rhs._coroHdl, nullptr)} {}

//     ~async_generator() {
//         if (_coroHdl) {
//             _coroHdl.destroy();
//         }
//     }

// private:
//     friend promise_type;

//     explicit async_generator(promise_type *p)
//         : _coroHdl(PromiseTypeHandle::from_promise(*p)) {}

//     PromiseTypeHandle _coroHdl;
// };
