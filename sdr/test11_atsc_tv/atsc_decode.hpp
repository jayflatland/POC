#pragma once

// ATSC data decoding: equalized data segments in, MPEG transport stream packets out.
//
//   12 interleaved trellis decoders (8-state Viterbi: precoder + 4-state code)
//   -> 52-branch convolutional deinterleaver
//   -> Reed-Solomon (207,187) decoder, t = 10
//   -> derandomizer -> 188-byte TS packets
//
// Conventions (symbol->encoder->byte mapping, trellis tables, RS roots,
// randomizer) follow ATSC A/53 Part 2; cross-checked against GNU Radio's
// gr-dtv implementation and verified on off-air signals.

#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <vector>

#include "atsc_demod.hpp"

namespace atsc {

static const int PKT_RS = 207;   // bytes per segment / RS codeword
static const int PKT_DATA = 187;  // after removing RS parity (the 0x47 sync is implied)
static const int GROUP_SEGS = 12;
static const int GROUP_BYTES = GROUP_SEGS * PKT_RS;

// -------------------- Reed-Solomon (255,235) shortened to (207,187) --------------------

class ReedSolomon {
public:
    ReedSolomon() {
        int v = 1;
        for (int i = 0; i < 255; i++) {
            exp_[i] = exp_[i + 255] = (uint8_t)v;
            log_[v] = i;
            v <<= 1;
            if (v & 0x100) {
                v ^= 0x11D;
            }
        }
        log_[0] = -1;
    }

    // corrects in place; returns number of corrected bytes, or -1 if uncorrectable
    int decode(uint8_t *block) {
        // syndromes S_j = r(alpha^j), j = 0..19 (first consecutive root 0)
        uint8_t S[NROOTS];
        bool all_zero = true;
        for (int j = 0; j < NROOTS; j++) {
            uint8_t s = 0;
            for (int i = 0; i < PKT_RS; i++) {
                s = (s ? exp_[log_[s] + j] : 0) ^ block[i];
            }
            S[j] = s;
            all_zero &= s == 0;
        }
        if (all_zero) {
            return 0;
        }

        // Berlekamp-Massey: error locator Lambda
        uint8_t lambda[NROOTS + 1] = {1}, b[NROOTS + 1] = {1}, t[NROOTS + 1];
        int L = 0, m = 1;
        uint8_t bb = 1;
        for (int n = 0; n < NROOTS; n++) {
            uint8_t d = S[n];
            for (int i = 1; i <= L; i++) {
                d ^= mul(lambda[i], S[n - i]);
            }
            if (d == 0) {
                m++;
                continue;
            }
            uint8_t coef = div(d, bb);
            std::memcpy(t, lambda, sizeof(t));
            for (int i = 0; i + m <= NROOTS; i++) {
                lambda[i + m] ^= mul(coef, b[i]);
            }
            if (2 * L <= n) {
                L = n + 1 - L;
                std::memcpy(b, t, sizeof(b));
                bb = d;
                m = 1;
            } else {
                m++;
            }
        }
        if (L > NROOTS / 2) {
            return -1;
        }

        // Chien search over the 207 real positions. Position i (0 = first byte) has
        // degree 206 - i in the codeword polynomial; its locator is alpha^(206 - i).
        int pos[NROOTS / 2];
        int found = 0;
        for (int i = 0; i < PKT_RS; i++) {
            int deg = PKT_RS - 1 - i;
            int xinv = (255 - deg) % 255;  // log of X^-1
            uint8_t sum = 0;
            for (int k = 0; k <= L; k++) {
                if (lambda[k]) {
                    sum ^= exp_[(log_[lambda[k]] + xinv * k) % 255];
                }
            }
            if (sum == 0) {
                if (found == NROOTS / 2) {
                    return -1;
                }
                pos[found++] = i;
            }
        }
        if (found != L) {
            return -1;  // roots outside the shortened block: too many errors
        }

        // Forney: error evaluator Omega = S(x) * Lambda(x) mod x^20
        uint8_t omega[NROOTS] = {};
        for (int i = 0; i < NROOTS; i++) {
            uint8_t v = 0;
            for (int k = 0; k <= std::min(i, L); k++) {
                v ^= mul(lambda[k], S[i - k]);
            }
            omega[i] = v;
        }
        for (int e = 0; e < found; e++) {
            int deg = PKT_RS - 1 - pos[e];
            int xinv = (255 - deg) % 255;
            uint8_t num = 0, den = 0;
            for (int i = 0; i < NROOTS; i++) {
                if (omega[i]) {
                    num ^= exp_[(log_[omega[i]] + xinv * i) % 255];
                }
            }
            for (int k = 1; k <= L; k += 2) {  // formal derivative: odd terms
                if (lambda[k]) {
                    den ^= exp_[(log_[lambda[k]] + xinv * (k - 1)) % 255];
                }
            }
            if (den == 0) {
                return -1;
            }
            // with first consecutive root 0: e = X * Omega(X^-1) / Lambda'(X^-1)
            uint8_t mag = mul(exp_[deg % 255], div(num, den));
            block[pos[e]] ^= mag;
        }
        return found;
    }

private:
    static const int NROOTS = 20;
    uint8_t mul(uint8_t a, uint8_t b) const { return (a && b) ? exp_[log_[a] + log_[b]] : 0; }
    uint8_t div(uint8_t a, uint8_t b) const { return a ? exp_[(log_[a] + 255 - log_[b]) % 255] : 0; }
    uint8_t exp_[512];
    int log_[256];
};


// -------------------- data decoder --------------------

struct DecodeStats {
    long packets = 0, corrected = 0, failed = 0, corrected_bytes = 0;
};

class DataDecoder {
public:
    using PacketCb = std::function<void(const uint8_t *ts188, bool ok)>;

    DataDecoder() {
        build_symbol_map();
        for (int e = 0; e < 12; e++) {
            vit_[e].reset();
        }
        for (int b = 0; b < 52; b++) {
            fifo_[b].assign((51 - b) * 4, 0);
            fifo_pos_[b] = 0;
        }
        align_.assign(156, 0);
        // the randomizer sequence for a whole field (it resets at each field start)
        rand_.resize(312 * PKT_DATA);
        unsigned st = 0x018f;
        static const unsigned taps[8] = {0x8000, 0x2000, 0x1000, 0x0200, 0x0020, 0x0010, 0x0008, 0x0004};
        for (auto &r : rand_) {
            uint8_t o = 0;
            for (int bit = 0; bit < 8; bit++) {
                if (st & taps[bit]) {
                    o |= 1 << bit;
                }
            }
            r = o;
            st = (st & 1) ? (((st ^ 0xa638) >> 1) | 0x8000) : (st >> 1);
        }
    }

    const DecodeStats &stats() const { return stats_; }

    // one equalized data segment; seg_index is 0..311 within the field
    void push_segment(int seg_index, const float *sym, const PacketCb &cb) {
        // a gap in the segment sequence: start over
        if (expect_ >= 0 && seg_index != expect_) {
            reset();
        }
        expect_ = (seg_index + 1) % 312;
        if (groups_.empty() && seg_index % GROUP_SEGS != 0) {
            return;  // wait for a group boundary
        }
        if (seg_index % GROUP_SEGS == 0) {
            Group g;
            g.first_seg = seg_index;
            g.bytes.fill(0);
            g.remaining = GROUP_SEGS * DATA_SYMS;
            groups_.push_back(g);
            group_serial_++;
        }
        int sg = seg_index % GROUP_SEGS;
        long gid = group_serial_;
        for (int i = 0; i < DATA_SYMS; i++) {
            const SymbolMap &m = map_[sg][i];
            vit_[m.encoder].step(sym[i], gid, m.byte, m.shift, *this);
        }
        flush_groups(cb);
    }

private:
    struct SymbolMap {
        uint8_t encoder;
        uint16_t byte;
        uint8_t shift;
    };

    struct Group {
        int first_seg = 0;
        std::array<uint8_t, GROUP_BYTES> bytes;
        int remaining = 0;
    };

    // mirrors the A/53 trellis encoder's chunk loading and per-segment encoder bump
    void build_symbol_map() {
        int encoder = 12 - 4, skip = 0, out = 0, next = 0;
        int wherefrom[12] = {};
        for (int chunk = 0; chunk < GROUP_BYTES; chunk += 12) {
            if (out >= next) {
                encoder = (encoder + 4) % 12;
                skip = 1;
            }
            for (int i = 0; i < 12; i++) {
                wherefrom[encoder] = chunk + i;
                encoder = (encoder + 1) % 12;
            }
            for (int shift = 6; shift >= 0; shift -= 2) {
                if (out >= next) {
                    out += 4;
                    next = out + DATA_SYMS;
                    if (!skip) {
                        encoder = (encoder + 4) % 12;
                    }
                    skip = 0;
                }
                for (int i = 0; i < 12; i++) {
                    int s = out / SEG_SYMS, p = out % SEG_SYMS - 4;
                    map_[s][p] = {(uint8_t)encoder, (uint16_t)wherefrom[encoder], (uint8_t)shift};
                    out++;
                    encoder = (encoder + 1) % 12;
                }
            }
        }
    }

    // streaming Viterbi for one of the 12 trellis encoders, with chunked traceback
    struct Viterbi {
        static const int DEPTH = 48, CHUNK = 32, RING = 128;
        static constexpr uint8_t NEXT[32] = {0, 1, 4, 5, 2, 3, 6, 7, 1, 0, 5, 4, 3, 2, 7, 6,
                                             4, 5, 0, 1, 6, 7, 2, 3, 5, 4, 1, 0, 7, 6, 3, 2};
        static constexpr uint8_t OUT[32] = {0, 2, 4, 6, 1, 3, 5, 7, 0, 2, 4, 6, 1, 3, 5, 7,
                                            4, 6, 0, 2, 5, 7, 1, 3, 4, 6, 0, 2, 5, 7, 1, 3};
        float metric[8];
        uint8_t prev[RING][8], dibit[RING][8];
        long dest_group[RING];
        uint16_t dest_byte[RING];
        uint8_t dest_shift[RING];
        long n = 0;

        void reset() {
            std::fill(metric, metric + 8, 0.0f);
            n = 0;
        }

        void step(float y, long gid, int byte, int shift, DataDecoder &dd) {
            float cost[8];
            for (int l = 0; l < 8; l++) {
                float d = y - (2.0f * l - 7.0f);
                cost[l] = d * d;
            }
            float nm[8] = {1e30f, 1e30f, 1e30f, 1e30f, 1e30f, 1e30f, 1e30f, 1e30f};
            int r = (int)(n % RING);
            for (int k = 0; k < 32; k++) {
                int s = k >> 2;
                float c = metric[s] + cost[OUT[k]];
                int ns = NEXT[k];
                if (c < nm[ns]) {
                    nm[ns] = c;
                    prev[r][ns] = (uint8_t)s;
                    dibit[r][ns] = (uint8_t)(k & 3);
                }
            }
            float mn = *std::min_element(nm, nm + 8);
            for (int s = 0; s < 8; s++) {
                metric[s] = nm[s] - mn;
            }
            dest_group[r] = gid;
            dest_byte[r] = (uint16_t)byte;
            dest_shift[r] = (uint8_t)shift;
            n++;
            if (n >= DEPTH + CHUNK && n % CHUNK == 0) {
                traceback(dd, false);
            }
        }

        // decide steps [n - DEPTH - CHUNK, n - DEPTH), or everything left if final
        void traceback(DataDecoder &dd, bool final) {
            int s = (int)(std::min_element(metric, metric + 8) - metric);
            long t = n - 1;
            long out_end = final ? n : n - DEPTH;
            long out_begin = final ? std::max(0L, n - DEPTH - CHUNK) : n - DEPTH - CHUNK;
            for (; t >= out_begin; t--) {
                int r = (int)(t % RING);
                if (t < out_end) {
                    dd.put_dibit(dest_group[r], dest_byte[r], dest_shift[r], dibit[r][s]);
                }
                s = prev[r][s];
            }
        }
    };

    void put_dibit(long gid, int byte, int shift, int dib) {
        long first = group_serial_ - (long)groups_.size() + 1;
        long k = gid - first;
        if (k < 0 || k >= (long)groups_.size()) {
            return;
        }
        Group &g = groups_[k];
        g.bytes[byte] |= (uint8_t)(dib << shift);
        g.remaining--;
    }

    void flush_groups(const PacketCb &cb) {
        while (!groups_.empty() && groups_.front().remaining <= 0) {
            Group &g = groups_.front();
            for (int s = 0; s < GROUP_SEGS; s++) {
                deinterleave_segment(g.first_seg + s, &g.bytes[s * PKT_RS], cb);
            }
            groups_.pop_front();
        }
        // groups that never complete (decoder lagging after a reset) shouldn't pile up
        while (groups_.size() > 4) {
            groups_.pop_front();
        }
    }

    void deinterleave_segment(int seg_index, const uint8_t *in, const PacketCb &cb) {
        if (seg_index == 0) {
            commutator_ = 0;  // re-synced at every field start
        }
        uint8_t out[PKT_RS];
        for (int j = 0; j < PKT_RS; j++) {
            int b = commutator_;
            commutator_ = (commutator_ + 1) % 52;
            uint8_t v = in[j];
            if (!fifo_[b].empty()) {
                uint8_t o = fifo_[b][fifo_pos_[b]];
                fifo_[b][fifo_pos_[b]] = v;
                fifo_pos_[b] = (fifo_pos_[b] + 1) % (int)fifo_[b].size();
                v = o;
            }
            // 156-byte alignment so packets come out on segment boundaries (52 segments late)
            uint8_t a = align_[align_pos_];
            align_[align_pos_] = v;
            align_pos_ = (align_pos_ + 1) % 156;
            out[j] = a;
        }
        if (++warmup_ <= 53) {
            return;  // deinterleaver still filling
        }
        int field_pkt = ((seg_index - 52) % 312 + 312) % 312;
        int fixed = rs_.decode(out);
        stats_.packets++;
        uint8_t ts[188];
        ts[0] = 0x47;
        for (int i = 0; i < PKT_DATA; i++) {
            ts[i + 1] = out[i] ^ rand_[field_pkt * PKT_DATA + i];
        }
        bool ok = fixed >= 0;
        if (!ok) {
            stats_.failed++;
            ts[1] |= 0x80;  // transport_error_indicator
        } else if (fixed > 0) {
            stats_.corrected++;
            stats_.corrected_bytes += fixed;
        }
        cb(ts, ok);
    }

    void reset() {
        for (int e = 0; e < 12; e++) {
            vit_[e].reset();
        }
        groups_.clear();
        warmup_ = 0;
        expect_ = -1;
    }

    SymbolMap map_[GROUP_SEGS][DATA_SYMS];
    Viterbi vit_[12];
    std::deque<Group> groups_;
    long group_serial_ = 0;
    int expect_ = -1;
    std::vector<uint8_t> fifo_[52];
    int fifo_pos_[52];
    std::vector<uint8_t> align_;
    int align_pos_ = 0, commutator_ = 0;
    long warmup_ = 0;
    ReedSolomon rs_;
    std::vector<uint8_t> rand_;
    DecodeStats stats_;
};

}  // namespace atsc
