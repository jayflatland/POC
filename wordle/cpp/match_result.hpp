#pragma once

#include <array>
#include "letter_color.hpp"
#include "word.hpp"

class match_results_type
{
public:
    std::array<letter_color_type, 5> pos_;

    match_results_type()
    {
        set_guess("_____");
        set_colors("_____");
    }

    match_results_type(const char *word, const char *colors)
    {
        set_guess(word);
        set_colors(colors);
    }

    word_type guess() const
    {
        word_type r;
        for (int i = 0; i < 5; i++)
        {
            r[i] = pos_[i].letter_;
        }
        return r;
    }

    void set_guess(const word_type &w)
    {
        for (int i = 0; i < 5; i++)
        {
            pos_[i].letter_ = w[i];
        }
    }

    void set_colors(const char *colors)
    {
        for (int i = 0; i < 5; i++)
        {
            pos_[i].color_ = color_type::gray;
            if (colors[i] == 'y')
                pos_[i].color_ = color_type::yellow;
            if (colors[i] == 'g')
                pos_[i].color_ = color_type::green;
        }
    }

    bool operator==(const match_results_type &o) const
    {
        for (size_t i = 0; i < pos_.size(); i++)
        {
            if (!(pos_[i] == o.pos_[i]))
            {
                return false;
            }
        }
        return true;
    }

    bool operator!=(const match_results_type &o) const
    {
        return !(*this == o);
    }
};

template <typename StreamT>
StreamT &operator<<(StreamT &s, const match_results_type &m)
{
    for (auto &&p : m.pos_)
    {
        s << p;
    }
    return s;
}

