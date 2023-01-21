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

#include "pair_hash.hpp"
#include "color.hpp"
#include "word.hpp"
#include "word_list.hpp"
#include "mask.hpp"
#include "match_result.hpp"
#include "util.hpp"

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


word_type find_best_guess_word(const word_list_type &possible_solutions, const word_list_type &words_to_try, bool verbose = false)
{
    if (possible_solutions.size() == 1)
    {
        return possible_solutions[0];
    }

    double best_avg_remaining_solution_cnt = std::numeric_limits<double>::max();
    word_type best_guess;
    for (auto &&guess : words_to_try)
    {
        int sum_remaining_solution_cnt = 0;
        int sum_cnt = 0;
        int max_remaining_solution_cnt = 0;
        for (auto &&solution : possible_solutions)
        {
            auto results = calc_match_results(solution, guess);
            auto cnt = count_solutions_that_match(results, possible_solutions);
            sum_cnt += 1;
            sum_remaining_solution_cnt += cnt;
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

word_list_type find_best_first_two_guess_words(const word_list_type &possible_solutions, const word_list_type &words_to_try, bool verbose = false)
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
                int sum_remaining_solution_cnt = 0;
                int sum_cnt = 0;
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
                    sum_cnt += 1;
                    sum_remaining_solution_cnt += cnt;
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

void assistant()
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

void autoplay()
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

void find_best_first_word()
{
    auto words = read_5_letter_words(GUESS_LIST);
    auto solutions = read_5_letter_words(SOLUTIONS_LIST);
    std::cout << words.size() << " words, " << solutions.size() << " solutions" << std::endl;

    std::cout << "Checking " << words.size() << " words for best guess word to reduce " << solutions.size() << " possible solutions" << std::endl;
    auto best_guess = find_best_guess_word(solutions, words, true);
    std::cout << "Best word to guess is " << best_guess << std::endl;
}

void find_best_first_two_words()
{
    auto words = read_5_letter_words(GUESS_LIST);
    auto solutions = read_5_letter_words(SOLUTIONS_LIST);
    std::cout << words.size() << " words, " << solutions.size() << " solutions" << std::endl;

    std::cout << "Checking " << words.size() << " words for best guess word to reduce " << solutions.size() << " possible solutions" << std::endl;
    auto best_guesses = find_best_first_two_guess_words(solutions, words, true);
    std::cout << "Best word to guesses are " << best_guesses << std::endl;
}


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

void find_best_first_three_words()
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
    //pre-compute word/solution masks
    std::cout << "pre-compute word/solution masks" << std::endl;
    std::unordered_map<std::pair<word_type, word_type>, mask_type> mask_cache;
    for (auto &&w : words)
    {
        for (auto &&solution : words)
        {
            mask_type mask = full_mask(words.size());
            mask_solutions_that_match(quick_calc_match_results(solution, w), words, mask);
            mask_cache[std::make_pair(solution, w)] = mask;
        }
        std::cout << w << " done" << std::endl;
    }

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
                    // mask_type mask = full_mask(words.size());
                    // mask_solutions_that_match(quick_calc_match_results(solution, w1), words, mask);
                    // mask_solutions_that_match(quick_calc_match_results(solution, w2), words, mask);
                    // mask_solutions_that_match(quick_calc_match_results(solution, w3), words, mask);

                    auto&& m1 = mask_cache[std::make_pair(solution, w1)];
                    auto&& m2 = mask_cache[std::make_pair(solution, w2)];
                    auto&& m3 = mask_cache[std::make_pair(solution, w3)];
                    auto mask = m1 & m2 & m3;

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

void test()
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
