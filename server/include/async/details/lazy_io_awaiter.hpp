#pragma once

#include <async/details/task_info.hpp>
#include <coroutine>
#include <cstdint>
#include <liburing.h>
#include <vector>

class io_context;
inline io_context *this_io_context() noexcept;

/// @brief 延迟 IO awaiter 基类
/// 实现延迟 IO 提交策略：SQE 在构造时分配和准备，但不立即提交给内核。
/// 多个 SQE 累积后在 io_context 事件循环中通过 io_uring_submit_and_wait
/// 以批处理方式提交，最大限度减少系统调用开销。
class lazy_io_awaiter {
public:
    lazy_io_awaiter();

    bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<> h) noexcept;

    int await_resume() noexcept {
        return info_.result;
    }

protected:
    io_uring_sqe *sqe() noexcept { return sqe_; }
    io_context *ctx() noexcept { return ctx_; }

private:
    io_uring_sqe *sqe_;
    io_context *ctx_;
    task_info info_{};

    friend class lazy_link_awaiter;
};

/// @brief 延迟链式 IO awaiter
/// 将多个 IO 操作通过 IOSQE_IO_LINK 标志原子地链接在一起。
/// 链中的所有操作作为一个原子单元提交：
/// - 如果任何操作失败，后续操作将被取消
/// - 协程仅在整个链完成后才恢复
/// - await_resume() 返回链中最后一个操作的结果
class lazy_link_awaiter {
public:
    lazy_link_awaiter(lazy_io_awaiter &&first, lazy_io_awaiter &&second);
    lazy_link_awaiter(lazy_link_awaiter &&chain, lazy_io_awaiter &&next);

    lazy_link_awaiter(lazy_link_awaiter &&) = default;
    lazy_link_awaiter &operator=(lazy_link_awaiter &&) = delete;
    lazy_link_awaiter(const lazy_link_awaiter &) = delete;
    lazy_link_awaiter &operator=(const lazy_link_awaiter &) = delete;

    bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<> h) noexcept;

    int await_resume() noexcept {
        return infos_.back().result;
    }

private:
    void apply_link_flags();

    io_context *ctx_;
    std::vector<io_uring_sqe *> sqes_;
    std::vector<link_task_info> infos_;
};

inline lazy_link_awaiter operator&&(lazy_io_awaiter &&a,
                                    lazy_io_awaiter &&b) {
    return lazy_link_awaiter(std::move(a), std::move(b));
}

inline lazy_link_awaiter operator&&(lazy_link_awaiter &&a,
                                    lazy_io_awaiter &&b) {
    return lazy_link_awaiter(std::move(a), std::move(b));
}
