#pragma once

#include <cstdint>
#include <vector>

// 802.11 convolutional code: K=7, rate 1/2, g0 = 133, g1 = 171 (octal).
// Encoder register: bit 6 = current input, bit 0 = oldest.

inline int parity7(int v) {
    return __builtin_parity(v & 0x7F);
}

inline std::vector<uint8_t> conv_encode(const std::vector<uint8_t> &bits) {
    std::vector<uint8_t> out;
    int state = 0;
    for (auto b : bits) {
        int reg = (b << 6) | state;
        out.push_back(parity7(reg & 0133));
        out.push_back(parity7(reg & 0171));
        state = reg >> 1;
    }
    return out;
}

// soft[] has 2 values per input bit (A then B): > 0 means 1, < 0 means 0, 0 = erased.
inline std::vector<uint8_t> viterbi_decode(const std::vector<float> &soft, size_t nbits) {
    static uint8_t out_a[128], out_b[128];
    static bool init = false;
    if (!init) {
        for (int r = 0; r < 128; r++) {
            out_a[r] = parity7(r & 0133);
            out_b[r] = parity7(r & 0171);
        }
        init = true;
    }

    std::vector<float> metric(64, -1e30f), next(64);
    metric[0] = 0.0f;
    std::vector<uint8_t> decisions(nbits * 64);

    for (size_t i = 0; i < nbits; i++) {
        float sa = soft[2 * i];
        float sb = soft[2 * i + 1];
        float best_all = -1e30f;
        for (int ns = 0; ns < 64; ns++) {
            int input = ns >> 5;
            int base = (ns & 0x1F) << 1;
            float best = -1e30f;
            int pick = 0;
            for (int lb = 0; lb < 2; lb++) {
                int ps = base | lb;
                int reg = (input << 6) | ps;
                float m = metric[ps] + (out_a[reg] ? sa : -sa) + (out_b[reg] ? sb : -sb);
                if (m > best) {
                    best = m;
                    pick = lb;
                }
            }
            next[ns] = best;
            decisions[i * 64 + ns] = pick;
            if (best > best_all) {
                best_all = best;
            }
        }
        for (int s = 0; s < 64; s++) {
            metric[s] = next[s] - best_all;  // keep metrics from growing
        }
    }

    int s = 0;
    for (int k = 1; k < 64; k++) {
        if (metric[k] > metric[s]) {
            s = k;
        }
    }
    std::vector<uint8_t> bits(nbits);
    for (size_t i = nbits; i-- > 0;) {
        bits[i] = s >> 5;
        s = ((s & 0x1F) << 1) | decisions[i * 64 + s];
    }
    return bits;
}
