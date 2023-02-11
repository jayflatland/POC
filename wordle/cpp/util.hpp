#pragma once

#include "word_list.hpp"
#include "mask.hpp"
#include "match_result.hpp"

inline match_results_type calc_match_results(const word_type &solution, const word_type &guess)
{
    auto results = match_results_type();
    word_type solution_copy = solution;
    word_type guess_copy = guess;

    // copy in word
    results.set_guess(guess);

    // check for greens first
    for (size_t i = 0; i < 5; i++)
    {
        // std::cout << "i=" << i << ", solution_copy=" << solution_copy << ", guess_copy=" << guess_copy << ", results=" << results << std::endl;
        if (guess_copy[i] == solution_copy[i])
        {
            results.pos_[i].color_ = color_type::green;
            solution_copy[i] = '_';
            guess_copy[i] = '*';
        }
        // std::cout << "i=" << i << ", solution_copy=" << solution_copy << ", guess_copy=" << guess_copy << ", results=" << results << std::endl;
        // std::cout << std::endl;
    }

    // std::cout << std::endl;

    // check for yellows
    for (size_t i = 0; i < 5; i++)
    {
        // std::cout << "i=" << i << ", solution_copy=" << solution_copy << ", guess_copy=" << guess_copy << ", results=" << results << std::endl;
        if (solution_copy.contains(guess_copy[i]))
        {
            results.pos_[i].color_ = color_type::yellow;
            solution_copy.remove(guess_copy[i]);
            guess_copy[i] = '*';
        }
        // std::cout << "i=" << i << ", solution_copy=" << solution_copy << ", guess_copy=" << guess_copy << ", results=" << results << std::endl;
        // std::cout << std::endl;
    }

    return results;
}

inline word_list_type find_solutions_that_match(const match_results_type &match_results, const word_list_type &possible_solutions)
{
    word_list_type remaining_solutions;
    for (auto &&test_solution : possible_solutions)
    {
        auto test_results = calc_match_results(test_solution, match_results.guess());
        if (test_results == match_results)
        {
            remaining_solutions.push_back(test_solution);
        }
    }
    return remaining_solutions;
}

inline bool solution_is_possible_for_match_results(const word_type &solution, const match_results_type &match_results)
{
    auto test_results = calc_match_results(solution, match_results.guess());
    return test_results == match_results;
    // return match_results == match_results;
    // return true;
}

inline int count_solutions_that_match(const match_results_type &match_results, const word_list_type &possible_solutions)
{
    int cnt = 0;
    for (auto &&test_solution : possible_solutions)
    {
        if (solution_is_possible_for_match_results(test_solution, match_results))
        {
            cnt++;
        }
    }
    return cnt;
}
