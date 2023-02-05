import numpy as np
import pandas as pd
from pandas import Timestamp, Timedelta
import matplotlib.pylab as plt
import seaborn as sns
import random

sns.set_style("whitegrid")


# %%

def run_a_bet(wager):
    return random.choice([0, 2*wager])

trials = []

bets_per_trial = 10000
num_trials = 20
max_wager = 1e999#4096

results = []
for trial in range(num_trials):
    balance = 0
    last_result = 1
    last_wager = 1
    largest_wager = 0
    losing_streak = 0
    for i in range(bets_per_trial):
        start_balance = balance

        if last_result == 0:
            wager = last_wager * 2
        else:
            wager = 1
        wager = min(wager, max_wager)

        largest_wager = max(largest_wager, wager)
        
        balance -= wager    
        outcome = run_a_bet(wager)
    
        if outcome == 0:    
            losing_streak += 1
        else:
            losing_streak = 0
    
        balance += outcome
        last_wager = wager
        last_result = outcome
        results.append({
            "trial": trial,
            "i": i,
            "balance": balance,
            "starting_balance": start_balance,
            "balance_change": balance - start_balance,
            "wager": wager,
            "losing_streak": losing_streak,
        })

df = pd.DataFrame(results)

#df['balance_change'].mean()

# %%
if 0:
    pass
    # %%
    r, c = 2, 1
    fig, axs = plt.subplots(r, c, figsize=(14, 7), sharex=True)
    if r == 1 and c == 1: axs = [axs]
    elif c == 1:  axs = list(axs)
    else:         axs = [axs[i, j] for j in range(c) for i in range(r)]  # flatten
    
    plt.sca(axs.pop(0))
    plt.title("Account Balance")
    for trial, gb in df.groupby('trial'):
        plt.plot(gb['i'], gb['balance'])
    #plt.plot(largest_wagers)
    
    plt.sca(axs.pop(0))
    plt.title("Losing Streak")
    for trial, gb in df.groupby('trial'):
        plt.plot(gb['i'], gb['losing_streak'])
    plt.show()
    
    #plt.legend(loc=2)
    plt.show()


