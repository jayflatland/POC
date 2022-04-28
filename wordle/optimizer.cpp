
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

    Word() {}
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

    void replace(char x, char y, int maxcnt)
    {
        for(auto &l : m_letters)
        {
            if(l == x && maxcnt > 0)
            {
                maxcnt--;
                l = y;
            }
        }
    }
};


class WordList : public std::vector<Word>
{
public:
    //WordList(const )
};



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


template <typename StreamT>
StreamT &operator<<(StreamT &s, const Word &w)
{
    for(auto && c : w.m_letters)
    {
        s << c;
    }
    return s;
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

    for(size_t i = 0; i < 5; i++)
    {
        auto solutionLetter = solutionCopy[i];
        auto guessLetter = guess[i];
        results.m_pos[i].m_letter = guessLetter;
        if(guessLetter == solutionLetter)
        {
            results.m_pos[i].m_color = Color::Green;
        }
        else if(solutionCopy.contains(guessLetter))
        {
            results.m_pos[i].m_color = Color::Yellow;
            solutionCopy.replace(guessLetter, '_', 1);
        }
        else
        {
            results.m_pos[i].m_color = Color::Gray;
        }
    }

    return results;
}


WordList findSolutionsThatMatch(const Word &guess, const MatchResults &matchResults, const WordList &priorSolutions)
{
    WordList remainingSolutions;
    for(auto && testSolution : priorSolutions)
    {
        auto testResults = calcMatchResults(testSolution, guess);
        if(testResults == matchResults)
        {
            remainingSolutions.push_back(testSolution);
        }
    }
    return remainingSolutions;
}


int countSolutionsThatMatch(const Word &guess, const MatchResults &matchResults, const WordList &priorSolutions)
{
    int cnt = 0;
    for(auto && testSolution : priorSolutions)
    {
        auto testResults = calcMatchResults(testSolution, guess);
        if(testResults == matchResults)
        {
            cnt++;
        }
    }
    return cnt;
}       


struct WordAndScore
{
    Word word;
    double score;
};

struct WordAndScoreList : public std::vector<WordAndScore>
{
};

WordAndScoreList findBestGuessWord(const WordList &priorSolutions, const WordList &wordsToTry)
{
    WordAndScoreList guessesAndScores;
    double bestAvgRemainingSolutionCnt = std::numeric_limits<double>::max();
    Word bestGuess;
    for(auto && guess : wordsToTry)
    {
        int sumRemainingSolutionCnt = 0;
        int sumCnt = 0;
        for(auto && solution : priorSolutions)
        {
            auto results = calcMatchResults(solution, guess);
            auto cnt = countSolutionsThatMatch(guess, results, wordsToTry);
            sumCnt += 1;
            sumRemainingSolutionCnt += cnt;
            // std::cout << guess << ", " << solution << ", " << cnt << std::endl;
        }
        double avgRemainingSolutionCnt = sumRemainingSolutionCnt / sumCnt;
        if(avgRemainingSolutionCnt < bestAvgRemainingSolutionCnt)
        {
            bestAvgRemainingSolutionCnt = avgRemainingSolutionCnt;
            bestGuess = guess;
        }
        std::cout << guess << ", " << avgRemainingSolutionCnt << ", " << bestGuess << ", " << bestAvgRemainingSolutionCnt << std::endl;
        guessesAndScores.push_back({guess, avgRemainingSolutionCnt});
    }
    return guessesAndScores;
}

struct GuessesAndResults
{
    Word guess;
    Word result;
};


struct GuessesAndResultsList : public std::vector<GuessesAndResults>
{
};




int main()
{
    auto words = readWords("words.txt");
    auto solutions = readWords("solutions.txt");
    std::cout << words.size() << " words, " << solutions.size() << " solutions" << std::endl;

    GuessesAndResultsList guessesAndResults;
    guessesAndResults.push_back({"acres", "xaxxx"});

    auto possibleSolutions = solutions;
    auto wordsToTry = words;


    for(auto &it : guessesAndResults)
    {
        auto guess = it.guess;
        auto solution = it.result;
        auto matchResults = calcMatchResults(solution, guess);
        possibleSolutions = findSolutionsThatMatch(guess, matchResults, possibleSolutions);
        std::cout << "Guess: " << matchResults << " => " << possibleSolutions.size() << " solutions left" << std::endl;
    }

    std::cout << "Checking " << wordsToTry.size() << " possible guess words for max reduction of " << possibleSolutions.size() << " possible solutions" << std::endl;
    auto guessesAndScores = findBestGuessWord(possibleSolutions, wordsToTry);
    //guessesAndScores.sort(key=lambda a : a[1])
    for(int i = 0; i < 10; i++)
    {
        std::cout << guessesAndScores[i].word << ", " << guessesAndScores[i].score << std::endl;
    }


    // auto m = calcMatchResults("beers", "tacos");
    // std::cout << m << std::endl;
    // std::cout << m << std::endl;
    return 0;
}
