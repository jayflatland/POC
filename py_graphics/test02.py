
import numpy as np

M = np.array([
    [ 1, 0, 0],
    [ 0, 1, 0],
    [ 0, 0, 1]])

v = np.array([[3], [7], [-9]])

print(M)
print(v)
r = np.matmul(M, v)
print(r)


from scipy.spatial.transform import Rotation as R
R.from_rotvec(0.0, np.array([1, 0, 0]))
