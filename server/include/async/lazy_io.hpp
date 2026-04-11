#pragma once

/// @file lazy_io.hpp
/// @brief 延迟 IO 操作命名空间
///
/// 所有 IO 操作在 co_await 时仅准备 SQE（分配 + 填充），
/// 不立即调用 io_uring_submit 提交给内核。
/// SQE 在 io_context 事件循环的下一次 io_uring_submit_and_wait 中
/// 以批处理方式提交，最大化高并发场景下的吞吐量。
///
/// 用法示例：
///   int n = co_await lazy::recv(fd, buf, len);
///   int fd = co_await lazy::openat(AT_FDCWD, "/tmp/file", O_RDONLY);
///   lazy::submit();  // 显式提交所有累积的 SQE

#include <async/config/io_context.hpp>
#include <async/details/lazy_io_awaiter.hpp>
#include <async/io_context.hpp>
#include <chrono>
#include <cstdint>
#include <liburing.h>
#include <sys/socket.h>

namespace lazy {

// ============================================================
// 网络操作
// ============================================================

struct recv_awaiter : lazy_io_awaiter {
    recv_awaiter(int fd, void *buf, std::size_t len, int flags) {
        io_uring_prep_recv(
            sqe(), fd, buf, static_cast<unsigned>(len), flags);
    }
};

/// @brief 从 socket 接收数据
/// @param fd socket 文件描述符
/// @param buf 接收缓冲区
/// @param len 缓冲区长度
/// @param flags recv 标志位，默认 0
/// @return 接收到的字节数，或负数错误码
inline recv_awaiter
recv(int fd, void *buf, std::size_t len, int flags = 0) {
    return recv_awaiter{fd, buf, len, flags};
}

struct send_awaiter : lazy_io_awaiter {
    send_awaiter(int fd, const void *buf, std::size_t len, int flags) {
        io_uring_prep_send(
            sqe(), fd, buf, static_cast<unsigned>(len), flags);
    }
};

/// @brief 向 socket 发送数据
/// @param fd socket 文件描述符
/// @param buf 发送缓冲区
/// @param len 发送长度
/// @param flags send 标志位，默认 0
/// @return 发送的字节数，或负数错误码
inline send_awaiter
send(int fd, const void *buf, std::size_t len, int flags = 0) {
    return send_awaiter{fd, buf, len, flags};
}

struct recvmsg_awaiter : lazy_io_awaiter {
    recvmsg_awaiter(int fd, msghdr *msg, unsigned flags) {
        io_uring_prep_recvmsg(sqe(), fd, msg, flags);
    }
};

/// @brief 从 socket 接收 scatter/gather 数据
inline recvmsg_awaiter
recvmsg(int fd, msghdr *msg, unsigned flags = 0) {
    return recvmsg_awaiter{fd, msg, flags};
}

struct sendmsg_awaiter : lazy_io_awaiter {
    sendmsg_awaiter(int fd, const msghdr *msg, unsigned flags) {
        io_uring_prep_sendmsg(sqe(), fd, msg, flags);
    }
};

/// @brief 向 socket 发送 scatter/gather 数据
inline sendmsg_awaiter
sendmsg(int fd, const msghdr *msg, unsigned flags = 0) {
    return sendmsg_awaiter{fd, msg, flags};
}

struct accept_awaiter : lazy_io_awaiter {
    accept_awaiter(int fd, sockaddr *addr, socklen_t *addrlen) {
        io_uring_prep_accept(sqe(), fd, addr, addrlen, 0);
    }
};

/// @brief 接受新的 socket 连接
/// @param fd 监听 socket 文件描述符
/// @param addr 客户端地址输出参数，可选
/// @param addrlen 地址长度输出参数，可选
/// @return 新连接的文件描述符，或负数错误码
inline accept_awaiter
accept(int fd, sockaddr *addr = nullptr, socklen_t *addrlen = nullptr) {
    return accept_awaiter{fd, addr, addrlen};
}

struct connect_awaiter : lazy_io_awaiter {
    connect_awaiter(int fd, const sockaddr *addr, socklen_t addrlen) {
        io_uring_prep_connect(sqe(), fd, addr, addrlen);
    }
};

/// @brief 发起非阻塞连接
/// @param fd socket 文件描述符
/// @param addr 目标地址
/// @param addrlen 地址长度
/// @return 0 表示成功，负数错误码
inline connect_awaiter
connect(int fd, const sockaddr *addr, socklen_t addrlen) {
    return connect_awaiter{fd, addr, addrlen};
}

struct shutdown_awaiter : lazy_io_awaiter {
    shutdown_awaiter(int fd, int how) {
        io_uring_prep_shutdown(sqe(), fd, how);
    }
};

/// @brief 关闭 socket 连接的读/写通道
/// @param fd socket 文件描述符
/// @param how SHUT_RD / SHUT_WR / SHUT_RDWR
inline shutdown_awaiter shutdown(int fd, int how) {
    return shutdown_awaiter{fd, how};
}

// ============================================================
// 文件操作
// ============================================================

struct read_awaiter : lazy_io_awaiter {
    read_awaiter(int fd, void *buf, std::size_t len, std::uint64_t offset) {
        io_uring_prep_read(
            sqe(), fd, buf, static_cast<unsigned>(len), offset);
    }
};

/// @brief 从文件读取数据
/// @param fd 文件描述符
/// @param buf 读取缓冲区
/// @param len 读取长度
/// @param offset 文件偏移量，默认 0
/// @return 读取的字节数，或负数错误码
inline read_awaiter
read(int fd, void *buf, std::size_t len, std::uint64_t offset = 0) {
    return read_awaiter{fd, buf, len, offset};
}

struct write_awaiter : lazy_io_awaiter {
    write_awaiter(
        int fd, const void *buf, std::size_t len, std::uint64_t offset) {
        io_uring_prep_write(
            sqe(), fd, buf, static_cast<unsigned>(len), offset);
    }
};

/// @brief 向文件写入数据
/// @param fd 文件描述符
/// @param buf 写入缓冲区
/// @param len 写入长度
/// @param offset 文件偏移量，默认 0
/// @return 写入的字节数，或负数错误码
inline write_awaiter
write(int fd, const void *buf, std::size_t len, std::uint64_t offset = 0) {
    return write_awaiter{fd, buf, len, offset};
}

struct close_awaiter : lazy_io_awaiter {
    explicit close_awaiter(int fd) {
        io_uring_prep_close(sqe(), fd);
    }
};

/// @brief 关闭文件描述符
/// @param fd 要关闭的文件描述符
/// @return 0 成功，负数错误码
inline close_awaiter close(int fd) {
    return close_awaiter{fd};
}

struct openat_awaiter : lazy_io_awaiter {
    openat_awaiter(int dirfd, const char *path, int flags, unsigned mode) {
        io_uring_prep_openat(sqe(), dirfd, path, flags, mode);
    }
};

/// @brief 打开或创建文件
/// @param dirfd 目录文件描述符（AT_FDCWD 表示当前目录）
/// @param path 文件路径
/// @param flags 打开标志（O_RDONLY, O_CREAT 等）
/// @param mode 创建模式，默认 0
/// @return 新文件描述符，或负数错误码
inline openat_awaiter
openat(int dirfd, const char *path, int flags, unsigned mode = 0) {
    return openat_awaiter{dirfd, path, flags, mode};
}

struct fsync_awaiter : lazy_io_awaiter {
    fsync_awaiter(int fd, unsigned flags) {
        io_uring_prep_fsync(sqe(), fd, flags);
    }
};

/// @brief 文件同步（fsync）
/// @param fd 文件描述符
/// @param flags 0 表示 fsync，IORING_FSYNC_DATASYNC 表示 fdatasync
inline fsync_awaiter fsync(int fd, unsigned flags = 0) {
    return fsync_awaiter{fd, flags};
}

// ============================================================
// 超时操作
// ============================================================

struct timeout_awaiter : lazy_io_awaiter {
    __kernel_timespec ts_;

    timeout_awaiter(__kernel_timespec ts, unsigned count, unsigned flags)
        : ts_(ts) {
        io_uring_prep_timeout(sqe(), &ts_, count, flags);
    }
};

/// @brief 注册一个超时请求（原始接口）
/// @param ts 超时时间（支持相对和绝对时间）
/// @param count 等待的完成事件数量，0 表示纯超时
/// @param flags IORING_TIMEOUT_ABS 等标志
/// @return 0 表示超时触发，正数表示在超时前完成的请求数
inline timeout_awaiter
timeout(__kernel_timespec ts, unsigned count = 0, unsigned flags = 0) {
    return timeout_awaiter{ts, count, flags};
}

/// @brief 将 chrono 时长转换为 __kernel_timespec
template <class Rep, class Period>
__kernel_timespec to_kernel_timespec(
    std::chrono::duration<Rep, Period> dur) {
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(dur);
    auto nsecs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(dur - secs);
    return {secs.count(), static_cast<long long>(nsecs.count())};
}

/// @brief 将 chrono time_point 转换为 __kernel_timespec（从 epoch 起）
template <class Clock, class Duration>
__kernel_timespec to_kernel_timespec(
    std::chrono::time_point<Clock, Duration> tp) {
    auto dur = tp.time_since_epoch();
    return to_kernel_timespec(dur);
}

/// @brief 将 chrono 时长转换为带偏移的 __kernel_timespec
/// 减去 timeout_bias_nanosecond 以补偿 SQE 提交延迟
template <class Rep, class Period>
__kernel_timespec to_kernel_timespec_biased(
    std::chrono::duration<Rep, Period> dur) {
    auto biased = dur - std::chrono::nanoseconds(config::timeout_bias_nanosecond);
    if (biased < std::chrono::duration<Rep, Period>::zero()) {
        biased = std::chrono::duration<Rep, Period>::zero();
    }
    return to_kernel_timespec(biased);
}

/// @brief 注册相对时长超时（使用 BOOTTIME 时钟）
/// @param dur 超时时长
/// @param count 等待的完成事件数量，0 表示纯超时
/// @return 0 表示超时触发，正数表示在超时前完成的请求数
template <class Rep, class Period>
timeout_awaiter timeout(std::chrono::duration<Rep, Period> dur,
                         unsigned count = 0) {
    return timeout_awaiter{
        to_kernel_timespec_biased(dur), count, IORING_TIMEOUT_BOOTTIME};
}

/// @brief 注册绝对时间点超时
/// steady_clock 使用默认时钟（MONOTONIC），system_clock 使用 REALTIME
/// @param tp 超时绝对时间点
/// @param count 等待的完成事件数量，0 表示纯超时
/// @return 0 表示超时触发，正数表示在超时前完成的请求数
template <class Clock, class Duration>
timeout_awaiter timeout(std::chrono::time_point<Clock, Duration> tp,
                         unsigned count = 0) {
    unsigned flags = IORING_TIMEOUT_ABS;
    if constexpr (std::is_same_v<Clock, std::chrono::system_clock>) {
        flags |= IORING_TIMEOUT_REALTIME;
    }
    return timeout_awaiter{to_kernel_timespec(tp), count, flags};
}

/// @brief 注册绝对时间点超时（显式接口）
/// @param tp 超时绝对时间点
/// @param count 等待的完成事件数量，0 表示纯超时
/// @return 0 表示超时触发，正数表示在超时前完成的请求数
template <class Clock, class Duration>
timeout_awaiter timeout_at(std::chrono::time_point<Clock, Duration> tp,
                             unsigned count = 0) {
    unsigned flags = IORING_TIMEOUT_ABS;
    if constexpr (std::is_same_v<Clock, std::chrono::system_clock>) {
        flags |= IORING_TIMEOUT_REALTIME;
    }
    return timeout_awaiter{to_kernel_timespec(tp), count, flags};
}

/// @brief 将 IO 操作与相对超时链接
/// IO 操作先执行，超时作为链接操作跟随：
/// - IO 在超时前完成 → 返回 IO 结果
/// - 超时先触发 → IO 被取消，返回 -ECANCELED
/// @param io IO 操作 awaiter
/// @param dur 超时时长
/// @return IO 结果，或 -ECANCELED
template <class Rep, class Period>
lazy_link_timeout timeout(lazy_io_awaiter &&io,
                           std::chrono::duration<Rep, Period> dur) {
    return lazy_link_timeout{
        std::move(io), to_kernel_timespec_biased(dur),
        IORING_TIMEOUT_BOOTTIME};
}

/// @brief 将 IO 操作与绝对超时链接
/// @param io IO 操作 awaiter
/// @param tp 超时绝对时间点
/// @return IO 结果，或 -ECANCELED
template <class Clock, class Duration>
lazy_link_timeout timeout_at(lazy_io_awaiter &&io,
                              std::chrono::time_point<Clock, Duration> tp) {
    unsigned flags = IORING_TIMEOUT_ABS;
    if constexpr (std::is_same_v<Clock, std::chrono::system_clock>) {
        flags |= IORING_TIMEOUT_REALTIME;
    }
    return lazy_link_timeout{std::move(io), to_kernel_timespec(tp), flags};
}

struct timeout_remove_awaiter : lazy_io_awaiter {
    timeout_remove_awaiter(std::uint64_t user_data, unsigned flags) {
        io_uring_prep_timeout_remove(sqe(), user_data, flags);
    }
};

/// @brief 取消一个已注册的超时请求
/// @param user_data 要取消的超时 SQE 的 user_data
/// @param flags 标志位
inline timeout_remove_awaiter
timeout_remove(std::uint64_t user_data, unsigned flags = 0) {
    return timeout_remove_awaiter{user_data, flags};
}

struct timeout_update_awaiter : lazy_io_awaiter {
    __kernel_timespec ts_;

    timeout_update_awaiter(__kernel_timespec ts, std::uint64_t user_data,
                            unsigned flags)
        : ts_(ts) {
        io_uring_prep_timeout_update(sqe(), &ts_, user_data, flags);
    }
};

/// @brief 更新一个已注册的超时请求的过期时间
/// @param dur 新的超时时长
/// @param user_data 要更新的超时 SQE 的 user_data
/// @param flags 标志位
inline timeout_update_awaiter
timeout_update(std::chrono::nanoseconds dur, std::uint64_t user_data,
               unsigned flags = 0) {
    return timeout_update_awaiter{
        to_kernel_timespec_biased(dur), user_data, flags};
}

// ============================================================
// Poll 操作
// ============================================================

struct poll_add_awaiter : lazy_io_awaiter {
    poll_add_awaiter(int fd, unsigned mask) {
        io_uring_prep_poll_add(sqe(), fd, mask);
    }
};

/// @brief 注册一个 poll 请求
/// @param fd 文件描述符
/// @param mask 事件掩码（POLLIN, POLLOUT 等）
/// @return 就绪的事件掩码，或负数错误码
inline poll_add_awaiter poll_add(int fd, unsigned mask) {
    return poll_add_awaiter{fd, mask};
}

// ============================================================
// 取消操作
// ============================================================

struct cancel_awaiter : lazy_io_awaiter {
    cancel_awaiter(std::uint64_t user_data, int flags) {
        io_uring_prep_cancel64(sqe(), user_data, flags);
    }
};

/// @brief 取消一个正在进行的 IO 请求
/// @param user_data 要取消的请求的 user_data
/// @param flags 标志位
inline cancel_awaiter cancel(std::uint64_t user_data, int flags = 0) {
    return cancel_awaiter{user_data, flags};
}

// ============================================================
// 辅助操作
// ============================================================

struct nop_awaiter : lazy_io_awaiter {
    nop_awaiter() {
        io_uring_prep_nop(sqe());
    }
};

/// @brief 提交一个 nop 操作（可用于测试或唤醒）
inline nop_awaiter nop() {
    return nop_awaiter{};
}

/// @brief 显式提交所有累积的 SQE 到内核
/// 通过 worker_meta 的批处理机制提交，将所有累积的 SQE 一次性
/// 通过 io_uring_submit() 提交给内核。
/// 通常不需要手动调用，io_context 事件循环会自动批量提交。
/// 在需要立即提交的场景（如低延迟要求）下可使用。
inline int submit() {
    auto *ctx = this_io_context();
    return ctx->meta().flush();
}

} // namespace lazy
