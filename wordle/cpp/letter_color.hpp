#pragma once

#include "color.hpp"

class letter_color_type
{
public:
    char m_letter;
    color_type m_color;

    bool operator==(const letter_color_type &o) const
    {
        return m_color == o.m_color;
    }
};

template <typename StreamT>
StreamT &operator<<(StreamT &s, const letter_color_type &lc)
{
    switch (lc.m_color)
    {
    case color_type::gray:
        s << "\u001b[37;1m\u001b[40;1m" << lc.m_letter << "\u001b[0m";
        break;
    case color_type::yellow:
        s << "\u001b[30m\u001b[43;1m" << lc.m_letter << "\u001b[0m";
        break;
    case color_type::green:
        s << "\u001b[30m\u001b[42;1m" << lc.m_letter << "\u001b[0m";
        break;
    default:
        s << lc.m_letter;
        break;
    }
    return s;
}

