#!/usr/bin/env python3
from gnuradio import gr, uhd, analog, filter, audio, blocks  # added blocks
from gnuradio.fft import window
import time


class AMAirbandReceiver(gr.top_block):
    def __init__(self,
                 center_freq=101.1e6,#133e6,
                 samp_rate=1e6,
                 audio_rate=48000,
                 gain=40):

        super().__init__()

        # USRP Source
        self.usrp = uhd.usrp_source(
            ",".join(("", "")),
            uhd.stream_args(cpu_format="fc32", channels=[0])
        )
        self.usrp.set_samp_rate(samp_rate)
        self.usrp.set_center_freq(center_freq)
        self.usrp.set_gain(gain)
        self.usrp.set_antenna("TX/RX")  # ✅ Use TX/RX antenna

        # Convert complex IQ to magnitude (AM envelope)
        self.am_demod = blocks.complex_to_mag()  # fixed from analog to blocks

        # Low-pass filter for audio
        audio_cutoff = 5e3
        transition = 2e3
        lpf_taps = filter.firdes.low_pass(
            gain=1.0,
            sampling_freq=samp_rate,
            cutoff_freq=audio_cutoff,
            transition_width=transition,
            window=window.win_type.WIN_HAMMING  # ✅ Use FFT window enum
        )
        self.audio_lpf = filter.fir_filter_fff(1, lpf_taps)

        # Resample to audio rate
        from gnuradio.filter import rational_resampler_fff
        self.resampler = rational_resampler_fff(
            interpolation=int(audio_rate),
            decimation=int(samp_rate),
            taps=[],
            fractional_bw=0.4
        )

        # Audio Output
        self.audio_out = audio.sink(int(audio_rate), "", True)

        # Connect blocks
        self.connect(self.usrp, self.am_demod)
        self.connect(self.am_demod, self.audio_lpf)
        self.connect(self.audio_lpf, self.resampler)
        self.connect(self.resampler, self.audio_out)

if __name__ == '__main__':
    tb = AMAirbandReceiver()
    tb.start()
    input("Press Enter to stop...\n")
    tb.stop()
    tb.wait()
