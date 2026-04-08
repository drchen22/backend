#pragma once

#include "async/task.hpp"
#include "async/details/task_info.hpp"
#include "async/details/worker_meta.hpp"
#include <coroutine>
#include <cstdint>
#include <liburing.h>
#include <print>
#include <thread>
#include <utility>

class io_context;

namespace detail {
inline thread_local io_context *tls_ctx = nullptr;
}

class io_context {
public:
    explicit io_context(
        unsigned entries = 256, unsigned submit_threshold = 1)
        : meta_(submit_threshold) {
        io_uring_queue_init(entries, &ring_, 0);
    }

    ~io_context() {
        stop();
        if (worker_.joinable()) {
            worker_.join();
        }
        io_uring_queue_exit(&ring_);
    }

    io_context(const io_context &) = delete;
    io_context &operator=(const io_context &) = delete;

    void start() {
        worker_ = std::thread([this] { run(); });
    }

    void join() {
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void stop() {
        stop_requested_.store(true, std::memory_order_release);
        submit_wakeup();
    }

    void co_spawn(Task<void> t) {
        work_.fetch_add(1, std::memory_order_relaxed);
        auto wrapper = runner(std::move(t), this);
        auto h = wrapper.get_handle();
        wrapper.detach();
        post(h);
    }

    struct yield_awaitable {
        io_context &ctx;
        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) noexcept {
            ctx.post(h);
        }
        void await_resume() noexcept {}
    };

    yield_awaitable yield() noexcept { return yield_awaitable{*this}; }

    io_uring *ring() noexcept { return &ring_; }

    worker_meta &meta() noexcept { return meta_; }

    void submit_wakeup() {
        auto *sqe = io_uring_get_sqe(&ring_);
        if (sqe) {
            io_uring_prep_nop(sqe);
            io_uring_sqe_set_data(sqe, nullptr);
            meta_.notify_sqe_ready(&ring_);
            meta_.flush(&ring_);
        }
    }

private:
    friend io_context *this_io_context() noexcept;

    void post(std::coroutine_handle<> h) {
        auto *sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            meta_.flush(&ring_);
            sqe = io_uring_get_sqe(&ring_);
        }
        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data64(
            sqe, static_cast<uint64_t>(encode_post_handle(h)));
        meta_.notify_sqe_ready(&ring_);
        meta_.flush(&ring_);
    }

    void work_done() {
        if (work_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            submit_wakeup();
        }
    }

    void run() {
        detail::tls_ctx = this;
        meta_.reset();

        while (!stop_requested_.load(std::memory_order_acquire)) {
            int ret = meta_.submit_and_wait(&ring_, 1);
            if (ret < 0) {
                continue;
            }

            unsigned head;
            struct io_uring_cqe *cqe;
            unsigned count = 0;

            io_uring_for_each_cqe(&ring_, head, cqe) {
                ++count;
                uint64_t data = static_cast<uint64_t>(
                    reinterpret_cast<uintptr_t>(
                        io_uring_cqe_get_data(cqe)));

                if (data == 0) {
                    if (stop_requested_.load(std::memory_order_acquire) ||
                        work_.load(std::memory_order_acquire) == 0) {
                        io_uring_cq_advance(&ring_, count);
                        goto done;
                    }
                    continue;
                }

                if (is_link_task(static_cast<uintptr_t>(data))) {
                    auto *info =
                        decode_link_task_info(static_cast<uintptr_t>(data));
                    info->result = cqe->res;
                    meta_.notify_io_completed();
                    auto h = info->handel_;
                    if (h && !h.done()) {
                        h.resume();
                    }
                } else if (is_io_task(static_cast<uintptr_t>(data))) {
                    auto *info =
                        decode_io_task_info(static_cast<uintptr_t>(data));
                    info->result = cqe->res;
                    meta_.notify_io_completed();
                    auto h = info->handel_;
                    if (h && !h.done()) {
                        h.resume();
                    }
                } else {
                    auto h = decode_post_handle(
                        static_cast<uintptr_t>(data));
                    if (h && !h.done()) {
                        h.resume();
                    }
                }
            }

            if (count > 0) {
                io_uring_cq_advance(&ring_, count);
            }
        }

    done:
        detail::tls_ctx = nullptr;
    }

    static Task<void> runner(Task<void> t, io_context *ctx) {
        try {
            co_await t;
        } catch (...) {
        }
        ctx->work_done();
        co_return;
    }

private:
    io_uring ring_;
    worker_meta meta_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<std::size_t> work_{0};
    std::thread worker_;
};

inline io_context *this_io_context() noexcept {
    return detail::tls_ctx;
}
