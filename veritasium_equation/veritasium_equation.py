import numpy as np
import pandas as pd
import matplotlib.pylab as plt
import seaborn as sns

sns.set_style("whitegrid")

# %%


r = 3.0

x = 0.5
x_n = []
for i in range(10000):
    x_n.append(x)
    x = r * x * (1-x)

# plt.figure()
# plt.plot(x_n)
# plt.show()

sns.histplot(x_n[:500])
