#pragma once

#include <async/config/io_context.hpp>
#include <async/details/spsc_cursor.hpp>
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <liburing.h>

class io_context;

using cur_t = std::uint64_t;

class alignas(config::cache_line_size) worker_meta {
public:
    explicit worker_meta(
        std::size_t submit_threshold = config::submission_threshold) noexcept
        : submit_threshold_(submit_threshold) {}

    worker_meta(const worker_meta &) = delete;
    worker_meta &operator=(const worker_meta &) = delete;

    // ================================================================
    // Region 1: Ring 共享区 — io_uring 实例数据
    // ================================================================

    int init(unsigned entries) noexcept {
        return io_uring_queue_init(entries, &ring_, 0);
    }

    void deinit() noexcept { io_uring_queue_exit(&ring_); }

    io_uring *ring() noexcept { return &ring_; }

    // ================================================================
    // Region 2: 只读共享区 — 不可变的上下文元数据
    // ================================================================

    [[nodiscard]] int ring_fd() const noexcept { return ring_.ring_fd; }

    [[nodiscard]] std::size_t ctx_id() const noexcept { return ctx_id_; }

    void set_ctx_id(std::size_t id) noexcept { ctx_id_ = id; }

    [[nodiscard]] std::size_t submit_threshold() const noexcept {
        return submit_threshold_;
    }

    void set_submit_threshold(std::size_t threshold) noexcept {
        submit_threshold_ = threshold;
    }

    // ================================================================
    // Region 3: 可读写共享区 — 跨线程协调
    // ================================================================

    void notify_external() noexcept {
        auto *sqe = io_uring_get_sqe(&ring_);
        if (sqe) {
            io_uring_prep_nop(sqe);
            io_uring_sqe_set_data(sqe, nullptr);
            io_uring_submit(&ring_);
        }
    }

    // ================================================================
    // Region 4: 线程本地区 — 快速路径执行数据
    // ================================================================

    int notify_sqe_ready() noexcept {
        std::size_t pending =
            requests_to_submit_.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (pending >= submit_threshold_) {
            return do_flush();
        }
        return 0;
    }

    void notify_io_inflight() noexcept {
        requests_to_reap_.fetch_add(1, std::memory_order_acq_rel);
    }

    void notify_io_completed() noexcept {
        requests_to_reap_.fetch_sub(1, std::memory_order_acq_rel);
    }

    int flush() noexcept { return do_flush(); }

    int submit_and_wait(unsigned min_complete) noexcept {
        requests_to_submit_.store(0, std::memory_order_release);
        return io_uring_submit_and_wait(&ring_, min_complete);
    }

    [[nodiscard]] std::size_t pending_submits() const noexcept {
        return requests_to_submit_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t inflight_count() const noexcept {
        return requests_to_reap_.load(std::memory_order_acquire);
    }

    void reset() noexcept {
        requests_to_submit_.store(0, std::memory_order_relaxed);
        requests_to_reap_.store(0, std::memory_order_relaxed);
        reap_cur_ = {};
    }

    void forward_task(std::coroutine_handle<> h) noexcept {
        auto idx = reap_cur_.tail() & (config::swap_capacity - 1);
        reap_swap_[idx] = h;
        reap_cur_.push(1);
    }

    [[nodiscard]] bool has_task_ready() const noexcept {
        return !reap_cur_.is_empty();
    }

    [[nodiscard]] std::coroutine_handle<> schedule() noexcept {
        auto idx = reap_cur_.head() & (config::swap_capacity - 1);
        auto h = reap_swap_[idx];
        reap_cur_.pop(1);
        return h;
    }

private:
    int do_flush() noexcept {
        std::size_t count =
            requests_to_submit_.exchange(0, std::memory_order_acq_rel);
        if (count == 0) {
            return 0;
        }
        int ret = io_uring_submit(&ring_);
        if (ret < 0) {
            requests_to_submit_.fetch_add(count, std::memory_order_release);
        }
        return ret;
    }

    // Region 1: Ring 共享区
    io_uring ring_;

    // Region 2: 只读共享区
    alignas(config::cache_line_size) std::size_t ctx_id_{0};
    std::size_t submit_threshold_;

    // Region 4: 线程本地区
    alignas(config::cache_line_size) std::atomic<std::size_t>
        requests_to_submit_{0};
    std::atomic<std::size_t> requests_to_reap_{0};
    spsc_cursor<cur_t, config::swap_capacity, false> reap_cur_;
    std::coroutine_handle<>
        reap_swap_[config::swap_capacity];
};
