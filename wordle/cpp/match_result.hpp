#pragma once

#include <array>
#include "letter_color.hpp"
#include "word.hpp"

class match_results_type
{
public:
    std::array<letter_color_type, 5> m_pos;

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
            r[i] = m_pos[i].m_letter;
        }
        return r;
    }

    void set_guess(const word_type &w)
    {
        for (int i = 0; i < 5; i++)
        {
            m_pos[i].m_letter = w[i];
        }
    }

    void set_colors(const char *colors)
    {
        for (int i = 0; i < 5; i++)
        {
            m_pos[i].m_color = color_type::gray;
            if (colors[i] == 'y')
                m_pos[i].m_color = color_type::yellow;
            if (colors[i] == 'g')
                m_pos[i].m_color = color_type::green;
        }
    }

    bool operator==(const match_results_type &o) const
    {
        for (size_t i = 0; i < m_pos.size(); i++)
        {
            if (!(m_pos[i] == o.m_pos[i]))
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
    for (auto &&p : m.m_pos)
    {
        s << p;
    }
    return s;
}

