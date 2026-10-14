#!/usr/bin/env python3
"""
Record raw GPS L1 IQ from the B200.

Saves interleaved int16 I/Q (sc16) plus a small .json sidecar with the
sample rate and frequency, for gps_receiver.py.

    ./record_gps.py --seconds 45 --out gps.sc16

A position fix needs ~40 s (ephemeris is broadcast every 30 s).
GPS is ~20 dB below the noise floor, so the antenna needs a clear view of
the sky. An active antenna needs a bias-tee; the B200 doesn't supply one.
"""

import argparse
import json
import sys
import time

import numpy as np
import uhd


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="gps.sc16")
    ap.add_argument("--seconds", type=float, default=45.0)
    ap.add_argument("--rate", type=float, default=4e6)
    ap.add_argument("--freq", type=float, default=1575.42e6)
    ap.add_argument("--gain", type=float, default=60.0)
    ap.add_argument("--antenna", default="TX/RX")
    ap.add_argument("--args", default="type=b200")
    a = ap.parse_args()

    usrp = uhd.usrp.MultiUSRP(a.args)
    usrp.set_rx_rate(a.rate, 0)
    usrp.set_rx_freq(uhd.types.TuneRequest(a.freq), 0)
    usrp.set_rx_gain(a.gain, 0)
    usrp.set_rx_antenna(a.antenna, 0)
    usrp.set_rx_bandwidth(a.rate, 0)
    rate = usrp.get_rx_rate(0)
    freq = usrp.get_rx_freq(0)
    print(f"B200: {rate/1e6:.3f} MS/s at {freq/1e6:.3f} MHz, gain {usrp.get_rx_gain(0):.0f} dB, "
          f"antenna {usrp.get_rx_antenna(0)}")

    st_args = uhd.usrp.StreamArgs("fc32", "sc16")
    st_args.channels = [0]
    streamer = usrp.get_rx_stream(st_args)
    md = uhd.types.RXMetadata()

    total = int(a.seconds * rate)
    buf = np.zeros((1, 200_000), dtype=np.complex64)
    got = 0
    overflows = 0

    cmd = uhd.types.StreamCMD(uhd.types.StreamMode.start_cont)
    cmd.stream_now = True
    streamer.issue_stream_cmd(cmd)

    with open(a.out, "wb") as f:
        t0 = time.time()
        while got < total:
            n = streamer.recv(buf, md, 1.0)
            if md.error_code == uhd.types.RXMetadataErrorCode.overflow:
                overflows += 1
                continue
            if md.error_code != uhd.types.RXMetadataErrorCode.none:
                print(f"RX error: {md.strerror()}", file=sys.stderr)
                continue
            n = min(n, total - got)
            iq = buf[0, :n].view(np.float32)
            f.write(np.clip(iq * 32767.0, -32767, 32767).astype(np.int16).tobytes())
            got += n
            if got // int(rate) != (got - n) // int(rate):
                print(f"\r{got/rate:5.1f} / {a.seconds:.0f} s   overflows {overflows}", end="", flush=True)

    streamer.issue_stream_cmd(uhd.types.StreamCMD(uhd.types.StreamMode.stop_cont))
    print(f"\nWrote {got} samples ({got/rate:.1f} s) to {a.out} in {time.time()-t0:.1f} s, overflows {overflows}")

    with open(a.out + ".json", "w") as f:
        json.dump({"rate": rate, "freq": freq, "gain": a.gain, "antenna": a.antenna,
                   "overflows": overflows, "format": "sc16"}, f, indent=2)


if __name__ == "__main__":
    main()
