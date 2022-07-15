#!/usr/bin/env python

###############################################################################
# Idea:
# 
# Neural nets are very good at memorizing.  Without sufficiently diverse 
# training data, a neural net will do just that.  What makes neural nets truly
# interesting is their ability to infer model structure from data, if
# sufficient data is  available.
# 
# Binary addition, beyond a few digits, provides a useful model whereby we can
# easily produce copious amounts of training data for a non-trivial model.
# This can potentially be a useful gauge of the capapility of neural nets.
#
# RESULTS:
# Terrible!  Doesn't converge.  Can't find any structure that does well.
# Feed forward networks can only seem to memorize.
# 
# Online searches suggest RNN is ideal.  Interesting.  This is a computation
# problem, not a recognition problem.
#
###############################################################################

import torch
import numpy as np
import math
import random
import numba

# %%
N = 100000
BW = 10

@numba.jit
def binvec(a, bw):
    v = np.zeros(bw)
    for i in range(bw):
        #print(a, (1<<i))
        if (a & (1<<i)) != 0:
            #print(a, (1<<i), " IS TRUE")
            v[bw-1-i] = 1.0
        #else:
        #    v[bw-1-i] = -1.0
    return v

@numba.jit
def binvecAB(a, b):
    return binvec((a<<BW) + b, BW * 2)

@numba.jit
def binvecC(c):
    return binvec(c, BW + 1)

@numba.jit
def genData():
    # N = 256
    x = np.zeros((N, BW * 2))
    y = np.zeros((N, BW + 1))
    for i in range(N):
        a = random.randint(0, (1<<BW)-1)
        b = random.randint(0, (1<<BW)-1)
    # i = 0
    # for a in range(1<<BW):
    #   for b in range(1<<BW):
        c = a + b
        #print(a, b, c)
        x[i] = binvecAB(a, b)
        y[i] = binvecC(c)
        # print(a, b, c, x[i], y[i])
        i += 1
        #print()
    return x, y

x, y = genData()

x = torch.tensor(x).to(torch.float)
y = torch.tensor(y).to(torch.float)

# %%
MID_D = 128
model = torch.nn.Sequential(
    torch.nn.Linear(BW * 2, MID_D), torch.nn.ReLU(),
    torch.nn.Linear(MID_D, MID_D), torch.nn.ReLU(),
    torch.nn.Linear(MID_D, BW + 1),
    #torch.nn.Softmax(BW+1),
    #torch.nn.Tanh(),
    #torch.nn.ReLU(),
    torch.nn.Sigmoid(),
)

#model = torch.nn.RNN(BW * 2, BW + 1)
#model(x)

# %%
loss_fn = torch.nn.MSELoss(reduction='sum')
optimizer = torch.optim.RMSprop(model.parameters(), lr=1e-3)
#optimizer = torch.optim.Adam(model.parameters(), lr=1e-3)

t = 0
# %%
while True:
    t += 1
    y_pred = model(x)
    loss = loss_fn(y_pred, y)
    if 1:#t % 100 == 0:
        print(t, loss.item())
    optimizer.zero_grad()
    loss.backward()
    optimizer.step()

# %%
def nnAdd(a, b):
    # print(a, b, a+b)
    v = binvecAB(a, b)
    v = torch.tensor(v).to(torch.float)
    out = model(v.unsqueeze(0))
    out = out.squeeze(0)
    # print(v)
    out = torch.where(out > 0.5, 1, 0)
    # print(out)
    # print("    ", out)
    #print()
    #return out
    r = 0
    for i in range(BW+1):
        if out[BW-i] > 0.0:
            r += (1<<i)
    return r

for i in range(1000):
    a = random.randint(0, (1<<BW)-1)
    b = random.randint(0, (1<<BW)-1)
    c = a + b
    c_est = nnAdd(a, b)
    if c != c_est:
        print(f"a={a:3d}, b={b:3d}, c={c:3d}, c_est={c_est:3d}, correct={c == c_est}")
    # print()

# %%
# ye = torch.where(model(x) > 0.5, 1.0, 0.0)
# y == ye
# x[-3]
# y[-3]
# ye[-3]
