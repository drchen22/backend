#pragma once

/// @file inet_address.hpp
/// @brief IPv4/IPv6 地址抽象，为 socket 连接和绑定提供统一接口

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>

namespace net {

class inet_address {
public:
    inet_address() noexcept {
        std::memset(&addr6_, 0, sizeof(addr6_));
        addr6_.sin6_family = AF_INET;
        addr6_.sin6_addr = in6addr_loopback;
        addr6_.sin6_port = 0;
    }

    /// @brief 监听所有接口的指定端口（IPv4）
    explicit inet_address(uint16_t port) noexcept {
        std::memset(&addr_, 0, sizeof(addr_));
        addr_.sin_family = AF_INET;
        addr_.sin_addr.s_addr = htonl(INADDR_ANY);
        addr_.sin_port = htons(port);
    }

    /// @brief 指定 IP 和端口（IPv4）
    inet_address(std::string_view ip, uint16_t port) noexcept {
        std::memset(&addr_, 0, sizeof(addr_));
        addr_.sin_family = AF_INET;
        addr_.sin_port = htons(port);
        ::inet_pton(AF_INET, ip.data(), &addr_.sin_addr);
    }

    /// @brief 从原生 sockaddr 构造
    inet_address(const sockaddr *addr, socklen_t len) noexcept {
        std::memset(&addr6_, 0, sizeof(addr6_));
        if (addr->sa_family == AF_INET) {
            std::memcpy(&addr_, addr, sizeof(sockaddr_in));
        } else if (addr->sa_family == AF_INET6) {
            std::memcpy(&addr6_, addr, sizeof(sockaddr_in6));
        }
    }

    /// @brief 从 sockaddr_in 构造
    explicit inet_address(const sockaddr_in &addr) noexcept : addr_(addr) {}

    /// @brief 从 sockaddr_in6 构造
    explicit inet_address(const sockaddr_in6 &addr) noexcept : addr6_(addr) {}

    /// @brief DNS 解析，返回找到的第一个地址
    /// @param host 主机名或 IP 地址
    /// @param port 端口号
    /// @param hints 可选的 addrinfo 提示
    /// @return 解析得到的地址
    /// @throws std::runtime_error 解析失败时抛出
    static inet_address resolve(
        std::string_view host, uint16_t port, int family = AF_INET) {
        addrinfo hints{};
        hints.ai_family = family;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo *result = nullptr;
        std::string port_str = std::to_string(port);

        int rc = getaddrinfo(
            host.data(), port_str.c_str(), &hints, &result);
        if (rc != 0) {
            throw std::runtime_error(
                std::string("getaddrinfo failed: ") + gai_strerror(rc));
        }

        inet_address addr(
            result->ai_addr, static_cast<socklen_t>(result->ai_addrlen));
        freeaddrinfo(result);
        return addr;
    }

    [[nodiscard]] const sockaddr *sockaddr_ptr() const noexcept {
        return reinterpret_cast<const ::sockaddr *>(&addr6_);
    }

    [[nodiscard]] socklen_t sockaddr_len() const noexcept {
        return family() == AF_INET6 ? sizeof(sockaddr_in6)
                                    : sizeof(sockaddr_in);
    }

    [[nodiscard]] sa_family_t family() const noexcept {
        return addr6_.sin6_family;
    }

    [[nodiscard]] bool is_ipv4() const noexcept {
        return family() == AF_INET;
    }

    [[nodiscard]] bool is_ipv6() const noexcept {
        return family() == AF_INET6;
    }

    [[nodiscard]] uint16_t port() const noexcept {
        if (is_ipv4()) {
            return ntohs(addr_.sin_port);
        }
        return ntohs(addr6_.sin6_port);
    }

    [[nodiscard]] std::string ip() const noexcept {
        char buf[INET6_ADDRSTRLEN]{};
        if (is_ipv4()) {
            ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf));
        } else {
            ::inet_ntop(AF_INET6, &addr6_.sin6_addr, buf, sizeof(buf));
        }
        return buf;
    }

    [[nodiscard]] std::string to_string() const noexcept {
        if (is_ipv6()) {
            return std::string("[") + ip() + "]:" + std::to_string(port());
        }
        return ip() + ":" + std::to_string(port());
    }

    /// @brief 获取可写的底层存储指针（用于 accept 等需要写入地址的场景）
    [[nodiscard]] ::sockaddr *storage_ptr() noexcept {
        return reinterpret_cast<::sockaddr *>(&addr6_);
    }

    /// @brief 获取存储长度的指针（用于 accept 等需要写入长度的场景）
    [[nodiscard]] socklen_t *storage_len_ptr() noexcept {
        if (is_ipv4()) {
            storage_len_ = sizeof(sockaddr_in);
        } else {
            storage_len_ = sizeof(sockaddr_in6);
        }
        return &storage_len_;
    }

private:
    union {
        sockaddr_in addr_;
        sockaddr_in6 addr6_;
    };
    socklen_t storage_len_{sizeof(sockaddr_in)};
};

} // namespace net
