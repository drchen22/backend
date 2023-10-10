#pragma once

#include <algorithm>
#include <cstdint>
#include <format>
#include <iostream>
#include <source_location>

#define FOREACH_LOG_LEVEL(f) \
    f(trace) \
    f(debug) \
    f(info) \
    f(critical) \
    f(warning) \
    f(error) \
    f(fatal) 

enum class log_level : std::uint8_t{
#define _FUNCTION(name) name,
        FOREACH_LOG_LEVEL(_FUNCTION)
#undef _FUNCTION
};

std::string log_level_name(log_level lev) {
    switch (lev) {
#define _FUNCTION(name) case log_level::name: return #name;
    FOREACH_LOG_LEVEL(_FUNCTION)
#undef _FUNCTION
    }
    return "unknown";
}

template <class T>
struct with_source_location {
private:
    T inner;
    std::source_location loc;

public:
    template <class U> requires std::constructible_from<T, U>
    consteval with_source_location(U &&inner, std::source_location loc = std::source_location::current())
    : inner(std::forward<U>(inner)), loc(std::move(loc)) {}

    constexpr T const &format() const { return inner; }
    constexpr std::source_location const &location() const { return loc; }
};

