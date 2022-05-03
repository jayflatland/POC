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
BW = 8

@numba.jit
def binvec(a, bw):
    v = np.zeros(bw)
    for i in range(bw):
        #print(a, (1<<i))
        if (a & (1<<i)) != 0:
            #print(a, (1<<i), " IS TRUE")
            v[bw-1-i] = 1.0
    return v

@numba.jit
def binvecAB(a, b):
    return binvec((a<<BW) + b, BW * 2)

@numba.jit
def binvecC(c):
    return binvec(c, BW + 1)

@numba.jit
def gen(x, y):
    for i in range(N):
        a = random.randint(0, (1<<BW)-1)
        b = random.randint(0, (1<<BW)-1)
        c = a + b
        #print(a, b, c)
        x[i] = binvecAB(a, b)
        y[i] = binvecC(c)
        #print(a, b, c, x[i], y[i])
        #print()

x = np.zeros((N, BW * 2))
y = np.zeros((N, BW + 1))
gen(x, y)

x = torch.tensor(x).to(torch.float)
y = torch.tensor(y).to(torch.float)

# %%
model = torch.nn.Sequential(
    torch.nn.Linear(BW * 2, 1024),
    #torch.nn.Tanh(),
    torch.nn.ReLU(),
    torch.nn.Linear(1024, BW + 1),
    #torch.nn.Softmax(BW+1),
    #torch.nn.Tanh(),
    #torch.nn.ReLU(),
    torch.nn.Sigmoid(),
)

#model = torch.nn.RNN(BW * 2, BW + 1)
#model(x)

t = 0
# %%
loss_fn = torch.nn.MSELoss(reduction='sum')
learning_rate = 1e-3
#optimizer = torch.optim.RMSprop(model.parameters(), lr=learning_rate)
optimizer = torch.optim.Adam(model.parameters(), lr=learning_rate)
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
    v = binvecAB(a, b)
    v = torch.tensor(v).to(torch.float)
    out = model(v.unsqueeze(0))
    out = torch.where(out > 0.5, 1, 0)
    #return out
    r = 0
    for i in range(BW+1):
        if out[0][BW-i] > 0.5:
            r += 1<<i
    return r

#for i in range(10):
for a in range(10):
  for b in range(10):
    # a = random.randint(0, (1<<BW)-1)
    # b = random.randint(0, (1<<BW)-1)
    c = a + b
    c_est = nnAdd(a, a)
    print(a, b, c, c_est, c == c_est)
# == torch.tensor(binvecC(255+255)).to(torch.float))
