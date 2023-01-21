#pragma once

#include "word.hpp"
#include <fstream>
#include <vector>

class word_list_type : public std::vector<word_type>
{
public:
    word_list_type() {}
    word_list_type(const std::vector<word_type> &&wl) : std::vector<word_type>(wl) {}

    bool contains(const word_type &word) const
    {
        for (auto &&w : *this)
        {
            if (w == word)
            {
                return true;
            }
        }
        return false;
    }
};

template <typename StreamT>
StreamT &operator<<(StreamT &s, const word_list_type &wl)
{
    s << "{ ";
    bool first = true;
    for (auto &&w : wl)
    {
        if (first)
            first = false;
        else
            s << ", ";
        s << w;
    }
    s << " }";

    return s;
}

word_list_type read_5_letter_words(const std::string &filename)
{
    std::ifstream file;
    file.open(filename);
    word_list_type words;
    while (file.good())
    {
        std::string s;
        std::getline(file, s);
        while (s.size() > 0 && (s[s.size() - 1] == '\r' || s[s.size() - 1] == '\n'))
        {
            s.resize(s.size() - 1); // strip whitespace
        }

        if (s.size() == 5)
        {
            words.push_back(s);
        }
    }

    return words;
}
