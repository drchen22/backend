#pragma once

#include <async/details/task_info.hpp>
#include <coroutine>
#include <cstdint>
#include <liburing.h>

struct io_awaitable {
    bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        info_ = new task_info{h, 0};
        io_uring_sqe_set_data64(
            sqe_, static_cast<uint64_t>(encode_io_task_info(info_)));
    }

    int await_resume() noexcept {
        int res = info_->result;
        delete info_;
        info_ = nullptr;
        return res;
    }

    task_info *info_ = nullptr;
    io_uring_sqe *sqe_ = nullptr;
};
