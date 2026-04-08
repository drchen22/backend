#pragma once

#include <coroutine>
#include <cstdint>
#include <memory>

inline constexpr uintptr_t io_tag_bit = 1;

struct task_info {
    static constexpr uintptr_t tag_bits = 3;
    static constexpr uintptr_t raw_task_info_mask = ~uintptr_t((1 << tag_bits) - 1);

    std::coroutine_handle<> handel_;
    int32_t result;

    [[nodiscard]] uintptr_t as_user_data() const noexcept {
        return reinterpret_cast<uintptr_t>(this) | io_tag_bit;
    }

    static task_info *from_user_data(uintptr_t data) noexcept {
        return reinterpret_cast<task_info *>(data & raw_task_info_mask);
    }
};

struct link_task_info {
    static constexpr uintptr_t link_tag_bit = 2;

    std::coroutine_handle<> handel_;
    int32_t result;
};

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
    return reinterpret_cast<task_info *>(data & task_info::raw_task_info_mask);
}

inline uintptr_t encode_link_task_info(link_task_info *info) noexcept {
    return reinterpret_cast<uintptr_t>(info) | link_task_info::link_tag_bit
           | io_tag_bit;
}

inline bool is_link_task(uintptr_t data) noexcept {
    return (data & (io_tag_bit | link_task_info::link_tag_bit))
           == (io_tag_bit | link_task_info::link_tag_bit);
}

inline link_task_info *
decode_link_task_info(uintptr_t data) noexcept {
    return reinterpret_cast<link_task_info *>(
        data & ~uintptr_t(3));
}

inline std::coroutine_handle<> decode_post_handle(uintptr_t data) noexcept {
    return std::coroutine_handle<>::from_address(
        reinterpret_cast<void *>(data));
}
