
#include <fstream>
#include <iostream>
#include <array>
#include <vector>
#include <limits>
#include <boost/program_options.hpp>

namespace po = boost::program_options;

enum class Color : uint8_t
{
    Gray,
    Yellow,
    Green
};

class Word : public std::array<char, 5>
{
public:
    Word()
    {
        std::fill(this->begin(), this->end(), '_');
    }

    Word(const char *s)
    {
        std::copy(s, s + 5, this->begin());
    }

    Word(const std::string &s)
    {
        std::copy(s.c_str(), s.c_str() + 5, this->begin());
    }

    bool contains(char c) const
    {
        for(auto &&l : *this)
        {
            if(l == c)
            {
                return true;
            }
        }
        return false;
    }

    void remove(char x)
    {
        for(auto &l : *this)
        {
            if(l == x)
            {
                l = '_';
                break;
            }
        }
    }
};

template <typename StreamT>
StreamT &operator<<(StreamT &s, const Word &w)
{
    for(auto && c : w)
    {
        s << c;
    }
    return s;
}


class WordList : public std::vector<Word>
{
public:
    WordList() {}
    WordList(const std::vector<Word> && wl) : std::vector<Word>(wl) {}

    bool contains(const Word &word) const
    {
        for(auto && w : *this)
        {
            if(w == word)
            {
                return true;
            }
        }
        return false;
    }
};

template <typename StreamT>
StreamT &operator<<(StreamT &s, const WordList &wl)
{
    s << "{ ";
    bool first = true;
    for(auto && w : wl)
    {
        if(first) first = false;
        else s << ", ";
        s << w;
    }
    s << " }";

    return s;
}


WordList readWords(const std::string &filename)
{
    std::ifstream file;
    file.open(filename);
    WordList words;
    while(file.good())
    {
        std::string s;
        std::getline(file, s);
        words.push_back(s);
    }

    return words;
}



class LetterColor
{
public:
    char m_letter;
    Color m_color;

    bool operator==(const LetterColor &o) const
    {
        return m_color == o.m_color;
    }
};

template <typename StreamT>
StreamT &operator<<(StreamT &s, const LetterColor &lc)
{
    switch(lc.m_color)
    {
        case Color::Gray: 
            s << "\u001b[37;1m\u001b[40;1m" << lc.m_letter << "\u001b[0m";
            break;
        case Color::Yellow: 
            s << "\u001b[30m\u001b[43;1m" << lc.m_letter << "\u001b[0m";
            break;
        case Color::Green: 
            s << "\u001b[30m\u001b[42;1m" << lc.m_letter << "\u001b[0m";
            break;
        default:
            s << lc.m_letter;
            break;
    }
    return s;
}


class MatchResults
{
public:
    std::array<LetterColor, 5> m_pos;

    MatchResults()
    {
        setGuess("_____");
        setColors("_____");
    }

    MatchResults(const char *word, const char *colors)
    {
        setGuess(word);
        setColors(colors);
    }

    Word guess() const
    {
        Word r;
        for(int i = 0; i < 5; i++)
        {
            r[i] = m_pos[i].m_letter;
        }
        return r;
    }

    void setGuess(const Word &w)
    {
        for(int i = 0; i < 5; i++)
        {
            m_pos[i].m_letter = w[i];
        }
    }
    
    void setColors(const char *colors)
    {
        for(int i = 0; i < 5; i++)
        {
            m_pos[i].m_color = Color::Gray;
            if(colors[i] == 'y') m_pos[i].m_color = Color::Yellow;
            if(colors[i] == 'g') m_pos[i].m_color = Color::Green;
        }
    }
    
    bool operator==(const MatchResults &o) const
    {
        for(size_t i = 0; i < m_pos.size(); i++)
        {
            if(!(m_pos[i] == o.m_pos[i]))
            {
                return false;
            }
        }
        return true;
    }
};


template <typename StreamT>
StreamT &operator<<(StreamT &s, const MatchResults &m)
{
    for(auto && p : m.m_pos)
    {
        s << p;
    }
    return s;
}

MatchResults calcMatchResults(const Word &solution, const Word &guess)
{
    auto results = MatchResults();
    Word solutionCopy = solution;
    Word guessCopy = guess;

    //copy in word
    results.setGuess(guess);

    //check for greens first
    for(size_t i = 0; i < 5; i++)
    {
        // std::cout << "i=" << i << ", solutionCopy=" << solutionCopy << ", guessCopy=" << guessCopy << ", results=" << results << std::endl;
        if(guessCopy[i] == solutionCopy[i])
        {
            results.m_pos[i].m_color = Color::Green;
            solutionCopy[i] = '_';
            guessCopy[i] = '*';
        }
        // std::cout << "i=" << i << ", solutionCopy=" << solutionCopy << ", guessCopy=" << guessCopy << ", results=" << results << std::endl;
        // std::cout << std::endl;
    }

    // std::cout << std::endl;

    //check for yellows
    for(size_t i = 0; i < 5; i++)
    {
        // std::cout << "i=" << i << ", solutionCopy=" << solutionCopy << ", guessCopy=" << guessCopy << ", results=" << results << std::endl;
        if(solutionCopy.contains(guessCopy[i]))
        {
            results.m_pos[i].m_color = Color::Yellow;
            solutionCopy.remove(guessCopy[i]);
            guessCopy[i] = '*';
        }
        // std::cout << "i=" << i << ", solutionCopy=" << solutionCopy << ", guessCopy=" << guessCopy << ", results=" << results << std::endl;
        // std::cout << std::endl;
    }

    return results;
}


WordList findSolutionsThatMatch(const MatchResults &matchResults, const WordList &possibleSolutions)
{
    WordList remainingSolutions;
    for(auto && testSolution : possibleSolutions)
    {
        auto testResults = calcMatchResults(testSolution, matchResults.guess());
        if(testResults == matchResults)
        {
            remainingSolutions.push_back(testSolution);
        }
    }
    return remainingSolutions;
}


bool solutionIsPossibleForMatchResults(const Word &solution, const MatchResults &matchResults)
{
    auto testResults = calcMatchResults(solution, matchResults.guess());
    return testResults == matchResults;
    // return matchResults == matchResults;
    //return true;
}


int countSolutionsThatMatch(const MatchResults &matchResults, const WordList &possibleSolutions)
{
    int cnt = 0;
    for(auto && testSolution : possibleSolutions)
    {
        if(solutionIsPossibleForMatchResults(testSolution, matchResults))
        {
            cnt++;
        }
    }
    return cnt;
}       


Word findBestGuessWord(const WordList &possibleSolutions, const WordList &wordsToTry, bool verbose=false)
{
    if(possibleSolutions.size() == 1)
    {
        return possibleSolutions[0];
    }

    double bestAvgRemainingSolutionCnt = std::numeric_limits<double>::max();
    Word bestGuess;
    for(auto && guess : wordsToTry)
    {
        int sumRemainingSolutionCnt = 0;
        int sumCnt = 0;
        int maxRemainingSolutionCnt = 0;
        for(auto && solution : possibleSolutions)
        {
            auto results = calcMatchResults(solution, guess);
            auto cnt = countSolutionsThatMatch(results, possibleSolutions);
            sumCnt += 1;
            sumRemainingSolutionCnt += cnt;
            maxRemainingSolutionCnt = std::max(maxRemainingSolutionCnt, cnt);
            // if(guess == "comet")
            // {
            //     std::cout << guess << ", " << solution << ", " << cnt << std::endl;
            // }
        }
        // double score = (double)sumRemainingSolutionCnt / (double)sumCnt;
        double score = maxRemainingSolutionCnt;
        if(score < bestAvgRemainingSolutionCnt)
        {
            //if count is low then require the guess be a solution
            //if(score > 1 || possibleSolutions.contains(guess))
            {
                bestAvgRemainingSolutionCnt = score;
                bestGuess = guess;
                // std::cout << "New best guess: " << bestGuess << ", " << bestAvgRemainingSolutionCnt << std::endl;
                // std::cout << "    score=" << score << ", sumRemainingSolutionCnt=" << sumRemainingSolutionCnt << ", sumCnt=" << sumCnt << std::endl;
            }
        }
        if(verbose)
        {
            std::cout << guess << ", " << score << ", " << bestGuess << ", " << bestAvgRemainingSolutionCnt << std::endl;
        }
    }
    return bestGuess;
}


WordList findBestFirst2GuessWords(const WordList &possibleSolutions, const WordList &wordsToTry, bool verbose=false)
{
    double bestAvgRemainingSolutionCnt = std::numeric_limits<double>::max();
    WordList bestGuesses;
    //for(auto && guess1 : wordsToTry)
    auto guess1 = Word("aesir");
    {
        for(auto && guess2 : wordsToTry)
        {
            int sumRemainingSolutionCnt = 0;
            int sumCnt = 0;
            int maxRemainingSolutionCnt = 0;
            for(auto && solution : possibleSolutions)
            {
                MatchResults results;
                results = calcMatchResults(solution, guess1);
                auto solutions1 = findSolutionsThatMatch(results, possibleSolutions);
                results = calcMatchResults(solution, guess2);
                auto cnt = countSolutionsThatMatch(results, solutions1);
                sumCnt += 1;
                sumRemainingSolutionCnt += cnt;
                maxRemainingSolutionCnt = std::max(maxRemainingSolutionCnt, cnt);
            }
            // double score = (double)sumRemainingSolutionCnt / (double)sumCnt;
            double score = maxRemainingSolutionCnt;
            if(score < bestAvgRemainingSolutionCnt)
            {
                //if count is low then require the guess be a solution
                //if(score > 5 || possibleSolutions.contains(guess))
                {
                    bestAvgRemainingSolutionCnt = score;
                    bestGuesses = WordList();
                    bestGuesses.push_back(guess1);
                    bestGuesses.push_back(guess2);
                    // std::cout << "New best guess: " << bestGuesses << ", " << bestAvgRemainingSolutionCnt << std::endl;
                    // std::cout << "    score=" << score << ", sumRemainingSolutionCnt=" << sumRemainingSolutionCnt << ", sumCnt=" << sumCnt << std::endl;
                }
            }
            if(verbose)
            {
                std::cout << guess1 << ", " << guess2 << ", " << score << ", " << bestGuesses << ", " << bestAvgRemainingSolutionCnt << std::endl;
            }
        }
    }
    return bestGuesses;
}


void assistant()
{
    // auto bestFirstWord = Word("roate");
    auto bestFirstWord = Word("aesir");
    auto words = readWords("words.txt");
    auto solutions = readWords("solutions.txt");
    std::cout << words.size() << " words, " << solutions.size() << " solutions" << std::endl;

    std::vector<MatchResults> guessesAndResults;

    //best first guess: soare?  roate?  aesir?

    std::cout << "Best first word is '" << bestFirstWord << "'" << std::endl;

    MatchResults mr;
    mr.setGuess(bestFirstWord);
    while(true)
    {
        std::string s;
        std::cout << "Enter guess: " << mr.guess() << std::endl;
        // std::cout << "Enter guess: ";
        // std::getline(std::cin, s);
        // mr.setGuess(s.c_str());

        std::cout << "Enter results (e.g. gbgyyb): ";
        std::getline(std::cin, s);
        mr.setColors(s.c_str());
        guessesAndResults.push_back(mr);

        auto possibleSolutions = solutions;
        auto wordsToTry = words;
        MatchResults matchResults;

        for(auto &matchResults : guessesAndResults)
        {
            possibleSolutions = findSolutionsThatMatch(matchResults, possibleSolutions);
            std::cout << "Guess: " << matchResults << " => " << possibleSolutions.size() << " solutions left";
            if(possibleSolutions.size() < 20)
            {
                std::cout << ": " << possibleSolutions;
            }
            std::cout << std::endl;
        }

        std::cout << "Checking " << wordsToTry.size() << " words for best guess word to reduce " << possibleSolutions.size() << " possible solutions" << std::endl;
        auto bestGuess = findBestGuessWord(possibleSolutions, wordsToTry);
        std::cout << "Best word to guess is '" << bestGuess << "'" << std::endl;
        mr.setGuess(bestGuess);
    }
}


void findBestFirstWord()
{
    auto words = readWords("words.txt");
    auto solutions = readWords("solutions.txt");
    std::cout << words.size() << " words, " << solutions.size() << " solutions" << std::endl;

    std::cout << "Checking " << words.size() << " words for best guess word to reduce " << solutions.size() << " possible solutions" << std::endl;
    auto bestGuess = findBestGuessWord(solutions, words, true);
    std::cout << "Best word to guess is " << bestGuess << std::endl;
}


void findBestFirst2Words()
{
    auto words = readWords("words.txt");
    auto solutions = readWords("solutions.txt");
    std::cout << words.size() << " words, " << solutions.size() << " solutions" << std::endl;

    std::cout << "Checking " << words.size() << " words for best guess word to reduce " << solutions.size() << " possible solutions" << std::endl;
    auto bestGuesses = findBestFirst2GuessWords(solutions, words, true);
    std::cout << "Best word to guesses are " << bestGuesses << std::endl;
}


void test()
{
    //std::cout << calcMatchResults("awful", "cabal") << std::endl;
    WordList solutions = std::vector<Word>{"batty", "catty", "patty", "fatty"};
    WordList words = std::vector<Word>{"batty", "catty", "patty", "fatty", "bcpfx"};
    auto guess = findBestGuessWord(solutions, words, true);
    std::cout << "guess=" << guess << std::endl;
}

int main(int argc, char **argv)
{
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help",                 "produce help message")
        ("assistant",            "run play assistant")
        ("test",                 "test")
        ("find-best-first-word", "find best first word")
        ("find-best-first-two-words", "find best first two words")
    ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    //po::store(po::command_line_parser(argc, argv). options(desc).positional(p).run(), vm);
    po::notify(vm);

    if (vm.count("help") || argc == 1) {
        std::cout << desc << std::endl;
        return 1;
    }

    if     (vm.count("assistant")) { assistant(); }
    else if(vm.count("test")) { test(); }
    else if(vm.count("find-best-first-word")) { findBestFirstWord(); }
    else if(vm.count("find-best-first-two-words")) { findBestFirst2Words(); }
    return 0;
}
