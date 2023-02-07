#include <fmt/format.h>
#include <fmt/chrono.h>
#include <fstream>
#include <iostream>
#include <array>
#include <vector>
#include <limits>
#include <chrono>
#include <unordered_map>
#include <boost/program_options.hpp>

#include "color.hpp"
#include "word.hpp"
#include "word_list.hpp"
#include "mask.hpp"
#include "match_result.hpp"
#include "util.hpp"
#include "find_best_first_three_words.hpp"

namespace po = boost::program_options;

/*
Best words: {aesir, cloth, boing}
*/

// #define GUESS_LIST      "wordle_words.txt"
// #define SOLUTIONS_LIST  "wordle_solutions.txt"
// #define BEST_FIRST_GUESS "aesir"

// #define GUESS_LIST      "corncob_lowercase.txt"
// #define SOLUTIONS_LIST  "corncob_lowercase.txt"
// #define BEST_FIRST_GUESS "serai"

#define GUESS_LIST "dwyl_alpha_words.txt"
#define SOLUTIONS_LIST "dwyl_alpha_words.txt"
#define BEST_FIRST_GUESS "abase"

inline word_type find_best_guess_word(const word_list_type &possible_solutions, const word_list_type &words_to_try, bool verbose = false)
{
    if (possible_solutions.size() == 1)
    {
        return possible_solutions.front();
    }

    double best_avg_remaining_solution_cnt = std::numeric_limits<double>::max();
    word_type best_guess;
    for (auto &&guess : words_to_try)
    {
        // int sum_remaining_solution_cnt = 0;
        // int sum_cnt = 0;
        int max_remaining_solution_cnt = 0;
        for (auto &&solution : possible_solutions)
        {
            auto results = calc_match_results(solution, guess);
            auto cnt = count_solutions_that_match(results, possible_solutions);
            // sum_cnt += 1;
            // sum_remaining_solution_cnt += cnt;
            max_remaining_solution_cnt = std::max(max_remaining_solution_cnt, cnt);
            // if(guess == "comet")
            // {
            //     std::cout << guess << ", " << solution << ", " << cnt << std::endl;
            // }
        }
        // double score = (double)sum_remaining_solution_cnt / (double)sum_cnt;
        double score = max_remaining_solution_cnt;
        if (score < best_avg_remaining_solution_cnt)
        {
            // if count is low then require the guess be a solution
            // if(score > 1 || possible_solutions.contains(guess))
            {
                best_avg_remaining_solution_cnt = score;
                best_guess = guess;
                // std::cout << "New best guess: " << best_guess << ", " << best_avg_remaining_solution_cnt << std::endl;
                // std::cout << "    score=" << score << ", sum_remaining_solution_cnt=" << sum_remaining_solution_cnt << ", sum_cnt=" << sum_cnt << std::endl;
            }
        }
        if (verbose)
        {
            std::cout << guess << ", " << score << ", " << best_guess << ", " << best_avg_remaining_solution_cnt << std::endl;
        }
    }
    return best_guess;
}

inline word_list_type find_best_first_two_guess_words(const word_list_type &possible_solutions, const word_list_type &words_to_try, bool verbose = false)
{
    double best_avg_remaining_solution_cnt = std::numeric_limits<double>::max();
    word_list_type best_guesses;
    // for(auto && guess1 : words_to_try)
    auto guess1 = word_type("aesir");
    {
        // for(auto && guess2 : words_to_try)
        auto guess2 = word_type("cloth");
        {
            for (auto &&guess3 : words_to_try)
            {
                // int sum_remaining_solution_cnt = 0;
                // int sum_cnt = 0;
                int max_remaining_solution_cnt = 0;
                for (auto &&solution : possible_solutions)
                {
                    match_results_type results;
                    results = calc_match_results(solution, guess1);
                    auto solutions1 = find_solutions_that_match(results, possible_solutions);
                    results = calc_match_results(solution, guess2);
                    auto solutions2 = find_solutions_that_match(results, solutions1);
                    results = calc_match_results(solution, guess3);
                    auto cnt = count_solutions_that_match(results, solutions2);
                    // sum_cnt += 1;
                    // sum_remaining_solution_cnt += cnt;
                    max_remaining_solution_cnt = std::max(max_remaining_solution_cnt, cnt);
                }
                // double score = (double)sum_remaining_solution_cnt / (double)sum_cnt;
                double score = max_remaining_solution_cnt;
                if (score < best_avg_remaining_solution_cnt)
                {
                    // if count is low then require the guess be a solution
                    // if(score > 5 || possible_solutions.contains(guess))
                    {
                        best_avg_remaining_solution_cnt = score;
                        best_guesses = word_list_type();
                        best_guesses.push_back(guess1);
                        best_guesses.push_back(guess2);
                        best_guesses.push_back(guess3);
                        // std::cout << "New best guess: " << best_guesses << ", " << best_avg_remaining_solution_cnt << std::endl;
                        // std::cout << "    score=" << score << ", sum_remaining_solution_cnt=" << sum_remaining_solution_cnt << ", sum_cnt=" << sum_cnt << std::endl;
                    }
                }
                if (verbose)
                {
                    std::cout << guess1 << ", " << guess2 << ", " << guess3 << ", " << score << ", " << best_guesses << ", " << best_avg_remaining_solution_cnt << std::endl;
                }
            }
        }
    }
    return best_guesses;
}

inline void assistant()
{
    auto best_first_word = word_type(BEST_FIRST_GUESS);
    auto words = read_5_letter_words(GUESS_LIST);
    auto solutions = read_5_letter_words(SOLUTIONS_LIST);
    std::cout << words.size() << " words, " << solutions.size() << " solutions" << std::endl;

    std::vector<match_results_type> guesses_and_results;

    std::cout << "Best first word is '" << best_first_word << "'" << std::endl;

    match_results_type mr;
    mr.set_guess(best_first_word);
    while (true)
    {
        std::string s;
        // std::cout << "Enter guess: " << mr.guess() << std::endl;
        std::cout << "Enter guess: ";
        std::getline(std::cin, s);
        mr.set_guess(s.c_str());

        std::cout << "Enter results (e.g. gbgyyb): ";
        std::getline(std::cin, s);
        mr.set_colors(s.c_str());
        guesses_and_results.push_back(mr);

        auto possible_solutions = solutions;
        auto words_to_try = words;

        for (auto &&match_results : guesses_and_results)
        {
            possible_solutions = find_solutions_that_match(match_results, possible_solutions);
            std::cout << "Guess: " << match_results << " => " << possible_solutions.size() << " solutions left";
            if (possible_solutions.size() < 20)
            {
                std::cout << ": " << possible_solutions;
            }
            std::cout << std::endl;
        }

        std::cout << "Checking " << words_to_try.size() << " words for best guess word to reduce " << possible_solutions.size() << " possible solutions" << std::endl;
        auto best_guess = find_best_guess_word(possible_solutions, words_to_try);
        std::cout << "Best word to guess is '" << best_guess << "'" << std::endl;
        mr.set_guess(best_guess);
    }
}

inline void autoplay()
{
    auto best_first_word = word_type(BEST_FIRST_GUESS);
    auto words = read_5_letter_words(GUESS_LIST);
    auto solutions = read_5_letter_words(SOLUTIONS_LIST);
    std::cout << words.size() << " words, " << solutions.size() << " solutions" << std::endl;

    std::vector<match_results_type> guesses_and_results;

    std::cout << "Best first word is '" << best_first_word << "'" << std::endl;

    match_results_type mr;
    mr.set_guess(best_first_word);
    while (true)
    {
        std::string s;
        std::cout << "Enter guess: " << mr.guess() << std::endl;
        // std::cout << "Enter guess: ";
        // std::getline(std::cin, s);
        // mr.set_guess(s.c_str());

        std::cout << "Enter results (e.g. gbgyyb): ";
        std::getline(std::cin, s);
        mr.set_colors(s.c_str());
        guesses_and_results.push_back(mr);

        auto possible_solutions = solutions;
        auto words_to_try = words;

        for (auto &&match_results : guesses_and_results)
        {
            possible_solutions = find_solutions_that_match(match_results, possible_solutions);
            std::cout << "Guess: " << match_results << " => " << possible_solutions.size() << " solutions left";
            if (possible_solutions.size() < 20)
            {
                std::cout << ": " << possible_solutions;
            }
            std::cout << std::endl;
        }

        std::cout << "Checking " << words_to_try.size() << " words for best guess word to reduce " << possible_solutions.size() << " possible solutions" << std::endl;
        auto best_guess = find_best_guess_word(possible_solutions, words_to_try);
        std::cout << "Best word to guess is '" << best_guess << "'" << std::endl;
        mr.set_guess(best_guess);
    }
}

inline void find_best_first_word()
{
    auto words = read_5_letter_words(GUESS_LIST);
    auto solutions = read_5_letter_words(SOLUTIONS_LIST);
    std::cout << words.size() << " words, " << solutions.size() << " solutions" << std::endl;

    std::cout << "Checking " << words.size() << " words for best guess word to reduce " << solutions.size() << " possible solutions" << std::endl;
    auto best_guess = find_best_guess_word(solutions, words, true);
    std::cout << "Best word to guess is " << best_guess << std::endl;
}

inline void find_best_first_two_words()
{
    auto words = read_5_letter_words(GUESS_LIST);
    auto solutions = read_5_letter_words(SOLUTIONS_LIST);
    std::cout << words.size() << " words, " << solutions.size() << " solutions" << std::endl;

    std::cout << "Checking " << words.size() << " words for best guess word to reduce " << solutions.size() << " possible solutions" << std::endl;
    auto best_guesses = find_best_first_two_guess_words(solutions, words, true);
    std::cout << "Best word to guesses are " << best_guesses << std::endl;
}

inline void test()
{
    // std::cout << calc_match_results("awful", "cabal") << std::endl;
    //  word_list_type solutions = std::vector<word_type>{"batty", "catty", "patty", "fatty"};
    //  word_list_type words = std::vector<word_type>{"batty", "catty", "patty", "fatty", "bcpfx"};
    //  auto guess = find_best_guess_word(solutions, words, true);
    //  std::cout << "guess=" << guess << std::endl;

    auto words = read_5_letter_words("corncob_lowercase.txt");
    std::cout << words.size() << std::endl;
}

int main(int argc, char **argv)
{
    po::options_description desc("Allowed options");
    desc.add_options()("help", "produce help message")("autoplay", "run auto play")("assistant", "run play assistant")("test", "test")("find-best-first-word", "find best first word")("find-best-first-two-words", "find best first two words")("find-best-first-three-words", "find best first three words");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    // po::store(po::command_line_parser(argc, argv). options(desc).positional(p).run(), vm);
    po::notify(vm);

    if (vm.count("help") || argc == 1)
    {
        std::cout << desc << std::endl;
        return 1;
    }

    if (vm.count("autoplay"))
    {
        autoplay();
    }
    else if (vm.count("assistant"))
    {
        assistant();
    }
    else if (vm.count("test"))
    {
        test();
    }
    else if (vm.count("find-best-first-word"))
    {
        find_best_first_word();
    }
    else if (vm.count("find-best-first-two-words"))
    {
        find_best_first_two_words();
    }
    else if (vm.count("find-best-first-three-words"))
    {
        find_best_first_three_words();
    }
    return 0;
}
