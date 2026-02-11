#pragma once

#include <coroutine>
#include <exception>
template <class T, class G, bool InitialSuspend, class... Bases>
struct promise_type_base : Bases... {
    using value_type = T;
    T _value;

    std::coroutine_handle<> _continuation;

    std::suspend_always yield_value(T value) {
        _value = value;
        return {};
    }

    G get_return_object() {
        return G{this};
    }

    auto initial_suspend() {
        if constexpr (InitialSuspend) {
            return std::suspend_always{};
        } else {
            return std::suspend_never{};
        }
    }

    std::suspend_always final_suspend() noexcept {

    }

    void return_value(T value) {
        _value = value;
    }

    void unhandled_exception() {
        std::terminate();
    }

    static auto get_return_object_on_allocation_failure() {
        return G{nullptr};
    }
};

// promise_type_base的特化版本，用于void类型
template <class G, class... Bases, bool InitialSuspend>
struct promise_type_base<void, G, InitialSuspend, Bases...> : Bases... {
    using value_type = void;
        std::coroutine_handle<> _continuation;

    auto yield_value() {
        return std::suspend_always{};
    }

    G get_return_object() {
        return G{this};
    }

    auto initial_suspend() {
        if constexpr (InitialSuspend) {
            return std::suspend_always{};
        } else {
            return std::suspend_never{};
        }
    }

    std::suspend_always final_suspend() noexcept {

    }

    void return_void() {
    }

    void unhandled_exception() {
        std::terminate();
    }

    static auto get_return_object_on_allocation_failure() {
        return G{nullptr};
    }
};

template <class T, class G, bool InitialSuspend, class... Bases>
struct promise_type_base_without_return : Bases... {
    using value_type = void;
    T _value;
        std::coroutine_handle<> _continuation;

    auto yield_value(T value) {
        _value = value;
        return std::suspend_always{};
    }

    G get_return_object() {
        return G{this};
    }

    auto initial_suspend() {
        if constexpr (InitialSuspend) {
            return std::suspend_always{};
        } else {
            return std::suspend_never{};
        }
    }

    std::suspend_always final_suspend() noexcept {
        return {};
    }

    void return_void() {
    }

    void unhandled_exception() {
        std::terminate();
    }

    static auto get_return_object_on_allocation_failure() {
        return G{nullptr};
    }
};

template <class PT>
struct iterator {
    std::coroutine_handle<PT> _coroHdl{nullptr};

    void resume() {
        if (not _coroHdl.done()) {
            _coroHdl.resume();
        }
    }

    iterator() = default;

    iterator(std::coroutine_handle<PT> hdl) : _coroHdl(hdl) {
        resume();
    }

    void operator++() {
        resume();
    }

    bool operator==(iterator const &other) const {
        return _coroHdl.done();
    }

    auto const &operator*() const {
        return _coroHdl.promise()._value;
    }
};
