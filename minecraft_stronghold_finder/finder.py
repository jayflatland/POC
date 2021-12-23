import numpy as np
import matplotlib.pylab as plt
import math

samples = [
    #   x        y         z        ang
    (-175.0,    69.0,    440.0,    47.4), #0
    (-310.0,    69.0,    440.0,    44.1), #1
    (-380.0,    66.0,    323.0,    38.5), #2

    (-2581.0,   74.0,   3024.0,  -137.5), #3 ????
    (-2497.0,  108.0,   2946.0,  -140.7), #4
    (-2315.0,   70.0,   2719.0,  -138.9),
    
    (-1774.0,   64.0,   2162.0,  -146.4),
    (-1541.0,   75.0,   1960.0,  -151.1),
    (-1292.0,   68.0,   1647.0,  -166.3),
    (-1172.0,   64.0,   1332.0,    39.1),
]


def calc_m_and_b(x, z, ang):
    ang_rad = ang * math.pi / 180.0
    m = math.cos(-ang_rad) / math.sin(-ang_rad)
    b = z - m * x
    return m, b

plt.figure()

x_ests = []
z_ests = []
x_locs = []
z_locs = []
for i1 in range(len(samples)):
    x1, y1, z1, ang1 = samples[i1]
    m1, b1 = calc_m_and_b(x1, z1, ang1)

    tmp_x1 = -3000.0
    tmp_z1 = m1 * tmp_x1 + b1
    tmp_x2 = 3000.0
    tmp_z2 = m1 * tmp_x2 + b1

    #print([tmp_x1, tmp_z1], [tmp_x2, tmp_z2])
    x_locs.append(x1)
    z_locs.append(z1)

    plt.plot([tmp_x1, tmp_x2], [tmp_z1, tmp_z2])

    for i2 in range(i1 + 1, len(samples)):
        x2, y2, z2, ang2 = samples[i2]

        m2, b2 = calc_m_and_b(x2, z2, ang2)
        
        """
        m1 * x + b1 = m2 * x + b2
                  b1 - b2 = m2 * x - m1 * x 
                  b1 - b2 = (m2 - m1) * x 
        """
        #print(m1, b1, m2, b2)
        x = (b1 - b2) / (m2 - m1)
        z = m1 * x + b1
        #print(f"i1={i1}, x1={x1:5.0f}, z1={z1:5.0f}, ang1={ang1:5.0f}, i2={i2}, x2={x2:5.0f}, z2={z2:5.0f}, ang2={ang2:5.0f}, x={x:5.0f}, z={z:5.0f}")
        print(f"i1={i1}, i2={i2}, x={x:5.0f}, z={z:5.0f}")
        x_ests.append(x)
        z_ests.append(z)
        


#plt.scatter(x_locs, z_locs)
plt.scatter(x_ests, z_ests)

np.median(x_ests)
np.median(z_ests)
