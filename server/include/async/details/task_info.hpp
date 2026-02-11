#pragma once

#include <coroutine>
#include <cstdint>
#include <memory>

struct [[nodiscard]] task_info {
    std::coroutine_handle<> handel_;

    int32_t result;

    [[nodiscard]] uint64_t as_user_data() const noexcept {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
    }
};

inline constexpr uintptr_t raw_task_info_mask =
    ~uintptr_t(alignof(task_info) - 1);

static_assert((~raw_task_info_mask) == 0x7 , "task_info must be 8-byte aligned");

inline task_info* raw_task_info_from_user_data(uintptr_t info) noexcept {
    return std::assume_aligned<alignof(task_info)>(reinterpret_cast<task_info*>(info & raw_task_info_mask));
}
