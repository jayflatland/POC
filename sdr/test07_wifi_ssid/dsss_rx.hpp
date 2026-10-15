#pragma once

// 802.11b DSSS receiver for 1 Mbps (DBPSK) and 2 Mbps (DQPSK), long and short preamble.
// CCK rates (5.5/11 Mbps) aren't decoded; beacons go out at 1 Mbps.
//
// At 20 MS/s one 1 us symbol is exactly 20 samples, so the 11-chip Barker
// code is matched-filtered with a 20-sample template.
//
// Barker correlation -> symbol timing lock -> differential demod
// -> self-synchronizing descrambler -> SFD -> PLCP header (CRC-16) -> PSDU

#include <cmath>

#include "phy_common.hpp"

class DsssRx {
public:
    DsssRx() {
        static const float barker[11] = {1, -1, 1, 1, -1, 1, 1, 1, -1, -1, -1};
        for (int k = 0; k < 20; k++) {
            tmpl_[k] = barker[k * 11 / 20];
        }
    }

    void process(const std::vector<cf> &x, double fs, const FrameCallback &cb, RxStats &stats) {
        if (std::abs(fs - 20e6) > 1.0 || x.size() < 4000) {
            return;
        }
        x_ = &x;
        size_t m_total = x.size() - 20;
        corr_.resize(m_total);
        mag_.resize(m_total);
        for (size_t n = 0; n < m_total; n++) {
            cf s = 0.0f;
            for (int k = 0; k < 20; k++) {
                s += x[n + k] * tmpl_[k];
            }
            corr_[n] = s;
            mag_[n] = std::norm(s);
        }

        std::vector<float> samp;
        for (size_t n = 0; n < m_total; n += 101) {
            samp.push_back(mag_[n]);
        }
        std::nth_element(samp.begin(), samp.begin() + samp.size() / 2, samp.end());
        float thr = samp[samp.size() / 2] * 10.0f;

        size_t n = 0;
        while (n + 800 < m_total) {
            // fold 20 symbols of correlation energy by timing offset
            float e[20] = {};
            for (int s = 0; s < 20; s++) {
                for (int o = 0; o < 20; o++) {
                    e[o] += mag_[n + 20 * s + o];
                }
            }
            int bo = 0;
            float mean = 0.0f;
            for (int o = 0; o < 20; o++) {
                mean += e[o] / 20.0f;
                if (e[o] > e[bo]) {
                    bo = o;
                }
            }
            if (e[bo] > 3.0f * mean && e[bo] / 20.0f > thr) {
                stats.dsss_lock++;
                n = std::max(n + 400, decode_from(n + bo, cb, stats));
            } else {
                n += 400;
            }
        }
    }

private:
    int descramble(int r) {
        int d = r ^ ((hist_ >> 3) & 1) ^ ((hist_ >> 6) & 1);
        hist_ = ((hist_ << 1) | r) & 0x7F;
        return d;
    }

    // move to the next symbol, re-centering on the strongest of 3 neighboring samples
    bool next_symbol(size_t &pos, cf &out) {
        pos += 20;
        if (pos + 2 >= mag_.size()) {
            return false;
        }
        size_t best = pos;
        if (mag_[pos - 1] > mag_[best]) {
            best = pos - 1;
        }
        if (mag_[pos + 1] > mag_[best]) {
            best = pos + 1;
        }
        pos = best;
        out = corr_[pos];
        if (mag_[pos] < 0.05f * level_) {
            if (++weak_ > 6) {
                return false;
            }
        } else {
            weak_ = 0;
            level_ = 0.95f * level_ + 0.05f * mag_[pos];
        }
        return true;
    }

    static uint16_t crc16_header(const int *bits) {
        uint16_t crc = 0xFFFF;
        for (int i = 0; i < 32; i++) {
            int fb = ((crc >> 15) & 1) ^ bits[i];
            crc = (uint16_t)(crc << 1);
            if (fb) {
                crc ^= 0x1021;
            }
        }
        return crc ^ 0xFFFF;
    }

    size_t decode_from(size_t pos, const FrameCallback &cb, RxStats &stats) {
        size_t start = pos;
        hist_ = 0;
        weak_ = 0;
        level_ = mag_[pos];
        cf prev = corr_[pos];
        cf acc = 0.0f;
        cf rot = 1.0f;
        uint16_t reg = 0;
        bool short_preamble = false;
        bool found = false;

        // SYNC + SFD at 1 Mbps DBPSK
        for (int sym = 0; sym < 220 && !found; sym++) {
            cf cur;
            if (!next_symbol(pos, cur)) {
                return pos;
            }
            cf d = cur * std::conj(prev);
            prev = cur;
            acc += d * d;  // squaring removes the BPSK data, leaving the per-symbol CFO rotation
            rot = std::polar(1.0f, -std::arg(acc) / 2.0f);
            int r = (d * rot).real() < 0.0f;
            reg = (uint16_t)((reg << 1) | descramble(r));
            if (sym >= 15) {
                if (reg == 0x05CF) {
                    found = true;
                } else if (reg == 0xF3A0) {
                    found = true;
                    short_preamble = true;
                }
            }
        }
        if (!found) {
            return pos;
        }
        stats.dsss_sfd++;

        auto dbpsk = [&](int &bit) {
            cf cur;
            if (!next_symbol(pos, cur)) {
                return false;
            }
            cf d = cur * std::conj(prev) * rot;
            prev = cur;
            bit = descramble(d.real() < 0.0f);
            return true;
        };
        auto dqpsk = [&](int &b0, int &b1) {
            cf cur;
            if (!next_symbol(pos, cur)) {
                return false;
            }
            cf d = cur * std::conj(prev) * rot;
            prev = cur;
            int q = ((int)std::lround(std::arg(d) / (PI / 2.0)) + 4) % 4;
            static const int dibit[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
            b0 = descramble(dibit[q][0]);
            b1 = descramble(dibit[q][1]);
            return true;
        };

        // PLCP header: 1 Mbps for long preamble, 2 Mbps for short
        int h[48];
        for (int i = 0; i < 48;) {
            if (short_preamble) {
                if (!dqpsk(h[i], h[i + 1])) {
                    return pos;
                }
                i += 2;
            } else {
                if (!dbpsk(h[i])) {
                    return pos;
                }
                i += 1;
            }
        }
        int signal = 0, length = 0;
        for (int i = 0; i < 8; i++) {
            signal |= h[i] << i;
        }
        for (int i = 0; i < 16; i++) {
            length |= h[16 + i] << i;
        }
        uint16_t crc = crc16_header(h);
        uint16_t rx_msb = 0, rx_lsb = 0;
        for (int i = 0; i < 16; i++) {
            rx_msb |= h[32 + i] << (15 - i);
            rx_lsb |= h[32 + i] << i;
        }
        if (crc != rx_msb && crc != rx_lsb) {
            stats.dsss_header_bad++;
            return pos;
        }
        stats.dsss_header_ok++;

        bool two_mbps;
        if (signal == 0x0A) {
            two_mbps = false;
        } else if (signal == 0x14) {
            two_mbps = true;
        } else {
            return pos + 20 * (size_t)length;  // CCK, skip it
        }
        int bytes = two_mbps ? length / 4 : length / 8;
        if (bytes < 14 || bytes > 2400) {
            return pos;
        }

        std::vector<int> bits;
        bits.reserve(bytes * 8);
        while ((int)bits.size() < bytes * 8) {
            if (two_mbps) {
                int b0, b1;
                if (!dqpsk(b0, b1)) {
                    return pos;
                }
                bits.push_back(b0);
                bits.push_back(b1);
            } else {
                int b;
                if (!dbpsk(b)) {
                    return pos;
                }
                bits.push_back(b);
            }
        }

        RxFrame frame;
        frame.psdu.resize(bytes);
        for (int i = 0; i < bytes; i++) {
            uint8_t v = 0;
            for (int b = 0; b < 8; b++) {
                v |= bits[8 * i + b] << b;
            }
            frame.psdu[i] = v;
        }
        frame.phy = std::string("DSSS ") + (two_mbps ? "2M" : "1M") + (short_preamble ? " short" : "");
        frame.power_db = (float)power_dbfs(*x_, start, pos - start);
        cb(frame);
        return pos;
    }

    float tmpl_[20];
    const std::vector<cf> *x_ = nullptr;
    std::vector<cf> corr_;
    std::vector<float> mag_;
    int hist_ = 0;
    int weak_ = 0;
    float level_ = 0.0f;
};
