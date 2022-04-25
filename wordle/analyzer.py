import numpy as np
import pandas as pd
from pandas import Timestamp, Timedelta
import matplotlib.pylab as plt
import seaborn as sns

sns.set_style("whitegrid")


# %%

df = pd.read_csv("words.csv")
df = df[df.isSolution == 1]

notLetters = "caeprud"

notSpots = [
    (3, 'l'),
]

isSpots = [
    (0, 's'),
    (2, 'o'),
]

for c in notLetters:
    df = df[~df.word.str.contains(c)]

for i, c in notSpots:
    df = df[df.word.str.contains(c)]
    df = df[df.word.str[i] != c]

for i, c in isSpots:
    df = df[df.word.str[i] == c]

print(df)