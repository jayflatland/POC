import numpy as np
import pandas as pd
import matplotlib.pylab as plt
import seaborn as sns

sns.set_style("whitegrid")

# %%

#from http://www.astronexus.com/hyg

df = pd.read_csv("/home/jay/Downloads/hygdata_v3.csv")

df = df[df['proper'].notnull()]

plt.scatter(df['ra'], df['dec'], marker='.')
