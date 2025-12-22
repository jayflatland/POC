import time
import numpy as np
import matplotlib.pyplot as plt

# %%

# uhd_rx_cfile -s 56e6 -r 56e6 -f 913.1e6 -g 40 -A RX2 -m test1.cfile
fs = 56e6 # Sample rate
x = np.fromfile('test1.cfile', dtype=np.complex64)

#round size down to nearest chunk
chunk_sz = (1<<20)
n = x.shape[0] // chunk_sz * chunk_sz
x = x[:n]

# %%
subdiv = 8192
x2 = x.reshape(subdiv, n//subdiv).mean(axis=0)
i2 = np.real(x2)
q2 = np.imag(x2)

# %%
plt.figure()
plt.plot(i2)
plt.plot(q2)
plt.show()

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
    if i % 100000 == 0:
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
