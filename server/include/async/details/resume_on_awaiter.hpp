#pragma once

#include <async/io_context.hpp>
#include <coroutine>

namespace detail {

class lazy_resume_on {
public:
    static constexpr bool await_ready() noexcept { return false; }

    bool await_suspend(std::coroutine_handle<> current) const noexcept {
        if (resume_ctx_ != detail::this_thread.ctx) [[likely]] {
            resume_ctx_->co_spawn_auto(current);
            return true;
        }
        return false;
    }

    constexpr void await_resume() const noexcept {}

    explicit lazy_resume_on(io_context &resume_context) noexcept
        : resume_ctx_(&resume_context) {}

private:
    io_context *resume_ctx_;
};

} // namespace detail
