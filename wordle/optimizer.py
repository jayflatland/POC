import numpy as np
import pandas as pd
from pandas import Timestamp, Timedelta
import matplotlib.pylab as plt
import seaborn as sns
import random

from termcolor import colored

sns.set_style("whitegrid")

"""
TODO
[ ] function to take a guess and a solution and return the hit/close/miss lists
[ ] function to take hit/close/miss list and return remaining possibilities
[ ] function to add hit/close/miss lists
"""
# %%

df = pd.read_csv("words.csv")

class LetterColor:
    def __init__(self):
        self.letter = " "
        self.color = "gray"
    def __repr__(self):
        if self.color == "gray":
            return "\u001b[37;1m\u001b[40;1m" + self.letter + "\u001b[0m"
        elif self.color == "yellow":
            return "\u001b[30m\u001b[43;1m" + self.letter + "\u001b[0m"
        elif self.color == "green":
            return "\u001b[30m\u001b[42;1m" + self.letter + "\u001b[0m"
        return self.letter
    def __eq__(self, o):
        return self.color == o.color


class MatchResults:
    def __init__(self):
        self.pos = [
            LetterColor(),
            LetterColor(),
            LetterColor(),
            LetterColor(),
            LetterColor() ]

    def __repr__(self):
        s = ""
        for lc in self.pos:
            s += str(lc)
        return s
    
    def __eq__(self, o):
        for i in range(5):
            if self.pos[i] != o.pos[i]:
                return False
        return True
        
# give a match result for the given guess against the given solution
def calcMatchResults(solution, guess):
    assert solution.islower()
    assert guess.islower()
    assert len(solution) == 5
    assert len(guess) == 5

    results = MatchResults()
    for i in range(5):
        solutionLetter = solution[i]
        guessLetter = guess[i]
        results.pos[i].letter = guessLetter
        if guessLetter == solutionLetter:
            results.pos[i].color = "green"
        elif guessLetter in solution:
            results.pos[i].color = "yellow"
            solution = solution.replace(guessLetter, "_", 1)

    return results


# check if the given guess fits in the ....??
# def doesGuessFitMatchResults(solution, guess, matchResults):
#     testResults = calcMatchResults(solution=solution, guess=guess)
#     return testResults == matchResults

def findSolutionsThatMatch(guess, matchResults, priorSolutions):
    remainingSolutions = []
    for testSolution in priorSolutions:
        testResults = calcMatchResults(solution=testSolution, guess=guess)
        if testResults == matchResults:
            remainingSolutions.append(testSolution)
    return remainingSolutions
        

def countSolutionsThatMatch(guess, matchResults, priorSolutions):
    cnt = 0
    for testSolution in priorSolutions:
        testResults = calcMatchResults(solution=testSolution, guess=guess)
        if testResults == matchResults:
            cnt += 1
    return cnt
        
    
words = list(df.word)
solutions = list(df[df.isSolution == 1].word)

def randomTestPlay():
    remainingSolutions = solutions
    solution = random.choice(solutions)
    print(f"Solution is {solution}")
    
    for i in range(6):
        guess = random.choice(words)
        print(f"Guess is {guess}")
        
        matchResults = calcMatchResults(solution=solution, guess=guess)
        print(f"Result is {matchResults}")
        
        remainingSolutions = findSolutionsThatMatch(guess=guess, matchResults=matchResults, priorSolutions=remainingSolutions)
        print(f"Remaining solutions are ({len(remainingSolutions)}): {remainingSolutions}")

#randomTestPlay()

def findBestGuessWord(priorSolutions, wordsToTry):
    guessesAndScores = []
    for guess in wordsToTry:
        sumRemainingSolutionCnt = 0
        sumCnt = 0
        for solution in priorSolutions:
            results = calcMatchResults(solution=solution, guess=guess)
            cnt = countSolutionsThatMatch(guess=guess, matchResults=results, priorSolutions=wordsToTry)
            sumCnt += 1
            sumRemainingSolutionCnt += cnt
            #print(guess, solution, cnt)
        avgRemainingSolutionCnt = sumRemainingSolutionCnt / sumCnt
        print(f"{guess},{avgRemainingSolutionCnt:.0f}")
        guessesAndScores.append((guess, avgRemainingSolutionCnt))
    return guessesAndScores

guessesAndResults = [
    ("acres", "xaxxx"),
    #("guilt", "_u___"),
    #("dumpy", "_u__y"),
]

possibleSolutions = solutions
wordsToTry = words

for guess, solution in guessesAndResults:
    matchResults = calcMatchResults(solution=solution, guess=guess)
    possibleSolutions = findSolutionsThatMatch(guess=guess, matchResults=matchResults, priorSolutions=possibleSolutions)
    print(f"Guess: {matchResults} => {len(possibleSolutions)} solutions left")

print(f"Checking {len(wordsToTry)} possible guess words for max reduction of {len(possibleSolutions)} possible solutions")
guessesAndScores = findBestGuessWord(priorSolutions=possibleSolutions, wordsToTry=wordsToTry)
guessesAndScores.sort(key=lambda a : a[1])
for g, s in guessesAndScores[:10]:
    print(g, s)
