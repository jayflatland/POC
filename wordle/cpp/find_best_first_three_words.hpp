#pragma once

#include <iostream>
#include <unordered_map>

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

    void clear()
    {
        worst_count_ = 0;
        counts_sum_ = 0;
        counts_cnt_ = 0;
    }

    double loss() const
    {
        if (counts_cnt_ == 0)
        {
            return 9999999;
        }
        // return -avg();
        return worst_count_;
    }

    template <typename StreamT>
    friend StreamT &operator<<(StreamT &os, const results_type &r)
    {
        os << "guess_words: {" << r.w1_ << ", " << r.w2_ << ", " << r.w3_ << "}";
        os << " worst: " << r.worst_solution_ << " (" << r.worst_count_ << ")";
        os << " avg:" << r.avg();
        os << " loss: " << r.loss();
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
    // for (int i = 0; i < words.size(); i++)
    int i = 0;
    for (auto &&w : words)
    {
        if (!mask[i])
        {
            i++;
            continue;
        }

        auto test_results = quick_calc_match_results(w, match_results.guess());
        if (test_results != match_results)
        {
            mask[i] = false;
        }
        i++;
    }
}

inline results_type find_best(const word_list_type &words, const results_type &start_result, int idx)
{
    results_type best = start_result;
    // for (auto &&w : words)
    auto &&w = words[::rand() % words.size()];
    {
        std::cout << "w=" << w << std::endl;
        results_type r = best;
        r.clear();
        switch (idx)
        {
        case 1:
            r.w1_ = w;
            break;
        case 2:
            r.w2_ = w;
            break;
        case 3:
            r.w3_ = w;
            break;
        default:
            assert(false);
            break;
        }

        for (auto &&solution : words)
        {
            std::cout << "w=" << w << ", s=" << solution << std::endl;
            mask_type mask = full_mask(words.size());
            mask_solutions_that_match(quick_calc_match_results(solution, r.w1_), words, mask);
            mask_solutions_that_match(quick_calc_match_results(solution, r.w2_), words, mask);
            mask_solutions_that_match(quick_calc_match_results(solution, r.w3_), words, mask);
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

        if (r.loss() < best.loss())
        {
            std::cout << idx << ": " << best << std::endl;
            best = r;
        }
    }
    return best;
}

inline void find_best_first_three_words()
{
    /*=========================================================================
    Strategy:
    For each solution, a set of guess words will produce a set of possible
    solutions.  We want to know the guess words that produce the smallest max
    number of possible solutions.

    The primary problem is that for a given guess set G, I need to try every
    solution, and for every solution, I need to try every word.  So there's a
    tough N^2 problem in there.

    For each solution, I need to compute a match result set M, and then need
    to see for each word how many words fit that match set.

    def compute_loss(guess_words):
        max_loss = 0
        for s in words:
            match_set = calc_match_results(guess_words, s)
            loss = 0
            for w in words:
                M2 = calc_match_results(guess_words, w)
                if match_set == M2:
                    loss += 1
            max_loss = max(max_loss, loss)

    How can I speed that up?

    Are any calculations cacheable?
    =========================================================================*/

    auto words = read_5_letter_words("wordle_words.txt");
    // auto words = read_5_letter_words("wordle_solutions.txt");
    // auto words = read_5_letter_words("corncob_lowercase.txt");
    // auto words = read_5_letter_words("dwyl_alpha_words.txt");
    // auto words = read_5_letter_words("sample_100_words.txt");
    // auto words = read_5_letter_words("sample_1000_words.txt");

    std::cout << words.size() << " words" << std::endl;

    // std::unordered_map<std::pair<word_type, word_type>, mask_type> word_solution_to_mask_cache;
    // for (auto &&w : words)
    // {
    //     std::cout << "w=" << w << std::endl;
    //     for (auto &&s : words)
    //     {
    //         // std::cout << "w=" << w << ", s=" << s << std::endl;
    //         mask_type mask = full_mask(words.size());
    //         mask_solutions_that_match(quick_calc_match_results(s, w), words, mask);
    //         word_solution_to_mask_cache[std::make_pair(w, s)] = mask;
    //     }
    // }

    // results_type best;
    // best.w1_ = words.front();
    // best.w2_ = words.front();
    // best.w3_ = words.front();

    // while(true)
    // {
    //     best = find_best(words, best, 1);
    //     best = find_best(words, best, 2);
    //     best = find_best(words, best, 3);
    // }
}
