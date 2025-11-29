import numpy as np
import sounddevice as sd

# --- Parameters ---
duration = 10.0        # seconds
#sample_rate = 44100    # samples per second
sample_rate = 192000    # samples per second

# --- Time axis ---
t = np.linspace(0, duration, int(sample_rate * duration), endpoint=False)

# --- Mathematical formula for audio ---
# Replace this with any function you like!
frequency = 440  # A4 pitch
signal = np.sin(2 * np.pi * frequency * t) * np.exp(-2 * t)

# Normalize to 16-bit range (optional but safer)
#signal = signal / np.max(np.abs(signal))
#signal = signal * 0.0
signal[:] = 0.0
i = 1000
while i + 100 < len(signal):
    signal[i:i+100] = 10.0
    i += 25000
print(signal)
print(len(signal))

# --- Play audio ---
sd.play(signal, sample_rate)
sd.wait()  # Wait until done
