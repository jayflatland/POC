#pragma once

#include "color.hpp"

class letter_color_type
{
public:
    char letter_;
    color_type color_;

    bool operator==(const letter_color_type &o) const
    {
        return color_ == o.color_;
    }
};

template <typename StreamT>
StreamT &operator<<(StreamT &s, const letter_color_type &lc)
{
    switch (lc.color_)
    {
    case color_type::gray:
        s << "\u001b[37;1m\u001b[40;1m" << lc.letter_ << "\u001b[0m";
        break;
    case color_type::yellow:
        s << "\u001b[30m\u001b[43;1m" << lc.letter_ << "\u001b[0m";
        break;
    case color_type::green:
        s << "\u001b[30m\u001b[42;1m" << lc.letter_ << "\u001b[0m";
        break;
    default:
        s << lc.letter_;
        break;
    }
    return s;
}

