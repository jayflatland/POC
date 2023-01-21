#pragma once

#include <array>
#include <functional>

class word_type : public std::array<char, 5>
{
public:
    word_type()
    {
        std::fill(this->begin(), this->end(), '_');
    }

    word_type(const char *s)
    {
        std::copy(s, s + 5, this->begin());
    }

    word_type(const std::string &s)
    {
        std::copy(s.c_str(), s.c_str() + 5, this->begin());
    }

    uint64_t hash() const
    {
        uint64_t h = 0;
        for (auto &&c : *this)
        {
            h = (h * 26) + (uint64_t)(c - 'a');
        }
        return h;
    }

    static constexpr uint64_t max_hash() { return 11881376; /*26**5*/ }

    bool contains(char c) const
    {
        for (auto &&l : *this)
        {
            if (l == c)
            {
                return true;
            }
        }
        return false;
    }

    void remove(char x)
    {
        for (auto &l : *this)
        {
            if (l == x)
            {
                l = '_';
                break;
            }
        }
    }
};


template<>
struct std::hash<word_type>
{
    size_t operator()(const word_type& x) const
    {
        return x.hash();
    }
};

template <typename StreamT>
StreamT &operator<<(StreamT &s, const word_type &w)
{
    for (auto &&c : w)
    {
        s << c;
    }
    return s;
}

