#!/usr/bin/env python3

from gnuradio import gr, blocks, analog, audio, filter
import numpy as np
import sys

class FMDemodFromIQ(gr.top_block):
    def __init__(self, iq_file, sample_rate=3000000, audio_rate=48_000):
        super().__init__()

        # Source: Interleaved float32 I/Q data
        self.source = blocks.file_source(gr.sizeof_float, iq_file, False)
        
        # Deinterleave: Split float32 [I, Q, I, Q, ...] into two float streams
        self.deinterleave = blocks.deinterleave(gr.sizeof_float)
        
        # Combine into complex: (I + jQ)
        self.float_to_complex = blocks.float_to_complex()
        print(self.float_to_complex)

        # FM demodulator
        self.wfm_rcv = analog.wfm_rcv(
            quad_rate=sample_rate,
            audio_decimation=int(sample_rate // audio_rate)
        )

        # Audio sink
        self.audio_sink = audio.sink(audio_rate, '', True)

        # Connect blocks
        self.connect(self.source, self.deinterleave)
        self.connect((self.deinterleave, 0), (self.float_to_complex, 0))
        self.connect((self.deinterleave, 1), (self.float_to_complex, 1))
        self.connect(self.float_to_complex, self.wfm_rcv)
        self.connect(self.wfm_rcv, self.audio_sink)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python3 fm_demod.py <iq_file>")
        sys.exit(1)

    iq_file = sys.argv[1]
    tb = FMDemodFromIQ(iq_file)
    tb.start()
    input("Press Enter to stop...\n")
    tb.stop()
    tb.wait()
