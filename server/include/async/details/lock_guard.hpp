#pragma once

#include <concepts>

namespace detail {

template <class Mutex>
class [[nodiscard]] lock_guard final {
public:
    explicit lock_guard(Mutex &mtx) noexcept : mtx_(mtx) {
        static_assert(requires(Mutex &m) { { m.unlock() } noexcept; },
                      "Mutex type must have a noexcept unlock() method");
    }

    ~lock_guard() noexcept { mtx_.unlock(); }

    lock_guard(const lock_guard &) = delete;
    lock_guard &operator=(const lock_guard &) = delete;

private:
    Mutex &mtx_;
};

} // namespace detail
