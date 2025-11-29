#!/usr/bin/env python3

from gnuradio import gr, blocks, uhd#, analog
#import numpy as np
import time

class ADSBBurstDetect(gr.top_block):
    def __init__(self):
        gr.top_block.__init__(self, "Minimal ADS-B Pulse Detector")

        self.samp_rate = 2_000_000
        self.center_freq = 1090e6
        self.gain = 50

        self.usrp = uhd.usrp_source(
            ",".join(("", "")),
            uhd.stream_args(cpu_format="fc32", channels=[0])
        )
        self.usrp.set_samp_rate(self.samp_rate)
        self.usrp.set_center_freq(self.center_freq)
        self.usrp.set_gain(self.gain)

        # Magnitude squared (power)
        self.mag2 = blocks.complex_to_mag_squared(1)

        # Threshold for pulse detection
        self.thresh = blocks.threshold_ff(5e-5, 5e-5, 0)

        # Burst detector: prints when threshold exceeded
        self.probe = blocks.probe_signal_f()

        self.connect(self.usrp, self.mag2, self.thresh)
        self.connect(self.thresh, self.probe)

    def poll(self):
        # Reads current signal value
        return self.probe.level()

if __name__ == "__main__":
    tb = ADSBBurstDetect()
    tb.start()
    print("Listening for ADS-B pulses...")
    try:
        while True:
            val = tb.poll()
            if val > 0.0:
                print("Burst detected at", time.time())
            time.sleep(0.001)
    except KeyboardInterrupt:
        tb.stop()
        tb.wait()
