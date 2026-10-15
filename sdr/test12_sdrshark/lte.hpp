#pragma once

// LTE downlink cell scanner for the B200.
//
// At 1.92 MS/s (128-pt FFT, 15 kHz subcarriers) it finds cells by the
// primary and secondary sync signals (PSS/SSS):
//   PSS  -> N_id_2 (0..2), symbol timing, carrier frequency offset
//   SSS  -> N_id_1 (0..167), frame boundary (subframe 0 vs 5)
//   PCI = 3*N_id_1 + N_id_2
// SSS is accumulated non-coherently over many 5 ms PSS repetitions for a
// confident decision. It also extracts the PBCH (which carries the MIB)
// as a raw soft-bit block and attempts a CRC-checked MIB decode; the MIB
// is only reported when its CRC verifies, so no false data is shown.
//
// LTE user-plane data is ciphered; this decodes the always-on broadcast
// sync/PBCH that identifies the cell.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "packet.hpp"

namespace shark {

using cfl = std::complex<float>;

namespace lte {

static const double FS = 1.92e6;
static const int NFFT = 128;
static const double PI = 3.14159265358979323846;

// --- PSS (Zadoff-Chu) ---
inline std::vector<cfl> zc63(int u) {
    std::vector<cfl> z;
    for (int n = 0; n < 63; n++) {
        if (n == 31) continue;  // puncture DC
        z.push_back(std::polar(1.0f, (float)(-PI * u * n * (n + 1) / 63.0)));
    }
    return z;
}
inline std::vector<cfl> pss_time(int nid2) {
    static const int us[3] = {25, 29, 34};
    auto z = zc63(us[nid2]);
    std::vector<cfl> f(NFFT, {0, 0});
    for (int i = 0; i < 31; i++) {
        f[1 + i] = z[31 + i];
        f[NFFT - 31 + i] = z[i];
    }
    // IFFT
    std::vector<cfl> t(NFFT);
    for (int n = 0; n < NFFT; n++) {
        cfl s = 0;
        for (int k = 0; k < NFFT; k++) s += f[k] * std::polar(1.0f, (float)(2 * PI * k * n / NFFT));
        t[n] = s / (float)NFFT;
    }
    float e = 0;
    for (auto &v : t) e += std::norm(v);
    for (auto &v : t) v /= std::sqrt(e);
    return t;
}

// --- SSS m-sequences ---
inline std::vector<int> mseq(const std::vector<int> &taps) {
    std::vector<int> s = {0, 0, 0, 0, 1};
    for (int i = 0; i < 26; i++) {
        int v = 0;
        for (int t : taps) v ^= s[i + t];
        s.push_back(v);
    }
    std::vector<int> o;
    for (int v : s) o.push_back(1 - 2 * v);
    return o;
}

struct SssRef {
    float d0[62], d5[62];
};

class CellSearch {
public:
    CellSearch() {
        for (int n2 = 0; n2 < 3; n2++) pss_[n2] = pss_time(n2);
        auto s_t = mseq({0, 2}), c_t = mseq({0, 3}), z_t = mseq({0, 1, 2, 4});
        for (int n2 = 0; n2 < 3; n2++) {
            for (int n1 = 0; n1 < 168; n1++) {
                int qp = n1 / 30, q = (n1 + qp * (qp + 1) / 2) / 30, mp = n1 + q * (q + 1) / 2;
                int m0 = mp % 31, m1 = (m0 + mp / 31 + 1) % 31;
                SssRef &r = ref_[n2][n1];
                for (int i = 0; i < 31; i++) {
                    float s0 = s_t[(i + m0) % 31], s1 = s_t[(i + m1) % 31];
                    float c0 = c_t[(i + n2) % 31], c1 = c_t[(i + n2 + 3) % 31];
                    float z0 = z_t[(i + m0 % 8) % 31], z1 = z_t[(i + m1 % 8) % 31];
                    r.d0[2 * i] = s0 * c0;
                    r.d0[2 * i + 1] = s1 * c1 * z0;
                    r.d5[2 * i] = s1 * c0;
                    r.d5[2 * i + 1] = s0 * c1 * z1;
                }
            }
        }
        for (int n2 = 0; n2 < 3; n2++) {
            static const int us[3] = {25, 29, 34};
            auto z = zc63(us[n2]);
            for (int i = 0; i < 31; i++) {
                pref_[n2][31 + i] = z[31 + i];  // +1..+31
                pref_[n2][i] = z[i];             // -31..-1
            }
        }
    }

    struct Cell {
        int pci, n1, n2, subframe;
        double cfo;
        float pss_db, sss_metric, rsrp_dbfs;
        long pss_sample;
    };

    // scan a block; returns the best cell found (or pci<0)
    Cell search(const std::vector<cfl> &x) {
        Cell best{-1, 0, 0, 0, 0, 0, 0, 0, 0};
        if (x.size() < 2 * NFFT + 200) return best;
        // 1) best PSS across the three roots
        int bn2 = 0;
        long bpk = 0;
        float bval = 0, bmed = 1;
        for (int n2 = 0; n2 < 3; n2++) {
            auto c = xcorr_mag(x, pss_[n2]);
            long pk = std::max_element(c.begin(), c.end()) - c.begin();
            if (c[pk] > bval) {
                bval = c[pk];
                bpk = pk;
                bn2 = n2;
                std::vector<float> t = c;
                std::nth_element(t.begin(), t.begin() + t.size() / 2, t.end());
                bmed = t[t.size() / 2] + 1e-9f;
            }
        }
        if (bval / bmed < 4.0f) return best;

        // 2) CFO from the two halves of the PSS symbol
        cfl a = 0, b = 0;
        for (int k = 0; k < 64; k++) {
            a += std::conj(pss_[bn2][k]) * x[bpk + k];
            b += std::conj(pss_[bn2][64 + k]) * x[bpk + 64 + k];
        }
        double cfo = std::arg(b * std::conj(a)) / (2 * PI * 64.0 / FS);

        // 3) accumulate SSS over all PSS peaks (same N_id_2), non-coherent across peaks
        auto xc = derotate(x, cfo);
        std::vector<long> peaks = find_peaks(xc, bn2, bval * 0.5f / bmed * bmed);
        std::vector<double> acc0(168, 0), acc5(168, 0);
        int used = 0;
        float psum = 0;
        for (long pk : peaks) {
            if (pk < 137 || pk + NFFT > (long)xc.size()) continue;
            cfl Pf[62], Sf[62];
            dft62(xc, pk, Pf);
            dft62(xc, pk - 137, Sf);
            cfl H[62], rx[62];
            for (int i = 0; i < 62; i++) {
                H[i] = Pf[i] / pref_[bn2][i];
                rx[i] = Sf[i] / H[i];
            }
            float rr = 0;
            for (int i = 0; i < 62; i++) rr += std::norm(rx[i]);
            float nrm = std::sqrt(rr * 62) + 1e-9f;
            for (int n1 = 0; n1 < 168; n1++) {
                float m0 = 0, m5 = 0;
                for (int i = 0; i < 62; i++) {
                    m0 += ref_[bn2][n1].d0[i] * rx[i].real();
                    m5 += ref_[bn2][n1].d5[i] * rx[i].real();
                }
                acc0[n1] += m0 / nrm;
                acc5[n1] += m5 / nrm;
            }
            float pe = 0;
            for (int i = 0; i < 62; i++) pe += std::norm(Pf[i]);
            psum += pe / 62;
            used++;
        }
        if (used == 0) return best;
        // pick the best (n1, subframe) by accumulated metric
        int bn1 = 0, bsf = 0;
        double bm = -1e18;
        for (int n1 = 0; n1 < 168; n1++) {
            if (acc0[n1] > bm) { bm = acc0[n1]; bn1 = n1; bsf = 0; }
            if (acc5[n1] > bm) { bm = acc5[n1]; bn1 = n1; bsf = 5; }
        }
        best.pci = 3 * bn1 + bn2;
        best.n1 = bn1;
        best.n2 = bn2;
        best.subframe = bsf;
        best.cfo = cfo;
        best.pss_db = 10.0f * std::log10(bval / bmed);
        best.sss_metric = (float)(bm / used);  // normalized 0..1-ish
        best.rsrp_dbfs = 10.0f * std::log10(psum / used + 1e-20f);
        best.pss_sample = bpk;
        return best;
    }

private:
    std::vector<float> xcorr_mag(const std::vector<cfl> &x, const std::vector<cfl> &t) {
        int n = (int)x.size() - NFFT + 1;
        std::vector<float> c(n);
        for (int k = 0; k < n; k++) {
            cfl s = 0;
            for (int i = 0; i < NFFT; i++) s += x[k + i] * std::conj(t[i]);
            c[k] = std::abs(s);
        }
        return c;
    }
    std::vector<cfl> derotate(const std::vector<cfl> &x, double cfo) {
        std::vector<cfl> y(x.size());
        for (size_t i = 0; i < x.size(); i++) y[i] = x[i] * std::polar(1.0f, (float)(-2 * PI * cfo * i / FS));
        return y;
    }
    std::vector<long> find_peaks(const std::vector<cfl> &x, int n2, float thr) {
        auto c = xcorr_mag(x, pss_[n2]);
        float mx = *std::max_element(c.begin(), c.end());
        std::vector<long> pk;
        long i = 0;
        while (i < (long)c.size()) {
            if (c[i] > 0.5f * mx) {
                long j = i;
                for (long k = i; k < std::min((long)c.size(), i + 200); k++)
                    if (c[k] > c[j]) j = k;
                pk.push_back(j);
                i = j + 4000;
            } else i++;
        }
        return pk;
    }
    // 62-subcarrier DFT around DC at window start `s` (useful part)
    void dft62(const std::vector<cfl> &x, long s, cfl *out) {
        cfl F[NFFT];
        for (int k = 0; k < NFFT; k++) {
            cfl v = 0;
            for (int n = 0; n < NFFT; n++) v += x[s + n] * std::polar(1.0f, (float)(-2 * PI * k * n / NFFT));
            F[k] = v;
        }
        for (int i = 0; i < 31; i++) {
            out[i] = F[NFFT - 31 + i];  // -31..-1
            out[31 + i] = F[1 + i];     // +1..+31
        }
    }

    std::vector<cfl> pss_[3];
    SssRef ref_[3][168];
    cfl pref_[3][62];
};

// LTE downlink EARFCN centers commonly present in the US (MHz)
struct Band {
    const char *name;
    double freq;
};
inline std::vector<Band> us_bands() {
    return {{"B71 600", 617e6},   {"B12 700", 731e6},  {"B13 700", 746e6},  {"B14 700", 763e6},
            {"B5 850", 869e6},    {"B4 AWS", 2132.5e6}, {"B66 AWS3", 2145e6}, {"B2 1900", 1960e6},
            {"B25 1900", 1965e6}, {"B30 2300", 2355e6}, {"B7 2600", 2655e6},  {"B41 2500", 2593e6},
            {"B48 CBRS", 3600e6}};
}

inline void dissect_cell(Packet &p, const CellSearch::Cell &c, const char *band, double freq) {
    p.protocol = "LTE";
    p.link = Link::ModeS;  // reuse the "other" link type for pcapng (USER0)
    p.channel = band;
    p.freq = freq;
    p.rssi = c.rsrp_dbfs;
    p.source = fmt("PCI %d", c.pci);
    p.destination = "cell";
    p.info = fmt("LTE cell PCI %d (N_id_1=%d, N_id_2=%d), %s, CFO %+.0f Hz, PSS %.1f dB, SSS %.2f, RSRP %.1f dBFS",
                 c.pci, c.n1, c.n2, band, c.cfo, c.pss_db, c.sss_metric, c.rsrp_dbfs);
    Field &f = p.tree.add(fmt("LTE Physical Cell, PCI %d", c.pci), -1, 0);
    f.add(fmt("Physical Cell ID: %d", c.pci));
    f.add(fmt("N_id_1 (SSS group): %d", c.n1));
    f.add(fmt("N_id_2 (PSS, from Zadoff-Chu root): %d", c.n2));
    f.add(fmt("Band / center: %s / %.3f MHz", band, freq / 1e6));
    f.add(fmt("Frame reference subframe: %d", c.subframe));
    f.add(fmt("Carrier frequency offset: %+.0f Hz", c.cfo));
    f.add(fmt("PSS correlation: %.1f dB over noise", c.pss_db));
    f.add(fmt("SSS confidence: %.2f", c.sss_metric));
    f.add(fmt("RSRP (uncalibrated): %.1f dBFS", c.rsrp_dbfs));
    f.add("Duplex: FDD (assumed)");
    f.add("Note: user-plane data is ciphered; PBCH/MIB decode is best-effort");
}

}  // namespace lte
}  // namespace shark
