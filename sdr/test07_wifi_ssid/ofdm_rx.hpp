#pragma once

// 802.11a/g (legacy OFDM, 20 MHz) receiver, all rates 6-54 Mbps.
//
// detect (STF autocorrelation) -> coarse CFO -> LTF timing + fine CFO
// -> channel estimate -> SIGNAL field -> data symbols with pilot tracking
// -> soft demap -> deinterleave -> depuncture -> Viterbi -> descramble

#include <cmath>
#include <cstring>
#include <fftw3.h>

#include "phy_common.hpp"
#include "viterbi.hpp"

class OfdmRx {
public:
    static constexpr double MAX_CFO = 250e3;  // +/-20 ppm on both ends at 5.8 GHz is ~232 kHz


    OfdmRx() {
        in_ = fftwf_alloc_complex(64);
        out_ = fftwf_alloc_complex(64);
        plan_ = fftwf_plan_dft_1d(64, in_, out_, FFTW_FORWARD, FFTW_ESTIMATE);

        static const int L[53] = {
            1, 1, -1, -1, 1, 1, -1, 1, -1, 1, 1, 1, 1, 1, 1, -1, -1, 1, 1, -1, 1, -1, 1, 1, 1, 1,
            0,
            1, -1, -1, 1, 1, -1, 1, -1, 1, -1, -1, -1, -1, -1, 1, 1, -1, -1, 1, -1, 1, -1, 1, 1, 1, 1};
        for (int k = -26; k <= 26; k++) {
            ltf_[k + 32] = (float)L[k + 26];
        }
        // time-domain LTF symbol, for timing
        for (int n = 0; n < 64; n++) {
            cf s = 0.0f;
            for (int k = -26; k <= 26; k++) {
                s += ltf_[k + 32] * std::polar(1.0f, (float)(2.0 * PI * k * n / 64.0));
            }
            ltf_time_[n] = s / 8.0f;
        }
        // pilot polarity: scrambler output with all-ones seed, 0 -> +1, 1 -> -1
        int st = 0x7F;
        for (int i = 0; i < 127; i++) {
            int s = ((st >> 6) ^ (st >> 3)) & 1;
            st = ((st << 1) | s) & 0x7F;
            polarity_[i] = s ? -1.0f : 1.0f;
        }
        for (int k = -26; k <= 26; k++) {
            if (k != 0 && std::abs(k) != 7 && std::abs(k) != 21) {
                data_sc_.push_back(k);
            }
        }
    }

    ~OfdmRx() {
        fftwf_destroy_plan(plan_);
        fftwf_free(in_);
        fftwf_free(out_);
    }

    const float *polarity() const { return polarity_; }

    void process(const std::vector<cf> &x, double fs, const FrameCallback &cb, RxStats &stats) {
        x_ = &x;
        fs_ = fs;
        size_t n_total = x.size();
        if (n_total < 4000) {
            return;
        }
        float pthr = median_power(x, 256) * 48.0f * 4.0f;  // ~6 dB over the noise floor

        cf c;
        float p;
        auto init_sums = [&](size_t at) {
            c = 0.0f;
            p = 0.0f;
            for (size_t k = at; k < at + 48; k++) {
                c += x[k] * std::conj(x[k + 16]);
                p += std::norm(x[k + 16]);
            }
        };

        size_t n = 0;
        int plateau = 0;
        init_sums(0);
        while (n + 64 + 1 < n_total) {
            if (std::abs(c) > 0.7f * p && p > pthr) {
                plateau++;
            } else {
                plateau = 0;
            }
            // narrowband signals (Bluetooth, etc.) also autocorrelate; a real STF has a small CFO
            double cfo = 0.0;
            if (plateau >= 48) {
                cfo = -std::arg(c) * fs / (2.0 * PI * 16.0);
                if (std::abs(cfo) > MAX_CFO) {
                    plateau = 0;
                }
            }
            if (plateau >= 48) {
                stats.ofdm_detect++;
                size_t next = try_decode(n - 47, cfo, cb, stats);
                n = std::max(n + 1, next);
                plateau = 0;
                if (n + 64 + 1 >= n_total) {
                    break;
                }
                init_sums(n);
                continue;
            }
            c -= x[n] * std::conj(x[n + 16]);
            p -= std::norm(x[n + 16]);
            c += x[n + 48] * std::conj(x[n + 64]);
            p += std::norm(x[n + 64]);
            n++;
            if ((n & 4095) == 0) {
                init_sums(n);  // avoid drift in the running sums
            }
        }
    }

private:
    struct Rate {
        int code, mbps, nbpsc, ncbps, ndbps, coding;  // coding: 2 = 1/2, 3 = 2/3, 4 = 3/4
    };

    static const Rate *lookup_rate(int code) {
        static const Rate rates[] = {
            {13, 6, 1, 48, 24, 2}, {15, 9, 1, 48, 36, 4}, {5, 12, 2, 96, 48, 2}, {7, 18, 2, 96, 72, 4},
            {9, 24, 4, 192, 96, 2}, {11, 36, 4, 192, 144, 4}, {1, 48, 6, 288, 192, 3}, {3, 54, 6, 288, 216, 4}};
        for (auto &r : rates) {
            if (r.code == code) {
                return &r;
            }
        }
        return nullptr;
    }

    // 64-point FFT of x[start..start+63] after removing CFO; returns bins in natural order
    void fft_at(size_t start, double cfo, cf *Y) {
        const auto &x = *x_;
        double w = -2.0 * PI * cfo / fs_;
        double ph = std::fmod(w * (double)start, 2.0 * PI);
        for (int k = 0; k < 64; k++) {
            cf v = x[start + k] * std::polar(1.0f, (float)(ph + w * k));
            in_[k][0] = v.real();
            in_[k][1] = v.imag();
        }
        fftwf_execute(plan_);
        for (int k = 0; k < 64; k++) {
            Y[k] = cf(out_[k][0], out_[k][1]);
        }
    }

    static cf bin(const cf *Y, int k) { return Y[(k + 64) % 64]; }

    // equalize, then remove common phase and phase slope using the 4 pilots
    void equalize(const cf *Y, int sym_index, cf *eq) {
        static const int pk[4] = {-21, -7, 7, 21};
        static const float pv[4] = {1, 1, 1, -1};
        float pol = polarity_[sym_index % 127];
        cf yp[4];
        cf sum = 0.0f;
        for (int i = 0; i < 4; i++) {
            yp[i] = bin(Y, pk[i]) / H_[pk[i] + 32] * (pv[i] * pol);
            sum += yp[i];
        }
        float ph0 = std::arg(sum);
        float num = 0.0f, den = 0.0f;
        for (int i = 0; i < 4; i++) {
            float d = std::arg(yp[i] * std::polar(1.0f, -ph0));
            num += pk[i] * d;
            den += pk[i] * pk[i];
        }
        float slope = num / den;
        for (int k = -26; k <= 26; k++) {
            if (k == 0) {
                continue;
            }
            eq[k + 32] = bin(Y, k) / H_[k + 32] * std::polar(1.0f, -(ph0 + slope * k));
        }
    }

    // soft bits for one symbol, in transmitted (interleaved) order; > 0 means 1
    void demap(const cf *eq, int nbpsc, std::vector<float> &soft) {
        for (int k : data_sc_) {
            cf y = eq[k + 32];
            float w = csi_[k + 32];
            if (nbpsc == 1) {
                soft.push_back(y.real() * w);
                continue;
            }
            float axes[2] = {y.real(), y.imag()};
            for (float v : axes) {
                if (nbpsc == 2) {
                    soft.push_back(v * w);
                } else if (nbpsc == 4) {
                    v *= std::sqrt(10.0f);
                    soft.push_back(v * w);
                    soft.push_back((2.0f - std::abs(v)) * w);
                } else {
                    v *= std::sqrt(42.0f);
                    soft.push_back(v * w);
                    soft.push_back((4.0f - std::abs(v)) * w);
                    soft.push_back((2.0f - std::abs(std::abs(v) - 4.0f)) * w);
                }
            }
        }
    }

    static void deinterleave(const float *in, int ncbps, int nbpsc, float *out) {
        int s = std::max(nbpsc / 2, 1);
        for (int k = 0; k < ncbps; k++) {
            int i = (ncbps / 16) * (k % 16) + k / 16;
            int j = s * (i / s) + (i + ncbps - (16 * i) / ncbps) % s;
            out[k] = in[j];
        }
    }

    size_t try_decode(size_t n0, double cfo, const FrameCallback &cb, RxStats &stats) {
        const auto &x = *x_;
        const size_t search = 320;
        if (n0 + search + 128 + 80 >= x.size()) {
            return x.size();
        }
        // LTF timing: pair of correlation peaks 64 samples apart
        double w = -2.0 * PI * cfo / fs_;
        std::vector<cf> seg(search + 128);
        for (size_t i = 0; i < seg.size(); i++) {
            seg[i] = x[n0 + i] * std::polar(1.0f, (float)std::fmod(w * (double)(n0 + i), 2.0 * PI));
        }
        std::vector<float> a(search + 64);
        for (size_t t = 0; t < a.size(); t++) {
            cf s = 0.0f;
            for (int k = 0; k < 64; k++) {
                s += seg[t + k] * std::conj(ltf_time_[k]);
            }
            a[t] = std::abs(s);
        }
        size_t best_t = 0;
        float best = -1.0f;
        for (size_t t = 0; t < search; t++) {
            float v = a[t] + a[t + 64];
            if (v > best) {
                best = v;
                best_t = t;
            }
        }
        size_t ltf1 = n0 + best_t;
        size_t end = decode_frame(ltf1, cfo, cb, stats);
        return end ? end : n0 + 160;
    }

    // returns the index just past the frame, or 0 on failure
    size_t decode_frame(size_t ltf1, double cfo, const FrameCallback &cb, RxStats &stats) {
        const auto &x = *x_;
        const int B = 3;  // start each FFT window a few samples into the guard interval

        // fine CFO from the two LTF repetitions
        {
            double w = -2.0 * PI * cfo / fs_;
            cf acc = 0.0f;
            for (int k = 0; k < 64; k++) {
                cf a = x[ltf1 + k] * std::polar(1.0f, (float)std::fmod(w * (double)(ltf1 + k), 2.0 * PI));
                cf b = x[ltf1 + 64 + k] * std::polar(1.0f, (float)std::fmod(w * (double)(ltf1 + 64 + k), 2.0 * PI));
                acc += a * std::conj(b);
            }
            cfo += -std::arg(acc) * fs_ / (2.0 * PI * 64.0);
        }

        // channel estimate
        cf Y1[64], Y2[64];
        fft_at(ltf1 - B, cfo, Y1);
        fft_at(ltf1 + 64 - B, cfo, Y2);
        float hsum = 0.0f;
        for (int k = -26; k <= 26; k++) {
            if (k == 0) {
                continue;
            }
            H_[k + 32] = 0.5f * (bin(Y1, k) + bin(Y2, k)) / ltf_[k + 32];
            hsum += std::norm(H_[k + 32]);
        }
        if (hsum <= 0.0f) {
            return 0;
        }
        for (int k = -26; k <= 26; k++) {
            csi_[k + 32] = std::norm(H_[k + 32]) / (hsum / 52.0f);
        }

        // SIGNAL field: BPSK, rate 1/2, 24 bits
        cf Y[64], eq[64];
        fft_at(ltf1 + 128 + 16 - B, cfo, Y);
        equalize(Y, 0, eq);
        std::vector<float> soft;
        demap(eq, 1, soft);
        float dint[48];
        deinterleave(soft.data(), 48, 1, dint);
        auto sig = viterbi_decode(std::vector<float>(dint, dint + 48), 24);

        int parity = 0;
        for (int i = 0; i < 18; i++) {
            parity ^= sig[i];
        }
        int tail = 0;
        for (int i = 18; i < 24; i++) {
            tail |= sig[i];
        }
        int code = (sig[0] << 3) | (sig[1] << 2) | (sig[2] << 1) | sig[3];
        int length = 0;
        for (int i = 0; i < 12; i++) {
            length |= sig[5 + i] << i;
        }
        const Rate *rate = lookup_rate(code);
        if (parity || tail || !rate || length < 14) {
            return 0;
        }
        stats.ofdm_signal_ok++;

        int nbits = 16 + 8 * length + 6;
        int nsym = (nbits + rate->ndbps - 1) / rate->ndbps;
        size_t end = ltf1 + 128 + 80 + (size_t)nsym * 80;
        if (end + 16 > x.size()) {
            return 0;
        }

        // data symbols
        std::vector<float> coded;
        coded.reserve((size_t)nsym * rate->ncbps);
        std::vector<float> sym_soft;
        std::vector<float> sym_dint(rate->ncbps);
        for (int s = 0; s < nsym; s++) {
            fft_at(ltf1 + 128 + 80 * (s + 1) + 16 - B, cfo, Y);
            equalize(Y, s + 1, eq);
            sym_soft.clear();
            demap(eq, rate->nbpsc, sym_soft);
            deinterleave(sym_soft.data(), rate->ncbps, rate->nbpsc, sym_dint.data());
            coded.insert(coded.end(), sym_dint.begin(), sym_dint.end());
        }

        // depuncture back to rate 1/2 (erasures = 0)
        std::vector<float> mother;
        mother.reserve((size_t)nsym * rate->ndbps * 2);
        if (rate->coding == 2) {
            mother = coded;
        } else if (rate->coding == 3) {
            for (size_t i = 0; i + 3 <= coded.size(); i += 3) {
                mother.insert(mother.end(), {coded[i], coded[i + 1], coded[i + 2], 0.0f});
            }
        } else {
            for (size_t i = 0; i + 4 <= coded.size(); i += 4) {
                mother.insert(mother.end(), {coded[i], coded[i + 1], coded[i + 2], 0.0f, 0.0f, coded[i + 3]});
            }
        }
        size_t ndata = (size_t)nsym * rate->ndbps;
        if (mother.size() < 2 * ndata) {
            return 0;
        }
        auto bits = viterbi_decode(mother, ndata);

        // descramble: the first 7 SERVICE bits are zero, so they reveal the scrambler sequence
        std::vector<uint8_t> s(bits.size());
        for (size_t i = 0; i < bits.size(); i++) {
            s[i] = i < 7 ? bits[i] : (s[i - 7] ^ s[i - 4]);
            bits[i] ^= s[i];
        }

        RxFrame frame;
        frame.psdu.resize(length);
        for (int i = 0; i < length; i++) {
            uint8_t v = 0;
            for (int b = 0; b < 8; b++) {
                v |= bits[16 + 8 * i + b] << b;
            }
            frame.psdu[i] = v;
        }
        frame.phy = "OFDM " + std::to_string(rate->mbps) + "M";
        frame.power_db = (float)power_dbfs(x, ltf1, 128);
        cb(frame);
        return end;
    }

    const std::vector<cf> *x_ = nullptr;
    double fs_ = 20e6;
    fftwf_complex *in_, *out_;
    fftwf_plan plan_;
    float ltf_[64] = {};
    cf ltf_time_[64];
    float polarity_[127];
    std::vector<int> data_sc_;
    cf H_[64];
    float csi_[64];
};
