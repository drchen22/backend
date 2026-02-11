#pragma once

#include <cstdint>
#include <format>
#include <iomanip>
#include <mutex>
#include <print>
#include <source_location>
#include <string>
#include <chrono>
enum class LogLevel : std::uint8_t {
    DEBUG,
    INFO,
    WARN,
    ERROR,
};

constexpr std::string_view to_string(LogLevel lev) {
    switch (lev) {
    case LogLevel::DEBUG: return "DEBUG";
    case LogLevel::INFO:  return "INFO";
    case LogLevel::WARN:  return "WARN";
    case LogLevel::ERROR: return "ERROR";
    default:              return "UNKNOWN";
    }
}

constexpr std::string_view color_for(LogLevel lev) {
    switch (lev) {
    case LogLevel::DEBUG: return "\033[36m";
    case LogLevel::INFO:  return "\033[32m";
    case LogLevel::WARN:  return "\033[33m";
    case LogLevel::ERROR: return "\033[31m";
    default:              return "\033[0m";
    }
}

static std::string extract_function_name(const std::string& full_func_name) {
    std::string func_name = full_func_name;

    // 移除返回类型
    std::size_t space_pos = func_name.find_last_of(' ');
    if (space_pos != std::string::npos) {
        func_name = func_name.substr(space_pos + 1);
    }

    // 移除参数列表
    std::size_t paren_pos = func_name.find('(');
    if (paren_pos != std::string::npos) {
        func_name = func_name.substr(0, paren_pos);
    }

    // 移除模板参数
    std::size_t template_pos = func_name.find('<');
    if (template_pos != std::string::npos) {
        func_name = func_name.substr(0, template_pos);
    }

    return func_name;
}


class LOG {
public:
    static LOG &instance() {
        static LOG instance;
        return instance;
    }

    template <class... Args>
    static void
    log(LogLevel lev, std::format_string<Args...> fmt, Args &&...args,
        std::source_location loc = std::source_location::current()) {
        auto &self = instance();

        if (lev < self._min_level) {
            return;
        }

        std::scoped_lock lock(self._mtx);

        auto now = std::chrono::system_clock::now();
        std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&time);

        // Format time string using strftime
        std::ostringstream time_stream;
        time_stream << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        std::string time_str = time_stream.str();

        // Format the message first
        auto message = std::format(fmt, std::forward<Args>(args)...);

        // Then format the complete log line
        auto formatted = std::format("[{}] [{}] {}:{} {}() - {}",
                                    time_str, to_string(lev), loc.file_name(),
                                    loc.line(), extract_function_name(loc.function_name()), message);


        std::println("{}{}\033[0m", color_for(lev), formatted);
    };

    template <typename... Args>
    static void
    d(std::format_string<Args...> fmt, Args &&...args,
      std::source_location const &loc = std::source_location::current()) {
        log(LogLevel::DEBUG, fmt, std::forward<Args>(args)..., loc);
    };

    template <typename... Args>
    static void
    i(std::format_string<Args...> fmt, Args &&...args,
      std::source_location const &loc = std::source_location::current()) {
        log(LogLevel::INFO, fmt, std::forward<Args>(args)..., loc);
    };

    template <typename... Args>
    static void
    w(std::format_string<Args...> fmt, Args &&...args,
      std::source_location const &loc = std::source_location::current()) {
        log(LogLevel::WARN, fmt, std::forward<Args>(args)..., loc);
    };

    template <typename... Args>
    static void
    e(std::format_string<Args...> fmt, Args &&...args,
      std::source_location const &loc = std::source_location::current()) {
        log(LogLevel::ERROR, fmt, std::forward<Args>(args)..., loc);
    };

private:
    LogLevel _min_level = LogLevel::DEBUG;
    std::mutex _mtx;
};
