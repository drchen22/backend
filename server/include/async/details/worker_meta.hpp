#pragma once

#include <atomic>
#include <cstdint>
#include <liburing.h>

/// @brief worker 提交批处理元数据
/// 跟踪两个关键计数器，延迟执行的力量来自于将多个 IO 操作
/// 批处理到单个系统调用中：
///   - requests_to_submit: 准备好提交的待处理 SQE 数量
///   - requests_to_reap:   等待完成的进行中 IO 操作数量
///
/// 当达到阈值时，通过 io_uring_enter() 将所有累积的 SQE
/// 提交给内核，显著减少每个操作的系统调用开销。
class worker_meta {
public:
    /// @brief 构造函数
    /// @param submit_threshold 触发自动提交的 SQE 累积阈值
    explicit worker_meta(
        unsigned submit_threshold = default_submit_threshold) noexcept
        : submit_threshold_(submit_threshold) {}

    worker_meta(const worker_meta &) = delete;
    worker_meta &operator=(const worker_meta &) = delete;

    /// @brief 通知一个新的 SQE 已准备好提交
    /// 内部递增 requests_to_submit，当达到阈值时自动调用 flush
    /// @param ring io_uring 实例指针
    /// @return 本次 flush 提交的 SQE 数量，未触发则返回 0
    int notify_sqe_ready(io_uring *ring) noexcept {
        std::size_t pending =
            requests_to_submit_.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (pending >= submit_threshold_) {
            return do_flush(ring);
        }
        return 0;
    }

    /// @brief 通知一个 IO 操作已入队（提交到内核）
    void notify_io_inflight() noexcept {
        requests_to_reap_.fetch_add(1, std::memory_order_acq_rel);
    }

    /// @brief 通知一个 IO 操作已完成（从 CQE 收割）
    void notify_io_completed() noexcept {
        requests_to_reap_.fetch_sub(1, std::memory_order_acq_rel);
    }

    /// @brief 手动提交所有累积的 SQE
    /// @param ring io_uring 实例指针
    /// @return 提交的 SQE 数量，出错返回负数
    int flush(io_uring *ring) noexcept {
        return do_flush(ring);
    }

    /// @brief 提交并等待至少 min_complete 个完成事件
    /// @param ring io_uring 实例指针
    /// @param min_complete 最小等待完成数
    /// @return 提交的 SQE 数量（>=0），或负数错误码
    int submit_and_wait(io_uring *ring, unsigned min_complete) noexcept {
        requests_to_submit_.store(0, std::memory_order_release);
        int ret = io_uring_submit_and_wait(ring, min_complete);
        return ret;
    }

    /// @brief 获取当前待提交的 SQE 数量
    [[nodiscard]] std::size_t pending_submits() const noexcept {
        return requests_to_submit_.load(std::memory_order_acquire);
    }

    /// @brief 获取当前进行中的 IO 操作数量
    [[nodiscard]] std::size_t inflight_count() const noexcept {
        return requests_to_reap_.load(std::memory_order_acquire);
    }

    /// @brief 重置所有计数器（用于 worker 启动或重置场景）
    void reset() noexcept {
        requests_to_submit_.store(0, std::memory_order_relaxed);
        requests_to_reap_.store(0, std::memory_order_relaxed);
    }

    /// @brief 设置提交阈值
    void set_submit_threshold(unsigned threshold) noexcept {
        submit_threshold_ = threshold;
    }

    /// @brief 获取当前提交阈值
    [[nodiscard]] unsigned submit_threshold() const noexcept {
        return submit_threshold_;
    }

private:
    int do_flush(io_uring *ring) noexcept {
        std::size_t count =
            requests_to_submit_.exchange(0, std::memory_order_acq_rel);
        if (count == 0) {
            return 0;
        }
        int ret = io_uring_submit(ring);
        if (ret < 0) {
            requests_to_submit_.fetch_add(
                count, std::memory_order_release);
        }
        return ret;
    }

    static constexpr unsigned default_submit_threshold = 1;

    std::atomic<std::size_t> requests_to_submit_{0};
    std::atomic<std::size_t> requests_to_reap_{0};
    unsigned submit_threshold_;
};
