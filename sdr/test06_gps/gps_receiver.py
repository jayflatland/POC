#!/usr/bin/env python3
"""
Software GPS L1 C/A receiver for recordings made with record_gps.py.

    ./gps_receiver.py gps.sc16                  # acquire, track, decode, position fix
    ./gps_receiver.py gps.sc16 --acquire-only   # just search for satellites

Stages:
    1. acquisition  - FFT search over PRN 1-32, Doppler, and code phase
    2. tracking     - Costas PLL + early/prompt/late DLL, 1 ms at a time
    3. nav decode   - bit sync, subframe preambles, parity, ephemeris
    4. position     - pseudoranges + least squares (needs 4+ satellites)
"""

import argparse
import json

import numpy as np

CHIP_RATE = 1.023e6
CODE_LEN = 1023
L1 = 1575.42e6
C = 299_792_458.0

# G2 delay taps (phase selector) for PRN 1-32, from IS-GPS-200
G2_TAPS = {
    1: (2, 6), 2: (3, 7), 3: (4, 8), 4: (5, 9), 5: (1, 9), 6: (2, 10), 7: (1, 8), 8: (2, 9),
    9: (3, 10), 10: (2, 3), 11: (3, 4), 12: (5, 6), 13: (6, 7), 14: (7, 8), 15: (8, 9), 16: (9, 10),
    17: (1, 4), 18: (2, 5), 19: (3, 6), 20: (4, 7), 21: (5, 8), 22: (6, 9), 23: (1, 3), 24: (4, 6),
    25: (5, 7), 26: (6, 8), 27: (7, 9), 28: (8, 10), 29: (1, 6), 30: (2, 7), 31: (3, 8), 32: (4, 9),
}


def ca_code(prn):
    """C/A code for a PRN as +1/-1 chips."""
    g1 = [1] * 10
    g2 = [1] * 10
    t1, t2 = G2_TAPS[prn]
    chips = np.zeros(CODE_LEN)
    for i in range(CODE_LEN):
        out = g1[9] ^ (g2[t1 - 1] ^ g2[t2 - 1])
        chips[i] = 1.0 - 2.0 * out  # 0 -> +1, 1 -> -1
        f1 = g1[2] ^ g1[9]
        f2 = g2[1] ^ g2[2] ^ g2[5] ^ g2[7] ^ g2[8] ^ g2[9]
        g1 = [f1] + g1[:9]
        g2 = [f2] + g2[:9]
    return chips


CODES = {prn: ca_code(prn) for prn in G2_TAPS}


def load(path, seconds=None, start=0.0):
    meta = json.load(open(path + ".json"))
    fs = meta["rate"]
    count = -1 if seconds is None else int(seconds * fs) * 2
    raw = np.fromfile(path, dtype=np.int16, count=count, offset=int(start * fs) * 4)
    x = raw[0::2].astype(np.float32) + 1j * raw[1::2].astype(np.float32)
    return x.astype(np.complex64), fs, meta


# -------------------- acquisition --------------------

def sampled_code(prn, fs, n, code_phase=0.0):
    idx = (np.floor(code_phase + np.arange(n) * CHIP_RATE / fs).astype(np.int64)) % CODE_LEN
    return CODES[prn][idx]


def acquire(x, fs, prns=range(1, 33), max_doppler=10e3, ms_noncoh=20, threshold=2.5):
    """Parallel code-phase search. Returns list of dicts sorted by strength."""
    n = int(round(fs * 1e-3))  # samples per 1 ms code period
    t = np.arange(n) / fs
    blocks = x[: n * ms_noncoh].reshape(ms_noncoh, n)
    # Doppler step: well under 1/(1 ms) so we don't lose much between bins
    dopplers = np.arange(-max_doppler, max_doppler + 1, 250.0)
    results = []
    for prn in prns:
        code_f = np.conj(np.fft.fft(sampled_code(prn, fs, n)))
        grid = np.zeros((len(dopplers), n), dtype=np.float32)
        for di, fd in enumerate(dopplers):
            wipe = np.exp(-2j * np.pi * fd * t).astype(np.complex64)
            corr = np.fft.ifft(np.fft.fft(blocks * wipe, axis=1) * code_f, axis=1)
            grid[di] = np.sum(np.abs(corr) ** 2, axis=0)
        di, si = np.unravel_index(np.argmax(grid), grid.shape)
        peak = grid[di, si]
        # second peak: same Doppler row, excluding +/- 1 chip around the main peak
        chip_samps = int(np.ceil(fs / CHIP_RATE)) + 1
        row = grid[di].copy()
        mask = (np.arange(n) - si + n // 2) % n - n // 2
        row[np.abs(mask) <= chip_samps] = 0
        ratio = peak / row.max()
        # code phase in chips at sample 0 of the recording
        code_phase = (-si * CHIP_RATE / fs) % CODE_LEN
        results.append({"prn": prn, "doppler": float(dopplers[di]), "sample": int(si),
                        "code_phase": float(code_phase), "ratio": float(ratio),
                        "found": ratio > threshold})
    return sorted(results, key=lambda r: -r["ratio"])


def print_acquisition(results):
    print(" PRN   peak ratio   Doppler (Hz)   code offset (samples)")
    for r in results:
        mark = "  <-- found" if r["found"] else ""
        print(f" {r['prn']:3d}   {r['ratio']:8.2f}     {r['doppler']:+8.0f}       {r['sample']:6d}{mark}")
    found = [r for r in results if r["found"]]
    print(f"\n{len(found)} satellite(s) found")
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file")
    ap.add_argument("--acquire-only", action="store_true")
    ap.add_argument("--threshold", type=float, default=2.5, help="acquisition peak / second-peak ratio")
    a = ap.parse_args()

    x, fs, meta = load(a.file, seconds=0.1)
    print(f"{a.file}: {fs/1e6:.3f} MS/s, {meta['freq']/1e6:.3f} MHz, gain {meta['gain']} dB, "
          f"antenna {meta['antenna']}\n")
    found = print_acquisition(acquire(x, fs, threshold=a.threshold))
    if a.acquire_only or not found:
        return


if __name__ == "__main__":
    main()
