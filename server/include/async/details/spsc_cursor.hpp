#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

template <class cur_t, std::size_t Capacity, bool IsThreadSafe>
class spsc_cursor {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be power of 2");

public:
    spsc_cursor() noexcept = default;

    void push(std::size_t num) noexcept {
        if constexpr (IsThreadSafe) {
            tail_.fetch_add(
                static_cast<cur_t>(num), std::memory_order_release);
        } else {
            tail_ += static_cast<cur_t>(num);
        }
    }

    void pop(std::size_t num) noexcept {
        if constexpr (IsThreadSafe) {
            head_.fetch_add(
                static_cast<cur_t>(num), std::memory_order_release);
        } else {
            head_ += static_cast<cur_t>(num);
        }
    }

    [[nodiscard]] std::size_t head() const noexcept {
        if constexpr (IsThreadSafe) {
            return head_.load(std::memory_order_acquire);
        } else {
            return head_;
        }
    }

    [[nodiscard]] std::size_t tail() const noexcept {
        if constexpr (IsThreadSafe) {
            return tail_.load(std::memory_order_acquire);
        } else {
            return tail_;
        }
    }

    [[nodiscard]] bool is_empty() const noexcept {
        return head() == tail();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return tail() - head();
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

    [[nodiscard]] std::size_t mask() const noexcept {
        return tail() & (Capacity - 1);
    }

private:
    static constexpr cur_t kInitial = 0;

    using storage_t =
        std::conditional_t<IsThreadSafe, std::atomic<cur_t>, cur_t>;

    storage_t head_{kInitial};
    storage_t tail_{kInitial};
};
