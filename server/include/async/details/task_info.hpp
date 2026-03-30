#pragma once

#include <coroutine>
#include <cstdint>
#include <memory>

struct task_info {
    std::coroutine_handle<> handel_;
    int32_t result;
};

inline constexpr uintptr_t io_tag_bit = 1;

inline uintptr_t encode_post_handle(std::coroutine_handle<> h) noexcept {
    return reinterpret_cast<uintptr_t>(h.address());
}

inline uintptr_t encode_io_task_info(task_info *info) noexcept {
    return reinterpret_cast<uintptr_t>(info) | io_tag_bit;
}

inline bool is_io_task(uintptr_t data) noexcept {
    return (data & io_tag_bit) != 0;
}

inline task_info *decode_io_task_info(uintptr_t data) noexcept {
    return reinterpret_cast<task_info *>(data & ~uintptr_t(1));
}

inline std::coroutine_handle<> decode_post_handle(uintptr_t data) noexcept {
    return std::coroutine_handle<>::from_address(
        reinterpret_cast<void *>(data));
}
