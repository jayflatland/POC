#pragma once

#include <iostream>
#include <unordered_map>
#include <chrono>
#include <fmt/format.h>

#include "pair_hash.hpp"
#include "word.hpp"
#include "word_list.hpp"
#include "mask.hpp"
#include "match_result.hpp"
#include "util.hpp"

using test_type = std::vector<word_type>;

// struct test_type
// {
//     word_type w1_, w2_, w3_;
// };

template <typename StreamT>
StreamT &operator<<(StreamT &os, const test_type &x)
{
    os << "{";
    bool first = true;
    for (auto &&w : x)
    {
        if (!first)
        {
            os << ",";
        }
        first = false;
        // os << '"' << w << '"';
        os << w;
    }
    os << "}";
    return os;
}

inline int color_to_int(const color_type &color)
{
    switch (color)
    {
    default:
    case color_type::gray:
        return 0;
    case color_type::yellow:
        return 1;
    case color_type::green:
        return 2;
    }
    return 0;
}

struct packed_test_result_type// : uint64_t
{
public:
    packed_test_result_type() : x_(0) {}
    packed_test_result_type(const uint64_t &x)
        : x_(x)
    {
    }
    
    static constexpr uint64_t digit_mult() { return 3; }
    static constexpr uint64_t word_mult() { return 243; } // 3**5

    packed_test_result_type operator*(const packed_test_result_type& rhs) const
    {
        return packed_test_result_type(x_ * rhs.x_);
    }

    packed_test_result_type operator+(const packed_test_result_type& rhs) const
    {
        return packed_test_result_type(x_ + rhs.x_);
    }
    
    bool operator==(const packed_test_result_type& rhs) const
    {
        return x_ == rhs.x_;
    }

    // //easier debug
    // static const int digit_mult = 10;
    // static const int word_mult = 100000;
    
    template <typename StreamT>
    friend StreamT& operator<<(StreamT& os, const packed_test_result_type& rhs)
    {
        std::string s;
        uint64_t x = rhs.x_;
        if(x == 0) { s = "0"; }
        while(x > 0)
        {
            //os << (x%digit_mult());
            s = std::to_string(x%digit_mult()) + s;
            x /= digit_mult();
        }
        os << s;
        return os;
    }
    
    const auto& val() const { return x_; }
private:
    uint64_t x_;
};


template<>
struct std::hash<packed_test_result_type>
{
    std::size_t operator()(const packed_test_result_type& x) const
    {
        return std::hash<uint64_t>{}(x.val());
    }
};

// using packed_test_result_type = uint64_t;//not sure i can pack 3 words into u32

// using packed_test_result_type = uint64_t;
// static const packed_test_result_type packed_test_result_digit_mult = 10;
// static const packed_test_result_type packed_test_result_word_mult = 100000;

inline packed_test_result_type match_result_to_int(const match_results_type &m)
{
    packed_test_result_type r;
    for (auto &&lc : m.pos_)
    {
        auto &&c = lc.color_;
        r = r * packed_test_result_type::digit_mult() + color_to_int(c);
    }
    return r;
}

inline packed_test_result_type word_solution_to_int(const word_type &solution, const word_type &word)
{
    return match_result_to_int(calc_match_results(solution, word));
}

inline packed_test_result_type calc_test_result(const test_type &tst, const word_type &solution)
{
    packed_test_result_type r;
    for (auto &&w : tst)
    {
        r = r * packed_test_result_type::word_mult() + word_solution_to_int(solution, w);
    }
    return r;
}

class score_type
{
public:
    double score() const
    {
        return -100.0 * max_ - avg();
        // return cnt_;
        // return -avg();
    }

    int max_ = 0;
    int total_ = 0;
    int cnt_ = 0;
    packed_test_result_type mr_;

    double avg() const
    {
        return (double)total_ / cnt_;
    }

    bool operator<(const score_type &rhs) const { return score() < rhs.score(); }
    bool operator<=(const score_type &rhs) const { return score() <= rhs.score(); }
};

template <typename StreamT>
StreamT &operator<<(StreamT &os, const score_type &x)
{
    os << "(cnt=" << x.cnt_ << ", max=" << x.max_ << ", avg=" << x.avg() << ", score=" << x.score() << ", mr=" << x.mr_ << ")";
    return os;
}

inline score_type calc_score_for_test(const word_list_type &words, const test_type &test)
{
    std::unordered_map<packed_test_result_type, int> counts;
    for (auto &&s : words)
    {
        packed_test_result_type r = calc_test_result(test, s);
        counts[r]++;
    }

    score_type s;
    for (auto &&kv : counts)
    {
        auto &&cnt = kv.second;
        s.total_ += cnt;
        s.max_ = std::max(s.max_, cnt);
        s.cnt_++;
        s.mr_ = kv.first;
    }
    return s;
}

inline test_type random_mutate_test(const word_list_type &words, const test_type &parent, int loops=1)
{
    test_type child = parent;
    
    for(int i = 0; i < loops; i++)
    {
        int wn = random() % child.size();
        int wi = random() % words.size();

        child[wn] = words[wi];
    }
    return child;
}

inline test_type cycle_mutate_test(const word_list_type &words, const test_type &parent, int loops=1)
{
    test_type child = parent;
    
    for(int i = 0; i < loops; i++)
    {
        static int wi = 0;
        static int wn = 0;
        wn++;
        if (wn >= 3)
        {
            wi++;
            wn = 0;
        }
        if (wi >= words.size())
        {
            wi = 0;
        }

        auto w = words[wi];
        child[wn] = w;
    }
    return child;
}

inline test_type random_char_mutate_test(const word_list_type &words, const test_type &parent, int loops=1)
{
    test_type child = parent;
    
    for(int i = 0; i < loops; i++)
    {
        int word_num = random() % child.size();
        int char_num = random() % 5;
        char new_char = 'a' + random() % 26;

        child[word_num][char_num] = new_char;
    }
    return child;
}

inline void find_best_first_three_words()
{
    auto words = read_5_letter_words("wordle_words.txt");
    // auto words = read_5_letter_words("wordle_solutions.txt");
    auto solutions = read_5_letter_words("wordle_solutions.txt");
    // auto words = read_5_letter_words("wordle_solutions.txt");
    // auto words = read_5_letter_words("corncob_lowercase.txt");
    // auto words = read_5_letter_words("dwyl_alpha_words.txt");
    // auto words = read_5_letter_words("sample_100_words.txt");
    // auto words = read_5_letter_words("sample_1000_words.txt");
    // auto&& solutions = words;

    std::cout << words.size() << " words" << std::endl;
    std::cout << solutions.size() << " solutions" << std::endl;

    test_type best_test{"homed", "clang", "sprit"};
    // test_type best_test{words.front()};
    // test_type best_test{words.front(), words.front()};
    // test_type best_test{words.front(), words.front(), words.front()};
    // test_type best_test{words.front(), words.front(), words.front(), words.front()};

    // wordle words only
    // best={berth,milky,paved,gowns}=>(cnt=10354, max=11, avg=1.25285, score=-1101.25, mr=1000001110002002000)
    // best={wacko,trugs,impel}=>(cnt=5721, max=33, avg=2.26744, score=-3302.27, mr=112000200000)
    // best={homed,clang,sprit}=>(cnt=6935, max=34, avg=1.87051, score=-3401.87, mr=1000010001110)
    // best={degum,lanch,spirt}=>(cnt=6468, max=28, avg=2.00557, score=-2802.01, mr=10201200020000)

    // wordle words and solutions
    // best={homed,clang,sprit}=>(cnt=1936, max=6, avg=1.19576, score=-601.196, mr=2000000000000)
    // best={comps,ruble,daint}=>(cnt=1908, max=6, avg=1.21331, score=-601.213, mr=101010200000100)
    // best={pronk,wight,clued,samba}=>(cnt=2212, max=3, avg=1.04656, score=-301.047, mr=20020100000200)

    score_type best_score = calc_score_for_test(solutions, best_test);
    std::cout << "best=" << best_test << "=>" << best_score << std::endl;

    int test_cnt = 0;
    auto start_time = std::chrono::system_clock::now();
    auto last_report_time = start_time;

    auto test_rate = [&]()
    {
        auto now = std::chrono::system_clock::now();
        auto dt = now - start_time;
        return (double)test_cnt * 1e9 / std::chrono::nanoseconds(dt).count();
    };

    while (true)
    {
        test_cnt++;
        // test_type test = random_char_mutate_test(words, best_test, 10);
        test_type test = random_mutate_test(words, best_test, random() % 3);
        // test_type test = cycle_mutate_test(words, best_test, 1);
        score_type score = calc_score_for_test(solutions, test);
        if (best_score < score)
        {
            best_score = score;
            best_test = test;
            std::cout << test_rate() << " per second [" << test_cnt << "] best=" << best_test << "=>" << best_score << std::endl;
        }

        auto now = std::chrono::system_clock::now();
        auto since_last_report = now - last_report_time;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(since_last_report).count() >= 60000)
        {
            std::cout << test_rate() << " per second [" << test_cnt << "]" << std::endl;
            last_report_time = now;
        }
    }
}
