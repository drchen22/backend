#pragma once

#include <async/details/trival_task.hpp>
#include <atomic>

class spin_mutex {
public:
    spin_mutex() noexcept = default;

    spin_mutex(const spin_mutex &) = delete;
    spin_mutex &operator=(const spin_mutex &) = delete;

    trival_task lock() noexcept {
        while (occupied_.exchange(true, std::memory_order_acquire)) {
            co_await std::suspend_always{};
        }
        co_return;
    }

    bool try_lock() noexcept {
        return !occupied_.exchange(true, std::memory_order_acquire);
    }

    void unlock() noexcept {
        occupied_.store(false, std::memory_order_release);
    }

private:
    std::atomic<bool> occupied_{false};
};
