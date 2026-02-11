#pragma once


#include <cstddef>
template <class... Ts>
struct type_list {
    struct is_type_list {};

    using type = type_list;

    static constexpr std::size_t size = sizeof...(Ts);

    template <class... Us>
    using append = type_list<Ts..., Us...>;
    template <class... Us>
    using prepend = type_list<Us..., Ts...>;
};

template <class T>
concept TL = requires {
    typename T::is_type_list;
    typename T::type;
};

/*
 * 基本查询操作
 * size - 获取类型数量 type_list::size
 * head_t - 获取第一个类型
 * last_t - 获取最后一个类型
 * at_t - 按索引获取类型
 * tails_t - 获取去掉第一个元素后的列表
 */

// 主模板声明，要求列表非空
template<TL in> requires (in::size > 0)
struct head;

// 偏特化：匹配至少有一个元素的type_list
template<class H, class... Ts>
struct head<type_list<H, Ts...>> {
    using type = H;
};

// 类型别名简化使用
template<TL in>
using head_t = typename head<in>::type;
