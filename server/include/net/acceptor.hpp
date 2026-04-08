#pragma once

/// @file acceptor.hpp
/// @brief TCP 接受器，封装 socket 创建、绑定、监听和接受连接的完整流程

#include <async/lazy_io.hpp>
#include <net/inet_address.hpp>
#include <net/socket.hpp>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace net {

class acceptor {
public:
    /// @brief 从 inet_address 创建接受器
    /// 构造时完成 socket 创建、SO_REUSEADDR 设置、绑定和监听
    explicit acceptor(const inet_address &addr) {
        int fd = socket::create_tcp(addr.family());
        if (fd < 0) {
            throw std::runtime_error("socket() failed");
        }

        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

        if (bind(fd, addr.sockaddr_ptr(), addr.sockaddr_len()) < 0) {
            ::close(fd);
            throw std::runtime_error("bind() failed");
        }

        if (listen(fd, 128) < 0) {
            ::close(fd);
            throw std::runtime_error("listen() failed");
        }

        listen_fd_ = fd;
        addr_ = addr;
    }

    acceptor(acceptor &&other) noexcept
        : listen_fd_(other.listen_fd_), addr_(other.addr_) {
        other.listen_fd_ = -1;
    }

    acceptor &operator=(acceptor &&other) noexcept {
        if (this != &other) [[likely]] {
            if (listen_fd_ >= 0) {
                ::close(listen_fd_);
            }
            listen_fd_ = other.listen_fd_;
            addr_ = other.addr_;
            other.listen_fd_ = -1;
        }
        return *this;
    }

    acceptor(const acceptor &) = delete;
    acceptor &operator=(const acceptor &) = delete;

    ~acceptor() {
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
        }
    }

    /// @brief 接受新连接（延迟 IO）
    /// @return 新连接的 socket，通过 int fd 返回
    auto accept() {
        return lazy::accept(listen_fd_);
    }

    /// @brief 接受新连接并获取客户端地址（延迟 IO）
    auto accept(inet_address &peer_addr) {
        return lazy::accept(
            listen_fd_,
            reinterpret_cast<sockaddr *>(peer_addr.storage_ptr()),
            peer_addr.storage_len_ptr());
    }

    [[nodiscard]] int fd() const noexcept { return listen_fd_; }

    [[nodiscard]] const inet_address &local_addr() const noexcept {
        return addr_;
    }

private:
    int listen_fd_ = -1;
    inet_address addr_;
};

} // namespace net
