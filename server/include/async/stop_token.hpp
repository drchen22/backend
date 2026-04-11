#pragma once

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

class stop_token;

/// @brief 回调链表节点基类
/// 采用 type-erasure 模式，通过虚函数执行回调
class callback_node {
public:
    virtual ~callback_node() = default;
    virtual void execute() = 0;

    callback_node *next{nullptr};

protected:
    callback_node() = default;
    callback_node(const callback_node &) = delete;
    callback_node &operator=(const callback_node &) = delete;
};

/// @brief 具体回调节点，持有可调用对象
template <class Callable>
class stop_callback_node final : public callback_node {
public:
    explicit stop_callback_node(Callable &&callable)
        : callable_(std::forward<Callable>(callable)) {}

    void execute() override { callable_(); }

private:
    std::remove_cvref_t<Callable> callable_;
};

/// @brief 协作式取消机制中的停止状态共享底层实现
/// 使用原子计数器管理所有权和停止请求，
/// 基于锁的同步处理回调注册和移除。
class stop_state_t final {
public:
    stop_state_t() noexcept = default;

    stop_state_t(const stop_state_t &) = delete;
    stop_state_t &operator=(const stop_state_t &) = delete;

    void add_ref() noexcept {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void release() noexcept {
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

    /// @brief 请求停止
    /// @return true 表示是首次请求停止（并执行了回调），false 表示之前已请求
    bool request_stop() {
        auto expected = false;
        if (!stop_requested_.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            return false;
        }

        execute_callbacks();
        return true;
    }

    /// @brief 查询是否已请求停止
    [[nodiscard]] bool stop_requested() const noexcept {
        return stop_requested_.load(std::memory_order_acquire);
    }

    /// @brief 注册回调节点
    /// @return true 注册成功，false 表示已请求停止（回调节点不会被注册）
    bool register_callback(callback_node *node) {
        std::lock_guard<std::mutex> lock(mtx_);

        if (stop_requested_.load(std::memory_order_acquire)) {
            return false;
        }

        node->next = head_;
        head_ = node;
        return true;
    }

    /// @brief 注销回调节点
    void unregister_callback(callback_node *node) noexcept {
        std::lock_guard<std::mutex> lock(mtx_);

        if (head_ == node) {
            head_ = node->next;
            node->next = nullptr;
            return;
        }

        auto *current = head_;
        while (current) {
            if (current->next == node) {
                current->next = node->next;
                node->next = nullptr;
                return;
            }
            current = current->next;
        }
    }

private:
    void execute_callbacks() {
        std::lock_guard<std::mutex> lock(mtx_);

        auto *current = head_;
        head_ = nullptr;

        while (current) {
            auto *next = current->next;
            current->next = nullptr;
            current->execute();
            current = next;
        }
    }

    std::atomic<std::size_t> ref_count_{1};
    std::atomic<bool> stop_requested_{false};
    std::mutex mtx_;
    callback_node *head_{nullptr};
};

/// @brief 停止源：创建取消信号并拥有共享停止状态
/// 多个 stop_source 对象可以共享相同的底层状态
class stop_source {
public:
    stop_source() : state_(new stop_state_t()) {}

    stop_source(const stop_source &other) noexcept : state_(other.state_) {
        if (state_) {
            state_->add_ref();
        }
    }

    stop_source(stop_source &&other) noexcept : state_(other.state_) {
        other.state_ = nullptr;
    }

    stop_source &operator=(const stop_source &other) noexcept {
        if (this != &other) {
            if (state_) {
                state_->release();
            }
            state_ = other.state_;
            if (state_) {
                state_->add_ref();
            }
        }
        return *this;
    }

    stop_source &operator=(stop_source &&other) noexcept {
        if (this != &other) {
            if (state_) {
                state_->release();
            }
            state_ = other.state_;
            other.state_ = nullptr;
        }
        return *this;
    }

    ~stop_source() {
        if (state_) {
            state_->release();
        }
    }

    [[nodiscard]] stop_token get_token() const noexcept;

    [[nodiscard]] bool request_stop() {
        if (!state_) {
            return false;
        }
        return state_->request_stop();
    }

    [[nodiscard]] bool stop_requested() const noexcept {
        return state_ && state_->stop_requested();
    }

    [[nodiscard]] bool stop_possible() const noexcept {
        return state_ != nullptr;
    }

    void swap(stop_source &other) noexcept {
        std::swap(state_, other.state_);
    }

private:
    friend class stop_token;

    explicit stop_source(stop_state_t *state) noexcept : state_(state) {
        if (state_) {
            state_->add_ref();
        }
    }

    stop_state_t *state_;
};

/// @brief 停止令牌：轻量级观察者，用于查询是否已请求取消
/// 可以被自由拷贝并传递给协程，不会影响取消状态
class stop_token {
public:
    stop_token() noexcept : state_(nullptr) {}

    stop_token(const stop_token &other) noexcept : state_(other.state_) {
        if (state_) {
            state_->add_ref();
        }
    }

    stop_token(stop_token &&other) noexcept : state_(other.state_) {
        other.state_ = nullptr;
    }

    stop_token &operator=(const stop_token &other) noexcept {
        if (this != &other) {
            if (state_) {
                state_->release();
            }
            state_ = other.state_;
            if (state_) {
                state_->add_ref();
            }
        }
        return *this;
    }

    stop_token &operator=(stop_token &&other) noexcept {
        if (this != &other) {
            if (state_) {
                state_->release();
            }
            state_ = other.state_;
            other.state_ = nullptr;
        }
        return *this;
    }

    ~stop_token() {
        if (state_) {
            state_->release();
        }
    }

    [[nodiscard]] bool stop_requested() const noexcept {
        return state_ && state_->stop_requested();
    }

    [[nodiscard]] bool stop_possible() const noexcept {
        return state_ != nullptr;
    }

    void swap(stop_token &other) noexcept {
        std::swap(state_, other.state_);
    }

    bool register_callback(callback_node *node) const {
        if (!state_) {
            return false;
        }
        return state_->register_callback(node);
    }

    void unregister_callback(callback_node *node) const noexcept {
        if (state_) {
            state_->unregister_callback(node);
        }
    }

private:
    friend class stop_source;

    explicit stop_token(stop_state_t *state) noexcept : state_(state) {
        if (state_) {
            state_->add_ref();
        }
    }

    stop_state_t *state_;
};

/// @brief 停止回调：当发出停止请求时同步执行注册的回调函数
/// 提供确定性的清理或通知逻辑
template <class Callable>
class stop_callback {
public:
    static_assert(std::is_invocable_v<Callable>,
                  "stop_callback requires an invocable Callable");

    explicit stop_callback(stop_token token, Callable callable)
        : token_(std::move(token))
        , node_(std::make_unique<stop_callback_node<Callable>>(
              std::forward<Callable>(callable))) {
        if (!token_.register_callback(node_.get())) {
            node_->execute();
        }
    }

    explicit stop_callback(const stop_source &source, Callable callable)
        : stop_callback(source.get_token(), std::forward<Callable>(callable)) {}

    stop_callback(const stop_callback &) = delete;
    stop_callback &operator=(const stop_callback &) = delete;

    ~stop_callback() {
        if (node_) {
            token_.unregister_callback(node_.get());
        }
    }

private:
    stop_token token_;
    std::unique_ptr<stop_callback_node<Callable>> node_;
};

template <class Callable>
stop_callback(stop_token, Callable) -> stop_callback<Callable>;

template <class Callable>
stop_callback(stop_source, Callable) -> stop_callback<Callable>;

inline stop_token stop_source::get_token() const noexcept {
    if (!state_) {
        return stop_token{};
    }
    return stop_token{state_};
}
