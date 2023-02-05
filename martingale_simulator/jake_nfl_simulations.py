import pandas as pd
import numpy as np
import matplotlib.pylab as plt
import seaborn as sns

#%%

# https://www.kaggle.com/datasets/maxhorowitz/nflplaybyplay2009to2016?resource=download
#data_dir = 'C:/Users/jake/Projects/SportsBetting/Data/Kaggle/archive/'
data_dir = '~/Downloads/NFL/'
file_name = 'NFL Play by Play 2009-2018 (v5).csv'

print('reading data..\n')
df = pd.read_csv(data_dir + file_name)
df['always1'] = 1
if 0:
    df_ = df.copy()
    df = df_.copy()
#%%

if 0:
    # investigating
    len(df)
    dfj = df.head(1000).copy()
    print('Num plays: ', len(df))
    print('Num games: ', len(df.game_id.unique()))
    df.groupby(by='game_id')['always1'].sum().plot(kind='line', title='num events per game')
    plt.figure(); sns.distplot(df.groupby(by='game_id')['always1'].sum()); plt.title('num events per game')
    plt.figure(); sns.distplot(df.groupby(by='game_id')['drive'].max()); plt.title('num drives per game')
    df.groupby(by='game_id')['drive'].max().value_counts().sort_index()
    punt_cols = [i for i in df.columns if 'punt' in i]
    if 0:
        [i for i in df.columns if 'touch' in i]
    punt_cols = ['punt_attempt', 'field_goal_attempt', 'touchdown', 'interception']
    care_cols = ['posteam','drive'] + punt_cols
    col_order = care_cols + [i for i in df.columns if i not in care_cols]
    dfj = df[col_order].head(1000).copy()
    df['game_drive'] = df['game_id'].astype(str) + '_d' + df['drive'].astype(str).str.zfill(3) # needed to sort the drives correctly
    dfg = df.groupby(by='game_drive')['punt_attempt'].sum()
    dfj = df[df['game_drive']=='2010091208_d16'][col_order].copy() # look at one with 2 punts, from a penalty ok
    dfg = df.groupby(by='game_drive')[['punt_attempt']].sum()
    df = df.join(df.groupby(by='game_drive')['punt_attempt'].sum(), on='game_drive', rsuffix='_g')
    df.rename(columns={'game_drive_g':'punts_attempted_on_this_drive'}, inplace=True)
    
    df.groupby(by='game_id')['punt_attempt'].sum()
    df.groupby(by='game_id')['punt_attempt'].sum().value_counts().sort_index()
    plt.hist(df.groupby(by='game_id')['punt_attempt'].sum()); plt.title('hist of sum punt_attempt per game')
    sns.distplot(df.groupby(by='game_id')['punt_attempt'].sum())

    # other explorations cause curious
    # plays per half? expecting slighly more second half for tricky end of game stuff
    df['date'] = pd.to_datetime(df['game_date'])
    df['dayofyear'] = df['date'].dt.dayofyear
    
    df.pivot_table(index='date', columns='game_half', values='always1', aggfunc=np.sum).cumsum().plot()
    # plays throughout the game?
#    df.pivot_table(index='game_seconds_remaining', columns='date', values='always1', aggfunc=np.sum).cumsum().plot()
    df.groupby(by='game_seconds_remaining')['always1'].sum().cumsum().plot()
    df.groupby(by='game_seconds_remaining')['punt_attempt'].sum().sort_index(ascending=False).cumsum().plot(title='cumulative punts by game_seconds_remaining').invert_xaxis()
    
    df.pivot_table(index='date', columns='posteam', values='punt_attempt', aggfunc=np.sum).cumsum().ffill().plot(title='cumulative punt_attempt by posteam over date')
    df.pivot_table(index='dayofyear', columns='posteam', values='punt_attempt', aggfunc=np.sum).cumsum().ffill().plot(title='cumulative punt_attempt by posteam over dayofyear')
    
    
    
#%%

def cumulative_drives_without_punt_attempt(df):
    punt_attempt_vec = df['punt_attempt'].values
    value_vec = np.array([np.nan]*len(df))
    value = 0
    for i in range(len(df)):
        punt_attempt = punt_attempt_vec[i]
        if punt_attempt > 0.5:
            value = 0
        else:
            value += 1
        value_vec[i] = value
    df['cum_no_punts_attempted'] = pd.Series(value_vec)
    return df


def run_marty(df,
              initial_balance=1000000.0,
              initial_bet = 500.0,
              max_bet = 250000.0, # TODO()
              allow_bankrupt = True, # TODO()
              allow_max_bet_breach = True, # TODO()
              reset_bidding_if_this_bet_exceeds_max_bet = False,
              reset_bidding_on_each_game = False,
              ):
    # run a martingale strategy
    # doing this row by row so can add bells and whistles later
    num_flips = len(df)
    punt_attempt_vec = df['punt_attempt'].values
    game_id_vec = df['game_id'].values
    
    len_df = len(df)
    i_vec = np.array([np.nan]*len_df)
    this_bet_vec = np.array([np.nan]*len_df)
    win_vec = np.array([np.nan]*len_df)
    winnings_vec = np.array([np.nan]*len_df)
    balance_pre_vec = np.array([np.nan]*len_df)
    balance_post_vec = np.array([np.nan]*len_df)
    just_lost_vec = np.array([np.nan]*len_df)
    reset_betting_vec = np.array([np.nan]*len_df)
    
#    initial_balance = 1000000.0
    balance = initial_balance
#    initial_bet = 5.0
    
    just_lost = False
    this_bet = initial_bet
    this_game_id = game_id_vec[0]
    prev_game_id = this_game_id
    for i in range(num_flips):
        this_game_id = game_id_vec[i]
        
        if just_lost:
            this_bet = this_bet * 2
        else:
            this_bet = initial_bet
        
        reset_betting = 0
        if reset_bidding_if_this_bet_exceeds_max_bet:
            if this_bet > max_bet:
                print(f'this_bet > max_bet: {this_bet} > {max_bet}, resetting bidding!')
                this_bet = initial_bet
                reset_betting = 1
        if reset_bidding_on_each_game:
            if this_game_id != prev_game_id:
                #print(f'new game_id, resetting bidding!')
                this_bet = initial_bet
                reset_betting = 1
            
        # pay to bet
        balance -= this_bet
        balance_pre = balance
#        prob = np.random.uniform()
#        win = 1 if prob < win_pct else 0
        win = punt_attempt_vec[i]
        
        winnings = 0
        if win:
            just_lost = False
            # get paid
            winnings = this_bet * 2
            balance += winnings
        else:
            just_lost = True
        balance_post = balance
        
        # save stuff
        prev_game_id = this_game_id
        
        i_vec[i] = i
        this_bet_vec[i] = this_bet
        win_vec[i] = win
        winnings_vec[i] = winnings
        balance_pre_vec[i] = balance_pre
        balance_post_vec[i] = balance_post
        just_lost_vec[i] = just_lost
        reset_betting_vec[i] = reset_betting
    
    df['i'] = pd.Series(i_vec)
    df['this_bet'] = pd.Series(this_bet_vec)
    df['win'] = pd.Series(win_vec)
    df['winnings'] = pd.Series(winnings_vec)
    df['balance_pre'] = pd.Series(balance_pre_vec)
    df['balance_post'] = pd.Series(balance_post_vec)
    df['just_lost'] = pd.Series(just_lost_vec)
    df['reset_betting'] = pd.Series(reset_betting_vec)
    
    return df

        

#%%

# set up stuff with each row as a unique drive
print('setup data..\n')
df['game_drive'] = df['game_id'].astype(str) + '_d' + df['drive'].astype(str).str.zfill(3)
dfd = df.groupby(by='game_drive')[['punt_attempt']].sum()
dfd['game_drive'] = dfd.index
dfd = dfd.reset_index(drop=True)
dfd['game_id'] = dfd['game_drive'].str.split('_d').str[0]

dfd['no_punt_attempt'] = np.where(dfd['punt_attempt'] < 0.5, 1, 0)
dfd = cumulative_drives_without_punt_attempt(dfd)

# marty time
print('marty time..\n')
initial_balance=1000000.0
initial_bet = 100.0
dfd = run_marty(dfd,
               initial_balance = initial_balance,
               initial_bet = initial_bet,
               reset_bidding_if_this_bet_exceeds_max_bet = False,
               reset_bidding_on_each_game = False)
if 0: 
    dfd['cum_no_punts_attempted'].plot(title='cum_no_punts_attempted (amount of times bet doubles)')
##%%
    
# plot stuff
print('plot stuff..\n')
r, c = 2, 2
fig, axs = plt.subplots(r, c, figsize=(9, 9), sharex=True)
if r == 1 and c == 1: axs = [axs]
elif r == 1 or c == 1:  axs = list(axs)
else: axs = [axs[i, j] for j in range(c) for i in range(r)]  # flatten

plt.suptitle(f'initial_balance:{initial_balance}, initial_bet:{initial_bet} (even payout/loss)')

plt.sca(axs.pop(0))
plt.plot(dfd['balance_post'])
plt.title(f'bank account balance')

plt.sca(axs.pop(0))
plt.plot(dfd['this_bet'])
plt.title(f'this_bet')

plt.sca(axs.pop(0))
plt.plot(dfd['punt_attempt'].ewm(alpha=0.005, min_periods=50).mean())
plt.title(f'ewma(punt_attempt, alpha0.005)')

plt.sca(axs.pop(0))
plt.plot(dfd['cum_no_punts_attempted'])
plt.title(f'cum_no_punts_attempted (amount of times bet doubles)')

                           
#%%
notes = """
this rolls over from game to game (basically acting like all the drives played in a row are one big game)
cases where you'd definitely do better than this:
    -super short posessions at the end of the half (or end of the game) are counted as their own drive -- kindof fuzzy to define this so I left it for now
    -short drives (field position is in here)
    -keep track of injuries
    -keep track of the matchup of the offense vs defence
    -etc
"""

#%%
questions = """
In the actual bet, what qualifies as a punt? Formation, or a kick? Block count?
In the bet, what are the odds/payout/fees/required size/specifics of that
"""


#%%




















