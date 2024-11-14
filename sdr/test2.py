from rtlsdr import RtlSdr
import numpy as np
import matplotlib.pyplot as plt

sdr = RtlSdr()
sdr.sample_rate = 3e6 # Hz
sdr.center_freq = 101.1e6   # Hz
#sdr.freq_correction = 60  # PPM
#print(sdr.valid_gains_db)
sdr.gain = 49.6
#print(sdr.gain)

x = sdr.read_samples(2<<20)
# x = x[-4096:]
sdr.close()

# plt.figure()
# plt.plot(x.real, label="I")
# plt.plot(x.imag, label="Q")
# plt.legend()
# plt.show()