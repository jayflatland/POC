import time
import numpy as np
import matplotlib.pyplot as plt

# %%

# uhd_rx_cfile -f 101100000 -r 400000 -A TX/RX -v test_101fox.cfile

# uhd_rx_cfile -f 915000000 -r 30000000 -A TX/RX -v test.cfile
fs = 30_000_000  # Sample rate
x = np.fromfile('test.cfile', dtype=np.complex64)

#uhd_rx_cfile -f 101100000 -r 10000000 -A TX/RX -v test_all_fm.cfile
# fs = 10_000_000  # Sample rate
# x = np.fromfile('test_all_fm.cfile', dtype=np.complex64)

# fs = 30_000_000  # Sample rate
#x = np.fromfile('test_101fox.cfile', dtype=np.float32)

# max_subdiv = (1<<20)
# n = x.shape[0] // max_subdiv * max_subdiv
# x = x[:n]

# %%
# subdiv = 8192
# x2 = x.reshape(subdiv, n//subdiv).mean(axis=0)
# i2 = np.real(x2)
# q2 = np.imag(x2)

# %%
# plt.figure()
# plt.plot(i2)
# plt.plot(q2)
# plt.show()

# %%

# Simulate or load complex IQ data
duration = 0.1  # seconds
t = np.arange(0, duration, 1/fs)
iq_data = x

# FFT parameters
fft_size = 1024
overlap = 0.5
hop_size = int(fft_size * (1 - overlap))

# Prepare FFT windowed frames
n_frames = (len(iq_data) - fft_size) // hop_size
waterfall = np.zeros((n_frames, fft_size))

for i in range(n_frames):
    print(f"{i} of {n_frames}")
    start = i * hop_size
    chunk = iq_data[start:start+fft_size]
    spectrum = np.fft.fftshift(np.fft.fft(chunk))
    power = np.abs(spectrum)**2
    waterfall[i, :] = power

# Convert to dB scale
waterfall_db = 10 * np.log10(waterfall + 1e-12)

# %%
# Plot the waterfall
plt.figure(figsize=(10, 6))
extent = [-fs/2/1e3, fs/2/1e3, 0, duration]  # x: freq (kHz), y: time (s)
plt.imshow(
    waterfall_db,
    aspect='auto',
    extent=extent,
    origin='lower',
    cmap='magma',
    vmin=-40, 
    #vmax=0
)
plt.xlabel("Frequency (kHz)")
plt.ylabel("Time (s)")
plt.title("Static FFT Waterfall")
plt.colorbar(label="Power (dB)")
plt.tight_layout()
plt.show()
