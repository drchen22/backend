#pragma once
// when_all 实现：同时等待多个 Task 完成，返回所有结果的 tuple
// 如果任意 Task 抛出异常，第一个异常会被重新抛出

#include "async/io_context.hpp"
#include <async/task.hpp>
#include <atomic>
#include <coroutine>
#include <functional>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

namespace detail {

// 类型特征：判断类型 T 是否为 Task 类型

template <class T>
struct is_task : std::false_type {};

template <class T>
struct is_task<Task<T>> : std::true_type {};
template <class T>
inline constexpr bool is_task_v = is_task<std::remove_cvref_t<T>>::value;

// 提取 Task 的值类型
template <class T>
struct task_value;

template <class U>
struct task_value<Task<U>> {
    using type = U;
};
template <class T>
using task_value_t = typename task_value<std::remove_cvref_t<T>>::type;

// 规范化 when_all 返回的类型：void 保持 void，引用保持引用，值类型移除 cvref
template <class V>
struct all_norm {
    using type = std::conditional_t<
        std::is_void_v<V>, void,
        std::conditional_t<std::is_reference_v<V>, V, std::remove_cvref_t<V>>>;
};
template <class V>
using all_norm_t = typename all_norm<V>::type;

// 拼接多个 tuple 类型为一个 tuple 类型
template <class... Tuples>
struct tuple_cat_type;

template <>
struct tuple_cat_type<> {
    using type = std::tuple<>;
};

template <class... Ts>
struct tuple_cat_type<std::tuple<Ts...>> {
    using type = std::tuple<Ts...>;
};

template <class... Ts, class... Us, class... Rest>
struct tuple_cat_type<std::tuple<Ts...>, std::tuple<Us...>, Rest...> {
    using type =
        typename tuple_cat_type<std::tuple<Ts..., Us...>, Rest...>::type;
};
template <class... Tuples>
using tuple_cat_type_t = typename tuple_cat_type<Tuples...>::type;

// 将单个 Task 的值转换为 tuple，void 任务返回空 tuple
template <class TaskT>
using maybe_tuple_for_all_t =
    std::conditional_t<std::is_void_v<task_value_t<TaskT>>, std::tuple<>,
                       std::tuple<all_norm_t<task_value_t<TaskT>>>>;

// when_all 的最终返回类型：所有 Task 的值拼接成一个 tuple（void 任务被跳过）
template <class... Tasks>
using when_all_result_t = tuple_cat_type_t<maybe_tuple_for_all_t<Tasks>...>;

// 结果存储容器：根据类型不同使用不同的存储方式
template <class V, class Enable = void>
struct all_holder;

// void 类型的特化：无需存储
template <class V>
struct all_holder<V, std::enable_if_t<std::is_void_v<V>>> {};

// 引用类型的特化：存储指针
template <class V>
struct all_holder<
    V, std::enable_if_t<!std::is_void_v<V> && std::is_reference_v<V>>> {
    std::remove_reference_t<V> *ptr{nullptr};
};

// 值类型的特化：使用 optional 存储
template <class V>
struct all_holder<
    V, std::enable_if_t<!std::is_void_v<V> && !std::is_reference_v<V>>> {
    std::optional<std::remove_cvref_t<V>> value;
};

// 启动分离的任务：如果当前有 io_context 则使用其 spawn 方法，否则直接恢复协程
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

// when_all 的状态管理结构
template <class... Tasks>
struct when_all_state {
    using tasks_tuple_t = std::tuple<std::remove_cvref_t<Tasks>...>;
    tasks_tuple_t tasks; // 所有要等待的 Task
    std::tuple<all_holder<task_value_t<std::remove_cvref_t<Tasks>>>...>
        results;         // 各 Task 的结果存储

    std::atomic<std::size_t> remaining{
        sizeof...(Tasks)};                  // 剩余未完成的 Task 数量
    std::coroutine_handle<> continuation{}; // 所有任务完成后的恢复句柄

    std::mutex ex_mtx;                      // 异常互斥锁
    std::exception_ptr exception;           // 第一个异常指针

    explicit when_all_state(Tasks &&...ts)
        : tasks(std::forward<Tasks>(ts)...) {}

    // 存储异常（仅存储第一个异常）
    void store_exception(std::exception_ptr e) {
        std::lock_guard lk(ex_mtx);
        if (!exception) {
            exception = e;
        }
    }

    // 存储第 I 个 Task 的结果值
    template <std::size_t I, class V>
    void store_value(V &&v) {
        using value_t = task_value_t<std::tuple_element_t<I, tasks_tuple_t>>;
        if constexpr (std::is_void_v<value_t>) {
            (void)v;
        } else if constexpr (std::is_reference_v<value_t>) {
            std::get<I>(results).ptr = std::addressof(v);
        } else {
            std::get<I>(results).value.emplace(std::forward<V>(v));
        }
    }

    // 构造第 I 个 Task 的结果 tuple
    template <std::size_t I>
    auto make_piece() {
        using value_t = task_value_t<std::tuple_element_t<I, tasks_tuple_t>>;
        if constexpr (std::is_void_v<value_t>) {
            return std::tuple<>{};
        } else if constexpr (std::is_reference_v<value_t>) {
            return std::tuple<value_t>{*std::get<I>(results).ptr};
        } else {
            using out_t = all_norm_t<value_t>;
            return std::tuple<out_t>{std::move(*std::get<I>(results).value)};
        }
    }

    // 构造最终的结果 tuple
    auto make_result_tuple() {
        return make_result_tuple_impl(
            std::make_index_sequence<sizeof...(Tasks)>{});
    }

private:
    template <std::size_t... Is>
    auto make_result_tuple_impl(std::index_sequence<Is...>) {
        return std::tuple_cat(make_piece<Is>()...);
    }
};


template <std::size_t I, class State, class TaskT>
Task<void> when_all_worker(TaskT t, State *st) {
    try {
        using value_t = task_value_t<TaskT>;
        if constexpr (std::is_void_v<value_t>) {
            co_await std::move(t);
        } else if constexpr (std::is_reference_v<value_t>) {
            auto &r = co_await std::move(t);
            st->template store_value<I>(r);
        } else {
            auto r = co_await std::move(t);
            st->template store_value<I>(std::move(r));
        }
    } catch (...) {
        st->store_exception(std::current_exception());
    }

    // 如果是最后一个完成的任务，恢复主协程
    if (st->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        st->continuation.resume();
    }
    co_return;
}

// 启动所有 when_all 的 worker 协程
template <class State, std::size_t... Is>
Task<void> start_when_all(State *st, std::index_sequence<Is...>) {
    (spawn_detached(
         when_all_worker<Is>(std::move(std::get<Is>(st->tasks)), st)),
     ...);
    co_return;
}

// when_all 启动的 awaitable：负责设置 continuation 并启动所有 worker
template <class State>
struct when_all_start_awaitable {
    State &st;

    bool await_ready() noexcept {
        return st.remaining.load(std::memory_order_acquire) == 0;
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
        st.continuation = h;
        auto starter = start_when_all(
            &st, std::make_index_sequence<
                     std::tuple_size_v<typename State::tasks_tuple_t>>{});
        auto sh = starter.get_handle();
        starter.detach();
        return sh;
    }

    void await_resume() noexcept {}
};

} // namespace detail

// when_all 主函数：等待所有 Task 完成，返回所有结果（异常会被重新抛出）
template <class... Tasks>
    requires(detail::is_task_v<Tasks> && ...)
Task<detail::when_all_result_t<Tasks...>> all(Tasks &&...tasks) {
    detail::when_all_state<Tasks...> st{std::forward<Tasks>(tasks)...};

    co_await detail::when_all_start_awaitable<detail::when_all_state<Tasks...>>{
        st};

    if (st.exception) {
        std::rethrow_exception(st.exception);
    }

    co_return st.make_result_tuple();
}
