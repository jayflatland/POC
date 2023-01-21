#pragma once

#include <iostream>

#include "pair_hash.hpp"
#include "word.hpp"
#include "word_list.hpp"
#include "mask.hpp"
#include "match_result.hpp"
#include "util.hpp"

struct results_type
{
    size_t worst_count_ = 0;
    word_type worst_solution_;
    size_t counts_sum_ = 0;
    size_t counts_cnt_ = 0;
    word_type w1_, w2_, w3_;

    double avg() const { return (double)counts_sum_ / (double)counts_cnt_; }

    double score() const
    {
        if (counts_cnt_ == 0)
        {
            return -9999999;
        }
        // return -avg();
        return -(double)worst_count_;
    }

    template <typename StreamT>
    friend StreamT &operator<<(StreamT &os, const results_type &r)
    {
        os << "guess_words: {" << r.w1_ << ", " << r.w2_ << ", " << r.w3_ << "}";
        os << " worst: " << r.worst_solution_ << " (" << r.worst_count_ << ")";
        os << " avg:" << r.avg();
        os << " score: " << r.score();
        return os;
    }
};

inline match_results_type quick_calc_match_results(const word_type &solution, const word_type &guess)
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
            results.m_pos[i].m_color = color_type::green;
            solution_copy[i] = '_';
            guess_copy[i] = '*';
        }
    }

    // check for yellows
    for (size_t i = 0; i < 5; i++)
    {
        // for (size_t j = 0; j < 5; j++)
        // {
        //     // std::cout << "i=" << i << ", solution_copy=" << solution_copy << ", guess_copy=" << guess_copy << ", results=" << results << std::endl;
        //     if (guess_copy[i] == solution_copy[j])
        //     {
        //         results.m_pos[i].m_color = color_type::yellow;
        //         solution_copy[j] = '_';
        //         guess_copy[i] = '*';
        //     }
        // }

        if (solution_copy.contains(guess_copy[i]))
        {
            results.m_pos[i].m_color = color_type::yellow;
            solution_copy.remove(guess_copy[i]);
            guess_copy[i] = '*';
        }
    }

    return results;
}

inline void mask_solutions_that_match(const match_results_type &match_results, const word_list_type &words, mask_type &mask)
{
    word_list_type remaining_solutions;
    assert(words.size() == mask.size());
    for (int i = 0; i < words.size(); i++)
    {
        if (!mask[i])
        {
            continue;
        }

        auto test_results = quick_calc_match_results(words[i], match_results.guess());
        if (test_results != match_results)
        {
            mask[i] = false;
        }
    }
}

inline void find_best_first_three_words()
{
    // auto words = read_5_letter_words("wordle_words.txt");
    // auto words = read_5_letter_words("wordle_solutions.txt");
    // auto words = read_5_letter_words("corncob_lowercase.txt");
    // auto words = read_5_letter_words("dwyl_alpha_words.txt");
    // auto words = read_5_letter_words("sample_100_words.txt");
    auto words = read_5_letter_words("sample_1000_words.txt");

    std::cout << words.size() << " words" << std::endl;

    /*=========================================================================
    Strategy:
    For each solution, a set of guess words will produce a set of possible
    solutions.  We want to know the guess words that produce the most balanced
    set of
    =========================================================================*/
    // pre-compute word/solution masks
    // std::cout << "pre-compute word/solution masks" << std::endl;
    // std::unordered_map<std::pair<word_type, word_type>, mask_type> mask_cache;
    // for (auto &&w : words)
    // {
    //     for (auto &&solution : words)
    //     {
    //         mask_type mask = full_mask(words.size());
    //         mask_solutions_that_match(quick_calc_match_results(solution, w), words, mask);
    //         mask_cache[std::make_pair(solution, w)] = mask;
    //     }
    //     std::cout << w << " done" << std::endl;
    // }

    size_t total_combos_to_test = words.size() * (words.size() - 1) * (words.size() - 2);
    std::cout << "total_combos_to_test=" << fmt::format(std::locale("en_US.UTF-8"), "{:L}", total_combos_to_test) << std::endl;

    size_t combos_tested = 0;
    std::chrono::time_point start_time = std::chrono::system_clock::now();
    results_type best;
    for (auto it1 = words.begin(); it1 != words.end(); it1++)
    {
        auto &&w1 = *it1;
        for (auto it2 = it1 + 1; it2 != words.end(); it2++)
        {
            auto &&w2 = *it2;
            for (auto it3 = it2 + 1; it3 != words.end(); it3++)
            {
                auto &&w3 = *it3;
                results_type r;
                r.w1_ = w1;
                r.w2_ = w2;
                r.w3_ = w3;
                for (auto &&solution : words)
                {
                    mask_type mask = full_mask(words.size());
                    mask_solutions_that_match(quick_calc_match_results(solution, w1), words, mask);
                    mask_solutions_that_match(quick_calc_match_results(solution, w2), words, mask);
                    mask_solutions_that_match(quick_calc_match_results(solution, w3), words, mask);

                    // auto &&m1 = mask_cache[std::make_pair(solution, w1)];
                    // auto &&m2 = mask_cache[std::make_pair(solution, w2)];
                    // auto &&m3 = mask_cache[std::make_pair(solution, w3)];
                    // auto mask = m1 & m2 & m3;

                    size_t cnt = std::count_if(mask.begin(), mask.end(), [](auto &&e)
                                               { return e; });

                    r.counts_sum_ += cnt;
                    r.counts_cnt_++;
                    if (cnt > r.worst_count_)
                    {
                        r.worst_count_ = cnt;
                        r.worst_solution_ = solution;
                    }
                }

                if (r.score() > best.score())
                {
                    best = r;
                }
                combos_tested++;
                std::chrono::time_point now = std::chrono::system_clock::now();
                auto elapsed = now - start_time;
                auto avg_elapsed_per = elapsed / combos_tested;
                size_t remaining_combos_to_test = total_combos_to_test - combos_tested;
                auto est_time_remaining = remaining_combos_to_test * avg_elapsed_per;
                auto est_finish_time = now + est_time_remaining;
                std::cout << "est_finish_time = "
                          << fmt::format("{:%Y-%m-%d %H:%M:%S}", est_finish_time)
                          << " (" << std::chrono::duration_cast<std::chrono::microseconds>(avg_elapsed_per).count() * 0.001 << "ms)"
                          << " (" << std::chrono::duration_cast<std::chrono::hours>(est_time_remaining).count() / 24.0 << " days)"
                          << std::endl;
                std::cout << "    this: " << r << std::endl;
                std::cout << "    best: " << best << std::endl;
                std::cout << std::endl;
            }
        }
    }
}
