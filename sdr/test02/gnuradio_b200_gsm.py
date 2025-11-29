#!/usr/bin/env python3

from gnuradio import gr, blocks, uhd
import time

class GSMCapture(gr.top_block):
    def __init__(self):

        gr.top_block.__init__(self, "GSM Capture")

        ##################################################
        # Parameters
        ##################################################
        self.samp_rate = 2_000_000      # 2 MHz works for a single channel
        self.center_freq = 945.2e6      # Example: GSM900 BCCH around ARFCN 62
        self.gain = 40                  # Adjust for your environment

        ##################################################
        # USRP Source
        ##################################################
        self.src = uhd.usrp_source(
            ",".join(("", "")),
            uhd.stream_args("fc32")
        )
        self.src.set_samp_rate(self.samp_rate)
        self.src.set_center_freq(self.center_freq)
        self.src.set_gain(self.gain)
        self.src.set_antenna("TX/RX")

        ##################################################
        # Output: file or FIFO for gr-gsm
        ##################################################

        # Write captured IQ to file
        self.sink = blocks.file_sink(
            itemsize=8,   # complex64 = 8 bytes
            filename="gsm_capture.cfile"
        )
        self.sink.set_unbuffered(False)

        ##################################################
        # Connections
        ##################################################
        self.connect(self.src, self.sink)


if __name__ == "__main__":
    tb = GSMCapture()
    tb.start()
    print("Capturing GSM at 945.2 MHz... Press Ctrl+C to stop.")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        tb.stop()
        tb.wait()
        print("Saved to gsm_capture.cfile")
