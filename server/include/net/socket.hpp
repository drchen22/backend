#pragma once

/// @file socket.hpp
/// @brief socket 封装类，提供基于延迟 IO 的便捷网络操作
///
/// net::socket 类将文件描述符管理与 lazy IO 操作结合，
/// 确保 lazy::recv(), lazy::send() 等延迟执行模型
/// 在整个抽象层中得以维持。

#include <async/lazy_io.hpp>
#include <cstdint>
#include <sys/socket.h>
#include <utility>

namespace net {

class socket {
public:
    socket() noexcept = default;

    explicit socket(int fd) noexcept : fd_(fd) {}

    socket(socket &&other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    socket &operator=(socket &&other) noexcept {
        if (this != &other) [[likely]] {
            if (fd_ >= 0) {
                ::close(fd_);
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    socket(const socket &) = delete;
    socket &operator=(const socket &) = delete;

    ~socket() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    /// @brief 从 socket 接收数据（延迟 IO）
    auto recv(void *buf, std::size_t len, int flags = 0) {
        return lazy::recv(fd_, buf, len, flags);
    }

    /// @brief 向 socket 发送数据（延迟 IO）
    auto send(const void *buf, std::size_t len, int flags = 0) {
        return lazy::send(fd_, buf, len, flags);
    }

    /// @brief 接收 scatter/gather 数据（延迟 IO）
    auto recvmsg(msghdr *msg, unsigned flags = 0) {
        return lazy::recvmsg(fd_, msg, flags);
    }

    /// @brief 发送 scatter/gather 数据（延迟 IO）
    auto sendmsg(const msghdr *msg, unsigned flags = 0) {
        return lazy::sendmsg(fd_, msg, flags);
    }

    /// @brief 关闭 socket 的读/写通道（延迟 IO）
    auto shutdown(int how = SHUT_RDWR) {
        return lazy::shutdown(fd_, how);
    }

    /// @brief 发起非阻塞连接（延迟 IO）
    static auto connect(int fd, const sockaddr *addr, socklen_t addrlen) {
        return lazy::connect(fd, addr, addrlen);
    }

    /// @brief 关闭 socket（延迟 IO），释放文件描述符
    auto close() {
        int fd = std::exchange(fd_, -1);
        return lazy::close(fd);
    }

    /// @brief 获取底层文件描述符
    [[nodiscard]] int fd() const noexcept { return fd_; }

    /// @brief 释放文件描述符的所有权，调用者负责关闭
    [[nodiscard]] int release() noexcept {
        return std::exchange(fd_, -1);
    }

    /// @brief 是否持有有效的文件描述符
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

private:
    int fd_ = -1;
};

} // namespace net
