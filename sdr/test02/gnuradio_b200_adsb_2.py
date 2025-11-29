#!/usr/bin/env python3

CHATGPT IS FUCKING STUPID

from gnuradio import gr, blocks, uhd
import time

# Ubuntu package uses this namespace
import air_modes

class ADSBReceiver(gr.top_block):
    def __init__(self):
        gr.top_block.__init__(self, "ADSB Receiver USRP B200")

        ##################################################
        # Parameters
        ##################################################
        self.samp_rate = 2_000_000
        self.center_freq = 1090e6
        self.gain = 40

        ##################################################
        # Blocks
        ##################################################

        # USRP Source
        self.usrp = uhd.usrp_source(
            ",".join(("", "")),
            uhd.stream_args(
                cpu_format="fc32",
                channels=[0],
            ),
        )
        self.usrp.set_samp_rate(self.samp_rate)
        self.usrp.set_center_freq(self.center_freq)
        self.usrp.set_gain(self.gain)

        # gr-air-modes GNU Radio blocks
        self.rx = air_modes.rx_path(self.samp_rate)

        # Print decoded messages
        self.printer = blocks.message_debug()

        ##################################################
        # Connections
        ##################################################
        self.connect(self.usrp, self.rx)
        self.msg_connect(self.rx, "out", self.printer, "print")

if __name__ == "__main__":
    tb = ADSBReceiver()
    tb.start()
    print("Receiving ADS-B on 1090 MHz...")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        tb.stop()
        tb.wait()
