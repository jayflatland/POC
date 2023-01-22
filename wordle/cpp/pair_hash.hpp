#pragma once

#include <functional>

template<>
template<typename A, typename B>
struct std::hash<std::pair<A, B>>
{
    using T = std::pair<A, B>;
    std::size_t operator()(const T& x) const
    {
        return std::hash<A>{}(x.first) ^ std::hash<B>{}(x.second);
    }
};
