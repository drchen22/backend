#pragma once

#include <async/config/io_context.hpp>
#include <async/details/io_context_meta.hpp>
#include <async/details/task_info.hpp>
#include <async/details/thread_meta.hpp>
#include <async/details/worker_meta.hpp>
#include <async/task.hpp>
#include <cassert>
#include <coroutine>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <print>
#include <queue>
#include <thread>
#include <utility>

class io_context;

inline io_context *this_io_context() noexcept;

enum class safety { safe, unsafe };

class io_context {
public:
    explicit io_context(
        unsigned entries = static_cast<unsigned>(config::io_uring_entries),
        std::size_t submit_threshold = config::submission_threshold)
        : meta_(submit_threshold) {
        std::scoped_lock lock(detail::global_context_meta.mtx);
        id_ = detail::global_context_meta.create_count.fetch_add(
            1, std::memory_order_relaxed);
    }

    ~io_context() {
        stop();
        join();
    }

    io_context(const io_context &) = delete;
    io_context &operator=(const io_context &) = delete;

    void start() {
        worker_ = std::thread([this] {
            detail::this_thread.ctx = this;
            detail::this_thread.worker = &meta_;
            detail::this_thread.ctx_id = id_;

            init();
            ring_ready_.store(true, std::memory_order_release);
            drain_spawn_queue();
            run();
            ring_ready_.store(false, std::memory_order_release);
            deinit();

            detail::this_thread.ctx = nullptr;
            detail::this_thread.worker = nullptr;
        });
    }

    void join() {
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void stop() {
        will_stop_.store(true, std::memory_order_release);
        if (ring_ready_.load(std::memory_order_acquire)) {
            meta_.notify_external();
        }
    }

    template <safety S = safety::safe>
    void co_spawn(Task<void> t) {
        if constexpr (S == safety::unsafe) {
            co_spawn_unsafe(std::move(t));
        } else {
            co_spawn_safe(std::move(t));
        }
    }

    struct yield_awaitable {
        io_context &ctx;
        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) noexcept {
            ctx.meta_.forward_task(h);
        }
        void await_resume() noexcept {}
    };

    yield_awaitable yield() noexcept { return yield_awaitable{*this}; }

    io_uring *ring() noexcept { return meta_.ring(); }

    worker_meta &meta() noexcept { return meta_; }

    [[nodiscard]] std::size_t id() const noexcept { return id_; }

private:
    friend io_context *this_io_context() noexcept;

    void init() {
        unsigned entries =
            static_cast<unsigned>(config::io_uring_entries);
        int ret = meta_.init(entries);
        if (ret < 0) {
            fprintf(stderr, "FATAL: io_uring_queue_init(%u) failed: %s (ret=%d)\n",
                    entries, strerror(-ret), ret);
            std::abort();
        }
        meta_.set_ctx_id(id_);
        meta_.reset();
    }

    void deinit() { meta_.deinit(); }

    void co_spawn_safe(Task<void> t) {
        work_.fetch_add(1, std::memory_order_relaxed);
        auto wrapper = runner(std::move(t), this);
        auto h = wrapper.get_handle();
        wrapper.detach();
        {
            std::scoped_lock lock(spawn_mtx_);
            spawn_queue_.push(h);
        }
        if (ring_ready_.load(std::memory_order_acquire)) {
            meta_.notify_external();
        }
    }

    void co_spawn_unsafe(Task<void> t) {
        work_.fetch_add(1, std::memory_order_relaxed);
        auto wrapper = runner(std::move(t), this);
        auto h = wrapper.get_handle();
        wrapper.detach();
        meta_.forward_task(h);
    }

    void drain_spawn_queue() noexcept {
        std::scoped_lock lock(spawn_mtx_);
        while (!spawn_queue_.empty()) {
            meta_.forward_task(spawn_queue_.front());
            spawn_queue_.pop();
        }
    }

    bool can_stop() const noexcept {
        return will_stop_.load(std::memory_order_acquire)
               && work_.load(std::memory_order_acquire) == 0;
    }

    void work_done() {
        if (work_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            meta_.notify_external();
        }
    }

    void run() {
        while (!can_stop()) {
            do_submission_part();
            do_completion_part();
            do_worker_part();
        }
    }

    void do_submission_part() {
        if (meta_.pending_submits() > 0) {
            meta_.flush();
        }
    }

    void do_completion_part() {
        unsigned head;
        struct io_uring_cqe *cqe;
        unsigned count = 0;

        auto *r = meta_.ring();
        io_uring_for_each_cqe(r, head, cqe) {
            ++count;
            handle_cq_entry(cqe);
        }

        if (count > 0) {
            io_uring_cq_advance(meta_.ring(), count);
        }
    }

    void handle_cq_entry(struct io_uring_cqe *cqe) {
        uint64_t data = static_cast<uint64_t>(
            reinterpret_cast<uintptr_t>(io_uring_cqe_get_data(cqe)));

        if (data == 0) {
            return;
        }

        if (is_link_task(static_cast<uintptr_t>(data))) {
            auto *info =
                decode_link_task_info(static_cast<uintptr_t>(data));
            info->result = cqe->res;
            meta_.notify_io_completed();
            auto h = info->handel_;
            if (h) {
                meta_.forward_task(h);
            }
        } else if (is_io_task(static_cast<uintptr_t>(data))) {
            auto *info =
                decode_io_task_info(static_cast<uintptr_t>(data));
            info->result = cqe->res;
            meta_.notify_io_completed();
            meta_.forward_task(info->handel_);
        } else {
            auto h = decode_post_handle(static_cast<uintptr_t>(data));
            if (h) {
                meta_.forward_task(h);
            }
        }
    }

    void do_worker_part() {
        std::size_t budget = config::swap_capacity;
        while (meta_.has_task_ready() && budget-- > 0) {
            auto h = meta_.schedule();
            if (h && !h.done()) {
                h.resume();
            }
        }

        if (!meta_.has_task_ready()) {
            drain_spawn_queue();
            if (!meta_.has_task_ready()) {
                meta_.submit_and_wait(1);
            }
        }
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
    worker_meta meta_;
    std::size_t id_{0};
    alignas(config::cache_line_size) std::atomic<bool> will_stop_{false};
    alignas(config::cache_line_size) std::atomic<bool> ring_ready_{false};
    alignas(config::cache_line_size) std::atomic<std::size_t> work_{0};
    std::thread worker_;
    std::mutex spawn_mtx_;
    std::queue<std::coroutine_handle<>> spawn_queue_;
};

inline io_context *this_io_context() noexcept {
    return detail::this_thread.ctx;
}

inline void co_spawn(Task<void> t) {
    assert(detail::this_thread.ctx != nullptr);
    detail::this_thread.ctx->co_spawn<safety::unsafe>(std::move(t));
}
