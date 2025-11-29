#!/usr/bin/env python3

from gnuradio import gr, analog, audio, uhd, filter, fft
# import time

class FMDemodFromUSRP(gr.top_block):
    def __init__(self, center_freq, sample_rate, audio_rate, gain):
        super().__init__()

        self.components = []

        # USRP source block
        master_clock_rate = 56e6
        sample_rate = 191_781*4#48_000*8#master_clock_rate // (128*9)
        audio_rate = 191_781
        usrp_source = uhd.usrp_source(
            ",".join(("", f"master_clock_rate={master_clock_rate}")),
            uhd.stream_args(cpu_format="fc32", channels=[0]),
        )
        usrp_source.set_samp_rate(sample_rate)
        usrp_source.set_center_freq(center_freq)
        usrp_source.set_gain(gain)
        usrp_source.set_antenna("TX/RX")
        # usrp_source.set_antenna("RX2")

        self.components.append(usrp_source)

        # ### --- Tight Bandpass Filter --- ###
        # # Passband: ±100 kHz, Transition: 20 kHz
        # bw = 200e3   # Bandwidth to pass (±100 kHz)
        # trans = 20e3 # Transition width
        # taps = filter.firdes.band_pass(
        #     gain=1.0,
        #     sampling_freq=sample_rate,
        #     low_cutoff_freq=50e6,#center_freq - 2000e3,
        #     high_cutoff_freq=200e6,#center_freq + 2000e3,
        #     transition_width=10e6#,
        #     #window=fft.fft_python.window.win_type.WIN_HAMMING,
        #     #param=6.76
        # )

        # bandpass_filter = filter.fir_filter_ccf(1, taps)  # Complex -> Complex
        # self.components.append(bandpass_filter)

        # bw = 80e3   # Bandwidth to pass (±100 kHz)
        # trans = 35e3 # Transition width
        # taps = filter.firdes.band_pass(
        #     gain=1.0,
        #     sampling_freq=sample_rate,
        #     low_cutoff_freq=bw / 2 - trans,
        #     high_cutoff_freq=bw / 2 + trans,
        #     transition_width=trans,
        #     window=fft.fft_python.window.win_type.WIN_HAMMING
        # )

        # bandpass_filter = filter.fir_filter_ccf(1, taps)  # Complex -> Complex
        # self.components.append(bandpass_filter)

        # FM demodulator (wideband FM)
        wfm_rcv = analog.wfm_rcv(
            quad_rate=sample_rate,
            audio_decimation=int(sample_rate // audio_rate)
        )
        self.components.append(wfm_rcv)

        # Audio sink
        audio_sink = audio.sink(int(audio_rate), "", True)
        self.components.append(audio_sink)

        for i in range(len(self.components) - 1):
            self.connect(self.components[i], self.components[i+1])

if __name__ == '__main__':
    tb = FMDemodFromUSRP(
        center_freq=101.1e6,
        sample_rate=56.0e6,
        audio_rate=48_000,
        gain=0
    )
    tb.start()
    input("Press Enter to stop...\n")
    tb.stop()
    tb.wait()
