
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

    MatchResults() {}
    MatchResults(const char *word, const char *colors)
    {
        for(int i = 0; i < 5; i++)
        {
            m_pos[i].m_letter = word[i];
            m_pos[i].m_color = Color::Gray;
            if(colors[i] == 'y') m_pos[i].m_color = Color::Yellow;
            if(colors[i] == 'g') m_pos[i].m_color = Color::Green;
        }
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


struct WordAndScore
{
    Word word;
    double score;
};

struct WordAndScoreList : public std::vector<WordAndScore>
{
};

Word findBestGuessWord(const WordList &priorSolutions, const WordList &wordsToTry)
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
            auto cnt = countSolutionsThatMatch(guess, results, priorSolutions);
            sumCnt += 1;
            sumRemainingSolutionCnt += cnt;
            // if(guess == "comet")
            // {
            //     std::cout << guess << ", " << solution << ", " << cnt << std::endl;
            // }
        }
        double avgRemainingSolutionCnt = sumRemainingSolutionCnt / sumCnt;
        if(avgRemainingSolutionCnt < bestAvgRemainingSolutionCnt)
        {
            //if count is low then require the guess be a solution
            if(avgRemainingSolutionCnt > 5 || priorSolutions.contains(guess))
            {
                bestAvgRemainingSolutionCnt = avgRemainingSolutionCnt;
                bestGuess = guess;
                // std::cout << "New best guess: " << bestGuess << ", " << bestAvgRemainingSolutionCnt << std::endl;
                // std::cout << "    avgRemainingSolutionCnt=" << avgRemainingSolutionCnt << ", sumRemainingSolutionCnt=" << sumRemainingSolutionCnt << ", sumCnt=" << sumCnt << std::endl;
            }
        }
        // std::cout << guess << ", " << avgRemainingSolutionCnt << ", " << bestGuess << ", " << bestAvgRemainingSolutionCnt << std::endl;
        guessesAndScores.push_back({guess, avgRemainingSolutionCnt});
    }
    return bestGuess;
}


// void test()
// {
//     auto words = readWords("words.txt");
//     auto solutions = readWords("solutions.txt");
//     std::cout << words.size() << " words, " << solutions.size() << " solutions" << std::endl;

//     auto results = MatchResults{"honey", "ggggg"};
//     auto guess = results.guess();
//     auto &wordsToTry = words;

//     auto cnt = countSolutionsThatMatch(guess, results, wordsToTry);
//     auto possibleSolutions = findSolutionsThatMatch(guess, results, solutions);
//     std::cout << "Remaining solutions: (" << cnt << "): " << possibleSolutions << std::endl;
//     auto bestGuess = findBestGuessWord(possibleSolutions, wordsToTry);
//     std::cout << "Best word to guess is " << bestGuess << std::endl;
// }


void solveHelper()
{
    auto words = readWords("words.txt");
    auto solutions = readWords("solutions.txt");
    std::cout << words.size() << " words, " << solutions.size() << " solutions" << std::endl;

    //BEST FIRST WORD IS soare
    std::vector<MatchResults> guessesAndResults = {
        MatchResults{"soare", " y yg"},
        MatchResults{"prove", " gg g"},
        // MatchResults{"bowel", " g gg"},
    };

    auto possibleSolutions = solutions;
    auto wordsToTry = words;
    MatchResults matchResults;

    for(auto &matchResults : guessesAndResults)
    {
        auto guess = matchResults.guess();
        possibleSolutions = findSolutionsThatMatch(guess, matchResults, possibleSolutions);
        std::cout << "Guess: " << matchResults << " => " << possibleSolutions.size() << " solutions left" << std::endl;
    }
    std::cout << "Remaining solutions: (" << possibleSolutions.size() << "): " << possibleSolutions << std::endl;

    std::cout << "Checking " << wordsToTry.size() << " possible guess words for max reduction of " << possibleSolutions.size() << " possible solutions" << std::endl;
    auto bestGuess = findBestGuessWord(possibleSolutions, wordsToTry);
    std::cout << "Best word to guess is " << bestGuess << std::endl;

    // possibleSolutions = findSolutionsThatMatch(bestGuess, matchResults, wordsToTry);
    // std::cout << "Remaining solutions: (" << possibleSolutions.size() << "): " << possibleSolutions << std::endl;
}


int main()
{
    solveHelper();
    // test();
    return 0;
}
