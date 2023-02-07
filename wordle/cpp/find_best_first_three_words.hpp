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

inline match_results_type fast_calc_match_results(const word_type &solution, const word_type &guess)
{
    auto results = match_results_type();
    word_type solution_copy = solution;
    word_type guess_copy = guess;

    // copy in word
    results.set_guess(guess);

    // check for greens first
    for (size_t i = 0; i < 5; i++)
    {
        if (guess_copy[i] == solution_copy[i])
        {
            results.pos_[i].color_ = color_type::green;
            solution_copy[i] = '_';
            guess_copy[i] = '*';
        }
    }

    // check for yellows
    for (size_t i = 0; i < 5; i++)
    {
        if (solution_copy.contains(guess_copy[i]))
        {
            results.pos_[i].color_ = color_type::yellow;
            solution_copy.remove(guess_copy[i]);
            //FASTER (not really) guess_copy[i] = '*';
        }
    }

    return results;
}


struct test_type
{
    word_type w1_, w2_, w3_;
};

template <typename StreamT>
StreamT &operator<<(StreamT &os, const test_type &x)
{
    os << "{\"" << x.w1_ << "\",\"" << x.w2_ << "\",\"" << x.w3_ << "\"}";
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

inline int match_result_to_int(const match_results_type &m)
{
    int r = 0;
    for (auto &&lc : m.pos_)
    {
        auto &&c = lc.color_;
        r = r * 3 + color_to_int(c);
    }
    return r;
}

inline int word_solution_to_int(const word_type &word, const word_type &solution)
{
    return match_result_to_int(fast_calc_match_results(solution, word));
}

inline int calc_test_result(const test_type &tst, const word_type &solution)
{
    static const int max_word_result = 243; // 3**5
    int r = 0;
    r = r * max_word_result + word_solution_to_int(solution, tst.w1_);
    r = r * max_word_result + word_solution_to_int(solution, tst.w2_);
    r = r * max_word_result + word_solution_to_int(solution, tst.w3_);
    return r;
}

class score_type
{
public:
    double score() const
    {
        return -max_;
        // return cnt_;
    }

    int max_ = 0;
    int total_ = 0;
    int cnt_ = 0;

    double avg() const
    {
        return (double)total_ / cnt_;
    }

    bool operator<(const score_type &rhs) const { return score() < rhs.score(); }
};

template <typename StreamT>
StreamT &operator<<(StreamT &os, const score_type &x)
{
    os << "(cnt=" << x.cnt_ << ", max=" << x.max_ << ", avg=" << x.avg() << ", score=" << x.score() << ")";
    return os;
}

inline score_type calc_score_for_test(const word_list_type &words, const test_type &test)
{
    std::unordered_map<int, int> counts;
    for (auto &&s : words)
    {
        int r = calc_test_result(test, s);
        counts[r]++;
    }

    score_type s;
    for (auto &&kv : counts)
    {
        auto &&cnt = kv.second;
        s.total_ += cnt;
        s.max_ = std::max(s.max_, cnt);
        s.cnt_++;
    }
    return s;
}

test_type random_mutate_test(const word_list_type &words, const test_type &parent)
{
    int wn = random() % 3;
    int wi = random() % words.size();

    auto w = words[wi];
    test_type child = parent;
    switch (wn)
    {
    case 0:
        child.w1_ = w;
        break;
    case 1:
        child.w2_ = w;
        break;
    case 2:
        child.w3_ = w;
        break;
    }
    return child;
}

test_type cycle_mutate_test(const word_list_type &words, const test_type &parent)
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
    test_type child = parent;
    switch (wn)
    {
    case 0:
        child.w1_ = w;
        break;
    case 1:
        child.w2_ = w;
        break;
    case 2:
        child.w3_ = w;
        break;
    }
    return child;
}

inline void find_best_first_three_words()
{
    /*=========================================================================
    Thinking again.
    Each set of 3 words is a test
    Each solution for a given test will result in a 3x match result.
    Each match result has 3**5=243 possibilities
    Each 3x match result has 3**15=14,348,907 possibilities
    =========================================================================*/

    auto words = read_5_letter_words("wordle_words.txt");
    auto solutions = read_5_letter_words("wordle_solutions.txt");
    // auto words = read_5_letter_words("wordle_solutions.txt");
    // auto words = read_5_letter_words("corncob_lowercase.txt");
    // auto words = read_5_letter_words("dwyl_alpha_words.txt");
    // auto words = read_5_letter_words("sample_100_words.txt");
    // auto words = read_5_letter_words("sample_1000_words.txt");
    // auto&& solutions = words;

    std::cout << words.size() << " words" << std::endl;
    std::cout << solutions.size() << " solutions" << std::endl;

    test_type best_test{words.front(), words.front(), words.front()};
    // test_type best_test{"corse","palet","bundt"};//(cnt=1836, max=9, avg=1.26089, score=1836);//wordle words/solutions, cnt
    // test_type best_test{"corse", "palet", "wings"}; //=>(cnt=1749, max=6, avg=1.32361, score=-6)//wordle words/solutions, max
    // test_type best_test{"aesir","cloth","bungy"};
    // test_type best_test{"meros","patly","deink"};
    // test_type best_test{"colts","spare","admin"};
    // best={"skran","cloth","derig"}=>-26
    // best={"freak","cloth","admin"}=>-7 wordl solutions, max
    // best={"cloth","kesar","admin"}=>-28, dwyl, max
    // best={"meros","patly","deink"}=>-26, dwyl, max
    score_type best_score = calc_score_for_test(solutions, best_test);
    std::cout << "best=" << best_test << "=>" << best_score << std::endl;

    int test_cnt = 0;
    auto start_time = std::chrono::system_clock::now();
    auto last_report_time = start_time;
    
    auto test_rate = [&]() {
        auto now = std::chrono::system_clock::now();
        auto dt = now - start_time;
        return (double)test_cnt * 1e9 / std::chrono::nanoseconds(dt).count();
    };
    
    while (true)
    {
        test_cnt++;
        test_type test = cycle_mutate_test(words, best_test);
        test = cycle_mutate_test(words, test);
        // test_type test = cycle_mutate_test(words, best_test);
        score_type score = calc_score_for_test(solutions, test);
        if (best_score < score)
        {
            best_score = score;
            best_test = test;
            std::cout << test_rate() << " per second [" << test_cnt << "] best=" << best_test << "=>" << best_score << std::endl;
        }

        auto now = std::chrono::system_clock::now();
        auto since_last_report = now - last_report_time;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(since_last_report).count() >= 5000)
        {
            std::cout << test_rate() << " per second [" << test_cnt << "] best=" << best_test << "=>" << best_score << std::endl;
            last_report_time = now;
        }
    }

    // brute force search
    // for (auto &&w1 : words)
    // {
    //     for (auto &&w2 : words)
    //     {
    //         std::cout << w1 << "," << w2 << std::endl;
    //         for (auto &&w3 : words)
    //         {
    //             test_cnt++;
    //             test_type test{w1, w2, w3};
    //             score_type score = calc_score_for_test(words, test);
    //             if (best_score < score)
    //             {
    //                 best_score = score;
    //                 best_test = test;
    //                 std::cout << "[" << test_cnt << "] best=" << best_test << "=>" << best_score << std::endl;
    //             }
    //         }
    //     }
    // }
}
