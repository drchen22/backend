#pragma once

#include <coroutine>
#include <cstdint>
#include <memory>

inline constexpr uintptr_t io_tag_bit = 1;

struct task_info {
    std::coroutine_handle<> handel_;
    int32_t result;
};

/// @brief 链式 IO 操作的 task_info
/// bit 0 (io_tag_bit): IO 任务标记
/// bit 1 (link_tag_bit): 链式 IO 标记
/// 中间节点的 handel_ 为 nullptr，只有链尾节点持有协程句柄
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
    return reinterpret_cast<task_info *>(data & ~uintptr_t(1));
}

/// @brief 编码链式 task_info，bit 0 + bit 1 标识链式 IO
inline uintptr_t encode_link_task_info(link_task_info *info) noexcept {
    return reinterpret_cast<uintptr_t>(info) | link_task_info::link_tag_bit
           | io_tag_bit;
}

/// @brief 判断是否为链式 IO 任务
inline bool is_link_task(uintptr_t data) noexcept {
    return (data & (io_tag_bit | link_task_info::link_tag_bit))
           == (io_tag_bit | link_task_info::link_tag_bit);
}

/// @brief 解码链式 task_info 指针
inline link_task_info *
decode_link_task_info(uintptr_t data) noexcept {
    return reinterpret_cast<link_task_info *>(
        data & ~uintptr_t(3));
}

inline std::coroutine_handle<> decode_post_handle(uintptr_t data) noexcept {
    return std::coroutine_handle<>::from_address(
        reinterpret_cast<void *>(data));
}
