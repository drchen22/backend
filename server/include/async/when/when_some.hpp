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
#include <vector>

namespace detail {

template <class T>
struct is_task : std::false_type {};

template <class T>
struct is_task<Task<T>> : std::true_type {};
template <class T>
inline constexpr bool is_task_v = is_task<std::remove_cvref_t<T>>::value;

template <class T>
struct task_value;

template <class U>
struct task_value<Task<U>> {
    using type = U;
};
template <class T>
using task_value_t = typename task_value<std::remove_cvref_t<T>>::type;

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

template <class... Tasks>
using when_some_variant_t = std::variant<any_alt_t<task_value_t<Tasks>>...>;

template <class... Tasks>
using when_some_result_t =
    std::vector<std::pair<std::size_t,
                          when_some_variant_t<std::remove_cvref_t<Tasks>...>>>;

template <class... Tasks>
struct when_some_state
    : std::enable_shared_from_this<when_some_state<Tasks...>> {
    using tasks_tuple_t = std::tuple<std::remove_cvref_t<Tasks>...>;
    using variant_t = when_some_variant_t<std::remove_cvref_t<Tasks>...>;
    using index_value_t = std::pair<std::size_t, variant_t>;

    tasks_tuple_t tasks;
    std::coroutine_handle<> continuation{};

    std::size_t min_complete;
    std::atomic<std::size_t> idx{0};

    std::vector<index_value_t> results;

    std::mutex m;
    std::exception_ptr exception;

    explicit when_some_state(std::size_t min, Tasks &&...ts)
        : tasks(std::forward<Tasks>(ts)...)
        , min_complete(min) {
        results.resize(min);
    }

    std::size_t claim_rank() noexcept {
        return idx.fetch_add(1, std::memory_order_acq_rel);
    }

    void store_exception(std::exception_ptr e) {
        std::lock_guard lk(m);
        if (!exception) {
            exception = std::move(e);
        }
    }

    template <std::size_t I, class V>
    void store_result(std::size_t rank, V &&v) {
        results[rank].first = I;
        results[rank].second.template emplace<I>(std::forward<V>(v));
    }
};

template <std::size_t I, class State, class TaskT>
Task<void> when_some_worker(TaskT t, std::shared_ptr<State> st) {
    try {
        using value_t = task_value_t<TaskT>;

        if constexpr (std::is_void_v<value_t>) {
            co_await std::move(t);
            if (st->idx.load(std::memory_order_acquire) >= st->min_complete) {
                co_return;
            }
            auto rank = st->claim_rank();
            if (rank >= st->min_complete) {
                co_return;
            }
            st->template store_result<I>(rank, std::monostate{});
            if (rank + 1 == st->min_complete) {
                st->continuation.resume();
            }
        } else if constexpr (std::is_reference_v<value_t>) {
            auto &r = co_await std::move(t);
            if (st->idx.load(std::memory_order_acquire) >= st->min_complete) {
                co_return;
            }
            auto rank = st->claim_rank();
            if (rank >= st->min_complete) {
                co_return;
            }
            st->template store_result<I>(rank, std::ref(r));
            if (rank + 1 == st->min_complete) {
                st->continuation.resume();
            }
        } else {
            auto r = co_await std::move(t);
            if (st->idx.load(std::memory_order_acquire) >= st->min_complete) {
                co_return;
            }
            auto rank = st->claim_rank();
            if (rank >= st->min_complete) {
                co_return;
            }
            st->template store_result<I>(rank, std::move(r));
            if (rank + 1 == st->min_complete) {
                st->continuation.resume();
            }
        }
    } catch (...) {
        if (st->idx.load(std::memory_order_acquire) >= st->min_complete) {
            co_return;
        }
        auto rank = st->claim_rank();
        if (rank >= st->min_complete) {
            co_return;
        }
        st->store_exception(std::current_exception());
        if (rank + 1 == st->min_complete) {
            st->continuation.resume();
        }
    }
    co_return;
}

template <class State, std::size_t... Is>
Task<void> start_when_some(std::shared_ptr<State> st,
                           std::index_sequence<Is...>) {
    (spawn_detached(
         when_some_worker<Is>(std::move(std::get<Is>(st->tasks)), st)),
     ...);
    co_return;
}

template <class State>
struct when_some_start_awaitable {
    std::shared_ptr<State> st;

    bool await_ready() noexcept {
        return st->min_complete == 0;
    }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> h) noexcept {
        st->continuation = h;
        auto starter = start_when_some(
            st, std::make_index_sequence<
                    std::tuple_size_v<typename State::tasks_tuple_t>>{});
        auto sh = starter.get_handle();
        starter.detach();
        return sh;
    }

    void await_resume() noexcept {}
};

} // namespace detail

/// @brief 等待指定数量的任务完成，返回按完成顺序排列的结果
/// @param min_complete 最少需要完成的任务数量
/// @param tasks 要执行的任务
/// @return 包含前 min_complete 个完成任务的结果向量
template <class... Tasks>
    requires(sizeof...(Tasks) > 0 && (detail::is_task_v<Tasks> && ...))
Task<detail::when_some_result_t<Tasks...>>
some(std::size_t min_complete, Tasks &&...tasks) {
    using state_t = detail::when_some_state<Tasks...>;
    using result_t = detail::when_some_result_t<Tasks...>;

    auto st = std::make_shared<state_t>(min_complete,
                                         std::forward<Tasks>(tasks)...);

    co_await detail::when_some_start_awaitable<state_t>{st};

    if (st->exception) {
        std::rethrow_exception(st->exception);
    }

    co_return result_t(std::move(st->results));
}
