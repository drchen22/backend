#pragma once
#include "async/io_context.hpp"
#include <async/task.hpp>
#include <atomic>
#include <coroutine>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace detail {


// 默认继承std::false_type，如果为Task<T>则为true_type
template <class T>
struct is_task : std::false_type {};

template <class T>
struct is_task<Task<T>> : std::true_type {};
template <class T>
inline constexpr bool is_task_v = is_task<std::remove_cvref_t<T>>::value;


//获取Task<T>中的T
template <class T>
struct task_value;

template <class U>
struct task_value<Task<U>> {
    using type = U;
};
template <class T>
using task_value_t = typename task_value<std::remove_cvref_t<T>>::type;

// 将任务值类型转换为适合在 std::variant 中存储的类型
// 对于 void 类型使用 std::monostate，对于引用类型使用 std::reference_wrapper
template <class V>
struct any_alt {
    using type = std::conditional_t<
        std::is_void_v<V>, std::monostate,
        std::conditional_t<std::is_reference_v<V>,
                           std::reference_wrapper<std::remove_reference_t<V>>,
                           std::remove_cvref_t<V>>>;
};
template <class V>
using any_alt_t = typename any_alt<V>::type;

// 启动一个分离的任务（不等待其完成）
inline void spawn_detached(Task<void> t) {
    if (auto *ctx = this_io_context()) {
        ctx->co_spawn(std::move(t));
    } else {
        auto h = t.get_handle();
        t.detach();
        if (h) {
            h.resume();
        }
    }
}

// 用于存储任意一个任务完成结果的 variant 类型
template <class... Tasks>
using when_any_variant_t = std::variant<any_alt_t<task_value_t<Tasks>>...>;

// when_any 的共享状态结构体，用于在多个 worker 协程之间共享数据
template <class... Tasks>
struct when_any_state : std::enable_shared_from_this<when_any_state<Tasks...>> {
    using tasks_tuple_t = std::tuple<std::remove_cvref_t<Tasks>...>;
    using variant_t = when_any_variant_t<std::remove_cvref_t<Tasks>...>;

    tasks_tuple_t tasks;                    // 存储所有输入任务
    std::coroutine_handle<> continuation{}; // 当有任务完成时需要恢复的协程句柄

    std::atomic<bool> done{false};          // 原子标志，表示是否有任务已完成
    std::size_t index{0};                   // 第一个完成的任务的索引

    std::mutex m;                           // 保护异常和结果的互斥锁
    std::exception_ptr exception;           // 第一个完成的任务抛出的异常
    std::optional<variant_t> result;        // 第一个完成的任务的结果

    explicit when_any_state(Tasks &&...ts)
        : tasks(std::forward<Tasks>(ts)...) {}

    // 尝试成为第一个完成的任务（线程安全）
    // 只有第一个调用的任务会返回 true，其他任务返回 false
    bool try_win(std::size_t i) noexcept {
        bool expected = false;
        if (done.compare_exchange_strong(expected, true,
                                         std::memory_order_acq_rel)) {
            index = i;
            return true;
        }
        return false;
    }

    // 设置异常信息（需要持有锁）
    void set_exception(std::exception_ptr e) {
        std::lock_guard lk(m);
        exception = e;
    }

    // 设置任务结果（将结果存入 variant 的指定索引位置）
    template <std::size_t I, class V>
    void set_result(V &&v) {
        result.emplace(std::in_place_index<I>, std::forward<V>(v));
    }
};

// worker 协程：等待一个任务完成，并尝试更新共享状态
// I: 任务索引, TaskT: 任务类型, st: 共享状态的智能指针
template <std::size_t I, class State, class TaskT>
Task<void> when_any_worker(TaskT t, std::shared_ptr<State> st) {
    try {
        using value_t = task_value_t<TaskT>;
        if constexpr (std::is_void_v<value_t>) {
            // 处理 void 返回类型的任务
            co_await std::move(t);
            if (st->try_win(I)) {          // 尝试成为第一个完成的任务
                st->template set_result<I>(std::monostate{});
                st->continuation.resume(); // 恢复调用者协程
            }
        } else if constexpr (std::is_reference_v<value_t>) {
            // 处理引用返回类型的任务
            auto &r = co_await std::move(t);
            if (st->try_win(I)) {
                st->template set_result<I>(std::ref(r));
                st->continuation.resume();
            }
        } else {
            // 处理值返回类型的任务
            auto r = co_await std::move(t);
            if (st->try_win(I)) {
                st->template set_result<I>(std::move(r));
                st->continuation.resume();
            }
        }
    } catch (...) {
        // 任务抛出异常
        if (st->try_win(I)) {
            st->set_exception(std::current_exception());
            st->continuation.resume();
        }
    }
    co_return;
}

// 启动所有 worker 协程
// 使用折叠表达式同时启动所有任务
template <class State, std::size_t... Is>
Task<void> start_when_any(std::shared_ptr<State> st,
                          std::index_sequence<Is...>) {
    (spawn_detached(
         when_any_worker<Is>(std::move(std::get<Is>(st->tasks)), st)),
     ...);
    co_return;
}

// awaitable 类型，用于在协程中启动 when_any 的所有 worker
template <class State>
struct when_any_start_awaitable {
    std::shared_ptr<State> st;

    bool await_ready() noexcept {
        return false;
    } // 总是需要挂起

    // 保存调用者的协程句柄，启动所有 worker，然后返回 starter 的句柄
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
        st->continuation = h;
        auto starter = start_when_any(
            st, std::make_index_sequence<
                    std::tuple_size_v<typename State::tasks_tuple_t>>{});
        auto sh = starter.get_handle();
        starter.detach();
        return sh;
    }

    void await_resume() noexcept {} // 不返回任何值
};

} // namespace detail

// when_any 的主函数
// 等待多个任务中的任意一个完成，返回 (索引, 结果) 对
// 要求：至少一个任务，且所有参数都是 Task 类型
template <class... Tasks>
    requires(sizeof...(Tasks) > 0 && (detail::is_task_v<Tasks> && ...))
Task<std::pair<std::size_t,
               detail::when_any_variant_t<std::remove_cvref_t<Tasks>...>>>
any(Tasks &&...tasks) {
    using state_t = detail::when_any_state<Tasks...>;
    using variant_t = detail::when_any_variant_t<std::remove_cvref_t<Tasks>...>;

    // 创建共享状态
    auto st = std::make_shared<state_t>(std::forward<Tasks>(tasks)...);

    // 等待所有 worker 启动并等待任意一个任务完成
    co_await detail::when_any_start_awaitable<state_t>{st};

    // 如果有任务抛出异常，重新抛出
    if (st->exception) {
        std::rethrow_exception(st->exception);
    }

    // 返回第一个完成的任务的索引和结果
    co_return std::pair<std::size_t, variant_t>{st->index,
                                                std::move(*st->result)};
}
