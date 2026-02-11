#pragma once


template<class T>
concept tasklike = requires {
    typename T::value_type;
    typename T::promise_type;
    typename T::is_tasklike;
};
