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

bets_per_trial = 1000
num_trials = 100000

total_balances = []
trial_final_balances = []
for trial in range(num_trials):
    balance = 0
    last_result = 1
    last_wager = 1
    largest_wager = 0
    i = 0
    # while True:
    balances = []
    largest_wagers = []
    losing_streaks = []
    losing_streak = 0
    for i in range(bets_per_trial):
        start_balance = balance

        if last_result == 0:
            wager = last_wager * 2
        else:
            wager = 1
        #if wager >= 128: wager = 128
    
        largest_wager = max(largest_wager, wager)
        
        balance -= wager    
        result = run_a_bet(wager)
    
        if result == 0:    
            losing_streak += 1
        else:
            losing_streak = 0
    
        balance += result
        last_wager = wager
        last_result = result
        balances.append(balance)
        largest_wagers.append(largest_wager)
        losing_streaks.append(losing_streak)
        total_balances.append(balance)
        #print(f"{i}: start_balance={start_balance}, balance={balance}, wager={wager}, result={result}, largest_wager={largest_wager}")
        # if i % 10000 == 0:
        #     print(balance)
    trial_final_balances.append(balance)
    trials.append({
        "balances": balances,
        "largest_wagers": largest_wagers,
        "losing_streaks": losing_streaks,
    })

print(np.array(trial_final_balances).mean() / bets_per_trial)

# %%
if 0:
    pass
    # %%
    r, c = 2, 1
    fig, axs = plt.subplots(r, c, figsize=(42, 25), sharex=True)
    if r == 1 and c == 1: axs = [axs]
    elif c == 1:  axs = list(axs)
    else:         axs = [axs[i, j] for j in range(c) for i in range(r)]  # flatten
    
    plt.sca(axs.pop(0))
    plt.title("Account Balance")
    for trial in trials:
        plt.plot(trial['balances'])
    #plt.plot(largest_wagers)
    
    plt.sca(axs.pop(0))
    plt.title("Losing Streak")
    for trial in trials:
        plt.plot(trial['losing_streaks'])
    plt.show()
    
    #plt.legend(loc=2)
    plt.show()


