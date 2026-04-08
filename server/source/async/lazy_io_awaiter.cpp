#include <async/details/lazy_io_awaiter.hpp>
#include <async/io_context.hpp>

lazy_io_awaiter::lazy_io_awaiter() {
    auto *ctx = this_io_context();
    auto *ring = ctx->ring();
    sqe_ = io_uring_get_sqe(ring);
    if (!sqe_) {
        ctx->meta().flush(ring);
        sqe_ = io_uring_get_sqe(ring);
    }
    ctx_ = ctx;
}

void lazy_io_awaiter::await_suspend(std::coroutine_handle<> h) noexcept {
    info_.handel_ = h;
    io_uring_sqe_set_data64(
        sqe_, static_cast<uint64_t>(encode_io_task_info(&info_)));
    ctx_->meta().notify_sqe_ready(ctx_->ring());
    ctx_->meta().notify_io_inflight();
}

lazy_link_awaiter::lazy_link_awaiter(
    lazy_io_awaiter &&first, lazy_io_awaiter &&second) {
    sqes_.reserve(2);
    ctx_ = first.ctx_;
    sqes_.push_back(first.sqe_);
    sqes_.push_back(second.sqe_);
    apply_link_flags();
}

lazy_link_awaiter::lazy_link_awaiter(
    lazy_link_awaiter &&chain, lazy_io_awaiter &&next) {
    ctx_ = chain.ctx_;
    sqes_ = std::move(chain.sqes_);
    sqes_.push_back(next.sqe_);
    apply_link_flags();
}

void lazy_link_awaiter::await_suspend(
    std::coroutine_handle<> h) noexcept {
    auto *ring = ctx_->ring();
    auto chain_size = sqes_.size();

    infos_.resize(chain_size);

    for (std::size_t i = 0; i < chain_size; ++i) {
        bool is_last = (i == chain_size - 1);
        infos_[i].handel_ = is_last ? h : nullptr;
        infos_[i].result = 0;

        io_uring_sqe_set_data64(
            sqes_[i],
            static_cast<uint64_t>(encode_link_task_info(&infos_[i])));
        ctx_->meta().notify_io_inflight();
    }

    ctx_->meta().notify_sqe_ready(ring);
}

void lazy_link_awaiter::apply_link_flags() {
    for (std::size_t i = 0; i + 1 < sqes_.size(); ++i) {
        sqes_[i]->flags |= IOSQE_IO_LINK;
    }
}
