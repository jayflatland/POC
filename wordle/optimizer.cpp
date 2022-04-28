
#include <fstream>
#include <iostream>
#include <array>
#include <vector>
#include <limits>

enum class Color : uint8_t
{
    Gray,
    Yellow,
    Green
};

class Word
{
public:
    std::array<char, 5> m_letters;

    Word()
    {
        std::fill(m_letters.begin(), m_letters.end(), '_');
    }

    Word(const char *s)
    {
        std::copy(s, s + 5, m_letters.begin());
    }

    Word(const std::string &s)
    {
        std::copy(s.c_str(), s.c_str() + 5, m_letters.begin());
    }

    char &operator[](int i)
    {
        return m_letters[i];
    }

    char operator[](int i) const
    {
        return m_letters[i];
    }

    bool contains(char c) const
    {
        for(auto &&l : m_letters)
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
        for(auto &l : m_letters)
        {
            if(l == x)
            {
                l = '_';
                break;
            }
        }
    }

    bool operator ==(const Word &o) const 
    {
        return m_letters == o.m_letters;
    }
};

template <typename StreamT>
StreamT &operator<<(StreamT &s, const Word &w)
{
    for(auto && c : w.m_letters)
    {
        s << c;
    }
    return s;
}


class WordList : public std::vector<Word>
{
public:
    //WordList(const )
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
            r.m_letters[i] = m_pos[i].m_letter;
        }
        return r;
    }

    void setGuess(const Word &w)
    {
        for(int i = 0; i < 5; i++)
        {
            m_pos[i].m_letter = w.m_letters[i];
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
            // solutionCopy[i] = '_';
            guessCopy[i] = '*';
        }
        // std::cout << "i=" << i << ", solutionCopy=" << solutionCopy << ", guessCopy=" << guessCopy << ", results=" << results << std::endl;
        // std::cout << std::endl;
    }

    return results;
}


WordList findSolutionsThatMatch(const Word &guess, const MatchResults &matchResults, const WordList &possibleSolutions)
{
    WordList remainingSolutions;
    for(auto && testSolution : possibleSolutions)
    {
        auto testResults = calcMatchResults(testSolution, guess);
        if(testResults == matchResults)
        {
            remainingSolutions.push_back(testSolution);
        }
    }
    return remainingSolutions;
}


int countSolutionsThatMatch(const Word &guess, const MatchResults &matchResults, const WordList &possibleSolutions)
{
    int cnt = 0;
    for(auto && testSolution : possibleSolutions)
    {
        auto testResults = calcMatchResults(testSolution, guess);
        if(testResults == matchResults)
        {
            cnt++;
        }
    }
    return cnt;
}       


Word findBestGuessWord(const WordList &possibleSolutions, const WordList &wordsToTry, bool verbose=false)
{
    double bestAvgRemainingSolutionCnt = std::numeric_limits<double>::max();
    Word bestGuess;
    for(auto && guess : wordsToTry)
    {
        int sumRemainingSolutionCnt = 0;
        int sumCnt = 0;
        for(auto && solution : possibleSolutions)
        {
            auto results = calcMatchResults(solution, guess);
            auto cnt = countSolutionsThatMatch(guess, results, possibleSolutions);
            sumCnt += 1;
            sumRemainingSolutionCnt += cnt;
            // if(guess == "comet")
            // {
            //     std::cout << guess << ", " << solution << ", " << cnt << std::endl;
            // }
        }
        double avgRemainingSolutionCnt = (double)sumRemainingSolutionCnt / (double)sumCnt;
        if(avgRemainingSolutionCnt < bestAvgRemainingSolutionCnt)
        {
            //if count is low then require the guess be a solution
            if(avgRemainingSolutionCnt > 5 || possibleSolutions.contains(guess))
            {
                bestAvgRemainingSolutionCnt = avgRemainingSolutionCnt;
                bestGuess = guess;
                // std::cout << "New best guess: " << bestGuess << ", " << bestAvgRemainingSolutionCnt << std::endl;
                // std::cout << "    avgRemainingSolutionCnt=" << avgRemainingSolutionCnt << ", sumRemainingSolutionCnt=" << sumRemainingSolutionCnt << ", sumCnt=" << sumCnt << std::endl;
            }
        }
        if(verbose)
        {
            std::cout << guess << ", " << avgRemainingSolutionCnt << ", " << bestGuess << ", " << bestAvgRemainingSolutionCnt << std::endl;
        }
    }
    return bestGuess;
}


WordList findBestFirst2GuessWords(const WordList &possibleSolutions, const WordList &wordsToTry, bool verbose=false)
{
    double bestAvgRemainingSolutionCnt = std::numeric_limits<double>::max();
    WordList bestGuesses;
    for(auto && guess1 : wordsToTry)
    {
        for(auto && guess2 : wordsToTry)
        {
            int sumRemainingSolutionCnt = 0;
            int sumCnt = 0;
            for(auto && solution : possibleSolutions)
            {
                MatchResults results;
                results = calcMatchResults(solution, guess1);
                auto solutions1 = findSolutionsThatMatch(guess1, results, possibleSolutions);
                results = calcMatchResults(solution, guess2);
                auto solutions2 = findSolutionsThatMatch(guess2, results, possibleSolutions);
                sumCnt += 1;
                sumRemainingSolutionCnt += solutions2.size();
            }
            double avgRemainingSolutionCnt = (double)sumRemainingSolutionCnt / (double)sumCnt;
            if(avgRemainingSolutionCnt < bestAvgRemainingSolutionCnt)
            {
                //if count is low then require the guess be a solution
                //if(avgRemainingSolutionCnt > 5 || possibleSolutions.contains(guess))
                {
                    bestAvgRemainingSolutionCnt = avgRemainingSolutionCnt;
                    bestGuesses = WordList();
                    bestGuesses.push_back(guess1);
                    bestGuesses.push_back(guess2);
                    // std::cout << "New best guess: " << bestGuesses << ", " << bestAvgRemainingSolutionCnt << std::endl;
                    // std::cout << "    avgRemainingSolutionCnt=" << avgRemainingSolutionCnt << ", sumRemainingSolutionCnt=" << sumRemainingSolutionCnt << ", sumCnt=" << sumCnt << std::endl;
                }
            }
            if(verbose)
            {
                std::cout << guess1 << ", " << guess2 << ", " << avgRemainingSolutionCnt << ", " << bestGuesses << ", " << bestAvgRemainingSolutionCnt << std::endl;
            }
        }
    }
    return bestGuesses;
}


void assistant()
{
    auto words = readWords("words.txt");
    auto solutions = readWords("solutions.txt");
    std::cout << words.size() << " words, " << solutions.size() << " solutions" << std::endl;

    std::vector<MatchResults> guessesAndResults;

    // std::cout << calcMatchResults("rotor", "soare") << std::endl;
    // return 0;
    //best first guess: soare?  roate?

    std::cout << "Best first word is 'roate'" << std::endl;

    MatchResults mr;
    mr.setGuess("roate");
    while(true)
    {
        std::string s;
        std::cout << "Enter guess: " << mr.guess() << std::endl;
        // std::cout << "Enter guess: ";
        // std::getline(std::cin, s);
        // mr.setGuess(s.c_str());

        std::cout << "Enter results (e.g. g_gyy_): ";
        std::getline(std::cin, s);
        mr.setColors(s.c_str());
        guessesAndResults.push_back(mr);

        auto possibleSolutions = solutions;
        auto wordsToTry = words;
        MatchResults matchResults;

        for(auto &matchResults : guessesAndResults)
        {
            auto guess = matchResults.guess();
            possibleSolutions = findSolutionsThatMatch(guess, matchResults, possibleSolutions);
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

int main()
{
    // assistant();
    // findBestFirstWord();
    findBestFirst2Words();
    return 0;
}
