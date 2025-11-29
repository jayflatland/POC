#!/usr/bin/env python3
# -*- coding: utf-8 -*-

CLAUDE CHATGPT IS FUCKING STUPID

"""
ADS-B Receiver for USRP B200
Receives 1090 MHz ADS-B signals and demodulates them
"""

from gnuradio import gr, blocks, filter, analog, digital
from gnuradio import uhd
import time

class adsb_receiver(gr.top_block):
    def __init__(self):
        gr.top_block.__init__(self, "ADS-B USRP Receiver")

        ##################################################
        # Variables
        ##################################################
        self.samp_rate = samp_rate = 2e6  # 2 MHz sample rate
        self.center_freq = center_freq = 1090e6  # 1090 MHz ADS-B frequency
        self.gain = gain = 40  # RF gain in dB
        self.threshold = threshold = 0.01  # Detection threshold

        ##################################################
        # Blocks
        ##################################################
        
        # USRP Source
        self.uhd_source = uhd.usrp_source(
            ",".join(("", "")),
            uhd.stream_args(
                cpu_format="fc32",
                channels=list(range(1)),
            ),
        )
        self.uhd_source.set_samp_rate(samp_rate)
        self.uhd_source.set_center_freq(center_freq, 0)
        self.uhd_source.set_gain(gain, 0)
        self.uhd_source.set_antenna('RX2', 0)
        
        # Low pass filter to isolate ADS-B signal
        self.lpf = filter.fir_filter_ccf(
            1,
            filter.firdes.low_pass(
                1, samp_rate, 1e6, 0.5e6,
                filter.firdes.WIN_HAMMING, 6.76
            )
        )
        
        # Magnitude (convert complex to real)
        self.mag = blocks.complex_to_mag_squared(1)
        
        # Moving average for smoothing
        self.moving_avg = filter.single_pole_iir_filter_ff(0.01, 1)
        
        # Threshold detection for preamble
        self.threshold_block = analog.threshold_ff(threshold, threshold, 0)
        
        # File sink to save raw data for post-processing
        self.file_sink = blocks.file_sink(gr.sizeof_float*1, 'adsb_output.dat', False)
        self.file_sink.set_unbuffered(False)
        
        # Optional: UDP sink to send data to dump1090 or other decoder
        # Uncomment if you want to stream to another application
        # self.udp_sink = blocks.udp_sink(gr.sizeof_float*1, '127.0.0.1', 30001, 1472, True)

        ##################################################
        # Connections
        ##################################################
        self.connect((self.uhd_source, 0), (self.lpf, 0))
        self.connect((self.lpf, 0), (self.mag, 0))
        self.connect((self.mag, 0), (self.moving_avg, 0))
        self.connect((self.moving_avg, 0), (self.threshold_block, 0))
        self.connect((self.threshold_block, 0), (self.file_sink, 0))
        # self.connect((self.threshold_block, 0), (self.udp_sink, 0))

    def get_samp_rate(self):
        return self.samp_rate

    def set_samp_rate(self, samp_rate):
        self.samp_rate = samp_rate
        self.uhd_source.set_samp_rate(self.samp_rate)

    def get_center_freq(self):
        return self.center_freq

    def set_center_freq(self, center_freq):
        self.center_freq = center_freq
        self.uhd_source.set_center_freq(self.center_freq, 0)

    def get_gain(self):
        return self.gain

    def set_gain(self, gain):
        self.gain = gain
        self.uhd_source.set_gain(self.gain, 0)


def main(top_block_cls=adsb_receiver, options=None):
    tb = top_block_cls()
    
    def sig_handler(sig=None, frame=None):
        tb.stop()
        tb.wait()
        exit(0)

    import signal
    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    tb.start()
    
    try:
        input('Press Enter to quit: ')
    except EOFError:
        pass
    except KeyboardInterrupt:
        pass
    
    tb.stop()
    tb.wait()


if __name__ == '__main__':
    main()