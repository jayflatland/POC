#pragma once

// Bluetooth Low Energy advertising receiver and dissector.
//
// One 32 MS/s capture centered at 2414 MHz holds advertising channels 37
// (2402 MHz) and 38 (2426 MHz). An overlap-save FFT channelizer pulls each
// out at 4 MS/s; then GFSK discriminator -> preamble + access address search
// -> dewhitening -> CRC-24.

#include <cmath>
#include <complex>
#include <cstdarg>
#include <functional>
#include <vector>

#include <fftw3.h>

#include "packet.hpp"

namespace shark {

using cfloat = std::complex<float>;

// overlap-save: input at fs, any number of output channels at fs/8, each centered
// on a bin offset that's a multiple of 8 (keeps phase continuous across blocks)
class Channelizer {
public:
    static const int N = 8192, D = 8, M = 1024, HOP = N - M;

    void init(double fs, const std::vector<double> &offsets_hz, double half_bw_hz) {
        fs_ = fs;
        in_ = fftwf_alloc_complex(N);
        spec_ = fftwf_alloc_complex(N);
        fwd_ = fftwf_plan_dft_1d(N, in_, spec_, FFTW_FORWARD, FFTW_MEASURE);
        sub_in_ = fftwf_alloc_complex(N / D);
        sub_out_ = fftwf_alloc_complex(N / D);
        inv_ = fftwf_plan_dft_1d(N / D, sub_in_, sub_out_, FFTW_BACKWARD, FFTW_MEASURE);
        for (double off : offsets_hz) {
            int k = (int)std::lround(off / (fs / N) / D) * D;
            bins_.push_back(k);
        }
        // raised-cosine edged passband, in units of output bins
        win_.resize(N / D);
        double df = fs / N;
        for (int i = 0; i < N / D; i++) {
            int k = i < N / D / 2 ? i : i - N / D;
            double f = std::abs(k * df);
            double edge = 150e3;
            double v = f < half_bw_hz - edge ? 1.0 : f > half_bw_hz + edge ? 0.0 : 0.5 + 0.5 * std::cos(M_PI * (f - half_bw_hz + edge) / (2 * edge));
            win_[i] = (float)v / N;
        }
        hist_.assign(M, cfloat(0, 0));
    }

    // calls out(channel_index, samples, count) with fs/8 output
    template <typename Out>
    void process(const cfloat *x, size_t n, Out out) {
        for (size_t i = 0; i < n; i++) {
            pending_.push_back(x[i]);
            if ((int)pending_.size() == HOP) {
                block(out);
                pending_.clear();
            }
        }
    }

private:
    template <typename Out>
    void block(Out out) {
        for (int i = 0; i < M; i++) { in_[i][0] = hist_[i].real(); in_[i][1] = hist_[i].imag(); }
        for (int i = 0; i < HOP; i++) { in_[M + i][0] = pending_[i].real(); in_[M + i][1] = pending_[i].imag(); }
        std::copy(pending_.end() - M, pending_.end(), hist_.begin());
        fftwf_execute(fwd_);
        std::vector<cfloat> res(HOP / D);
        for (size_t c = 0; c < bins_.size(); c++) {
            for (int i = 0; i < N / D; i++) {
                int k = i < N / D / 2 ? i : i - N / D;
                int src = ((bins_[c] + k) % N + N) % N;
                sub_in_[i][0] = spec_[src][0] * win_[i];
                sub_in_[i][1] = spec_[src][1] * win_[i];
            }
            fftwf_execute(inv_);
            for (int i = 0; i < HOP / D; i++) res[i] = cfloat(sub_out_[M / D + i][0], sub_out_[M / D + i][1]);
            out((int)c, res.data(), res.size());
        }
    }

    double fs_ = 0;
    fftwf_complex *in_, *spec_, *sub_in_, *sub_out_;
    fftwf_plan fwd_, inv_;
    std::vector<int> bins_;
    std::vector<float> win_;
    std::vector<cfloat> hist_, pending_;
};


// GFSK 1 Mb/s advertising-channel decoder at 4 samples/bit
class BleRx {
public:
    using Emit = std::function<void(Packet &&)>;
    static constexpr double RATE = 4e6;
    static const uint32_t ADV_AA = 0x8E89BED6;

    void init(int channel, double freq) {
        channel_ = channel;
        freq_ = freq;
        for (int i = 0; i < 32; i++) pattern_[8 + i] = (ADV_AA >> i) & 1;
        for (int i = 0; i < 8; i++) pattern_[i] = (ADV_AA & 1) ? !(i & 1) : (i & 1);  // 0101... before an AA ending in 0
    }

    void process(const cfloat *z, size_t n, double t0, const Emit &emit) {
        size_t base = f_.size();
        f_.resize(base + n);
        mag_.resize(base + n);
        for (size_t i = 0; i < n; i++) {
            f_[base + i] = std::arg(z[i] * std::conj(prev_));
            mag_[base + i] = std::norm(z[i]);
            prev_ = z[i];
        }
        double buf_t0 = t0 - (double)base / RATE;
        const size_t MAXPKT = 4 * (40 + 16 + 8 * 255 + 24) + 8;
        size_t i = pos_;
        while (i + MAXPKT < f_.size()) {
            size_t used = try_packet(i, buf_t0, emit);
            i += used ? used : 1;
        }
        // keep enough history for a packet that starts near the end
        size_t keep_from = i > 8 ? i - 8 : 0;
        f_.erase(f_.begin(), f_.begin() + keep_from);
        mag_.erase(mag_.begin(), mag_.begin() + keep_from);
        pos_ = i - keep_from;
    }

private:
    // soft bit: integrate the discriminator over the bit
    float soft(size_t at) const { return f_[at] + f_[at + 1] + f_[at + 2] + f_[at + 3]; }

    size_t try_packet(size_t n, double buf_t0, const Emit &emit) {
        // quick reject: preamble alternation
        float s0 = soft(n), s1 = soft(n + 4);
        if ((s0 > 0) == (s1 > 0)) return 0;
        float s[40];
        float mean = 0.0f;
        for (int k = 0; k < 40; k++) {
            s[k] = soft(n + 4 * k);
            mean += s[k];
        }
        mean /= 40.0f;
        int errs = 0;
        for (int k = 0; k < 40 && errs <= 1; k++) errs += ((s[k] > mean) != (bool)pattern_[k]);
        if (errs > 1) return 0;
        // DC from the pattern itself: midpoint of its 1s and 0s
        float one = 0, zero = 0;
        int n1 = 0, n0 = 0;
        for (int k = 0; k < 40; k++) {
            if (pattern_[k]) { one += s[k]; n1++; } else { zero += s[k]; n0++; }
        }
        float dc = 0.5f * (one / n1 + zero / n0);
        size_t start = n + 160;

        auto bit = [&](int k) { return soft(start + 4 * k) > dc ? 1 : 0; };
        uint8_t lfsr = (channel_ & 0x3F) | 0x40;
        auto dewhiten = [&](int b) {
            int w = lfsr & 1;
            lfsr >>= 1;
            if (w) lfsr ^= 0x44;
            return b ^ w;
        };
        std::vector<uint8_t> pdu(2);
        for (int k = 0; k < 16; k++) pdu[k / 8] |= dewhiten(bit(k)) << (k % 8);
        int len = pdu[1];
        if (len < 6 || len > 37 + 218) return 0;
        pdu.resize(2 + len + 3);
        for (int k = 16; k < 16 + 8 * (len + 3); k++) pdu[k / 8] |= dewhiten(bit(k)) << (k % 8);

        // CRC-24 over header + payload, init 0x555555, transmitted reversed
        uint32_t reg = 0x555555;
        for (int k = 0; k < 16 + 8 * len; k++) {
            int b = (pdu[k / 8] >> (k % 8)) & 1;
            int fb = ((reg >> 23) & 1) ^ b;
            reg = (reg << 1) & 0xFFFFFF;
            if (fb) reg ^= 0x00065B;
        }
        uint32_t rx = 0;
        for (int k = 0; k < 24; k++) rx |= (uint32_t)((pdu[2 + len + k / 8] >> (k % 8)) & 1) << (23 - k);
        if (rx != reg) return 0;

        Packet p;
        p.time = buf_t0 + n / RATE;
        p.channel = fmt("BLE %d", channel_);
        p.freq = freq_;
        p.link = Link::Ble;
        p.ble_channel = channel_;
        size_t total = 160 + 4 * 8 * (2 + len + 3);
        double e = 0;
        for (size_t k = n; k < n + total; k++) e += mag_[k];
        p.rssi = (float)(10.0 * std::log10(e / total + 1e-20));
        // payload as on air after dewhitening: access address + PDU + CRC
        p.data = {0xD6, 0xBE, 0x89, 0x8E};
        p.data.insert(p.data.end(), pdu.begin(), pdu.end());
        dissect(p);
        emit(std::move(p));
        return total;
    }

    void dissect(Packet &p);

    int channel_ = 37;
    double freq_ = 2402e6;
    uint8_t pattern_[40];
    std::vector<float> f_, mag_;
    size_t pos_ = 0;
    cfloat prev_{1, 0};
};

// ---- dissector ----

inline const char *ble_company(int id) {
    switch (id) {
    case 0x0006: return "Microsoft";
    case 0x000F: return "Broadcom";
    case 0x004C: return "Apple";
    case 0x0059: return "Nordic Semiconductor";
    case 0x0075: return "Samsung";
    case 0x0087: return "Garmin";
    case 0x00E0: return "Google";
    case 0x0131: return "Cypress";
    case 0x0157: return "Anhui Huami";
    case 0x0171: return "Amazon";
    case 0x01DA: return "Logitech";
    case 0x02E5: return "Espressif";
    case 0x038F: return "Xiaomi";
    case 0x0499: return "Ruuvi";
    case 0x05A7: return "Sonos";
    case 0x0822: return "Tile";
    default: return nullptr;
    }
}

inline const char *apple_type(int t) {
    switch (t) {
    case 0x02: return "iBeacon";
    case 0x05: return "AirDrop";
    case 0x07: return "AirPods / proximity pairing";
    case 0x09: return "AirPlay target";
    case 0x0C: return "Handoff";
    case 0x0D: return "Tethering target";
    case 0x0F: return "Nearby Action";
    case 0x10: return "Nearby Info";
    case 0x12: return "Find My";
    default: return nullptr;
    }
}

inline void BleRx::dissect(Packet &p) {
    static const char *types[16] = {"ADV_IND", "ADV_DIRECT_IND", "ADV_NONCONN_IND", "SCAN_REQ", "SCAN_RSP",
                                    "CONNECT_IND", "ADV_SCAN_IND", "ADV_EXT_IND", "AUX_CONNECT_RSP", "?",
                                    "?", "?", "?", "?", "?", "?"};
    const uint8_t *d = p.data.data();
    int type = d[4] & 0x0F, len = d[5];
    bool txadd = d[4] & 0x40, rxadd = d[4] & 0x80;
    p.protocol = "LE LL";
    Field &ll = p.tree.add("Bluetooth Low Energy Link Layer", 0, (int)p.data.size());
    ll.add("Access Address: 0x8e89bed6 (advertising)", 0, 4);
    Field &h = ll.add(fmt("Packet Header: %s, length %d", types[type], len), 4, 2);
    h.add(fmt("PDU Type: %s (%d)", types[type], type), 4, 1);
    h.add(fmt("ChSel: %d", (d[4] >> 5) & 1), 4, 1);
    h.add(fmt("TxAdd: %s", txadd ? "Random" : "Public"), 4, 1);
    h.add(fmt("RxAdd: %s", rxadd ? "Random" : "Public"), 4, 1);
    h.add(fmt("Length: %d", len), 5, 1);
    const uint8_t *pl = d + 6;
    int off = 6;
    p.info = types[type];
    p.destination = "broadcast";

    auto addr = [&](const char *name, int at, bool random) {
        std::string a = mac_rev(d + at);
        ll.add(fmt("%s: %s (%s)", name, a.c_str(), random ? "random" : "public"), at, 6);
        return a;
    };

    int ad_start = -1;
    if (type == 0 || type == 2 || type == 4 || type == 6) {  // AdvA + AdvData
        p.source = addr(type == 4 ? "AdvA (scan response)" : "Advertising Address", off, txadd);
        ad_start = off + 6;
    } else if (type == 1) {
        p.source = addr("Advertising Address", off, txadd);
        p.destination = addr("Target Address", off + 6, rxadd);
    } else if (type == 3) {
        p.source = addr("Scanning Address", off, txadd);
        p.destination = addr("Advertising Address", off + 6, rxadd);
    } else if (type == 5) {
        p.source = addr("Initiator Address", off, txadd);
        p.destination = addr("Advertising Address", off + 6, rxadd);
        if (len >= 34) {
            uint32_t aa = pl[12] | pl[13] << 8 | pl[14] << 16 | (uint32_t)pl[15] << 24;
            Field &ld = ll.add("Link Layer Data", off + 12, 22);
            ld.add("Access Address: 0x" + hex(aa, 8), off + 12, 4);
            ld.add(fmt("Interval: %d (%.2f ms)", pl[22] | pl[23] << 8, (pl[22] | pl[23] << 8) * 1.25), off + 22, 2);
            p.info += fmt(", new connection AA 0x%08x", aa);
        }
    }

    if (ad_start >= 0) {
        int end = 6 + len;
        Field &adv = ll.add("Advertising Data", ad_start, end - ad_start);
        int i = ad_start;
        while (i < end) {
            int l = d[i];
            if (l == 0 || i + 1 + l > end) break;
            int t = d[i + 1];
            const uint8_t *v = d + i + 2;
            int vl = l - 1;
            Field &f = adv.add("", i, l + 1);
            switch (t) {
            case 0x01:
                f.label = fmt("Flags: 0x%02x%s%s%s", v[0], v[0] & 2 ? " (LE General Discoverable)" : "",
                              v[0] & 1 ? " (LE Limited Discoverable)" : "", v[0] & 4 ? " (BR/EDR Not Supported)" : "");
                break;
            case 0x02: case 0x03: {
                std::string u;
                for (int k = 0; k + 1 < vl; k += 2) u += (u.empty() ? "" : ", ") + fmt("0x%04x", v[k] | v[k + 1] << 8);
                f.label = std::string(t == 2 ? "Incomplete" : "Complete") + " list of 16-bit service UUIDs: " + u;
                break;
            }
            case 0x06: case 0x07:
                f.label = "128-bit service UUIDs";
                break;
            case 0x08: case 0x09: {
                std::string name = printable(v, vl);
                f.label = std::string(t == 8 ? "Shortened" : "Complete") + " local name: \"" + name + "\"";
                p.info += ", \"" + name + "\"";
                break;
            }
            case 0x0A:
                f.label = fmt("TX power level: %d dBm", (int8_t)v[0]);
                break;
            case 0x16:
                f.label = vl >= 2 ? fmt("Service data, UUID 0x%04x", v[0] | v[1] << 8) : std::string("Service data");
                if (vl >= 2 && (v[0] | v[1] << 8) == 0xFE9F) p.info += ", Google Fast Pair";
                if (vl >= 2 && (v[0] | v[1] << 8) == 0xFD6F) p.info += ", Exposure Notification";
                break;
            case 0x19:
                f.label = vl >= 2 ? fmt("Appearance: 0x%04x", v[0] | v[1] << 8) : std::string("Appearance");
                break;
            case 0xFF: {
                int cid = vl >= 2 ? (v[0] | v[1] << 8) : -1;
                const char *cn = cid >= 0 ? ble_company(cid) : nullptr;
                f.label = cn ? fmt("Manufacturer specific data: %s (0x%04x)", cn, cid) : fmt("Manufacturer specific data: company 0x%04x", cid);
                std::string extra;
                if (cid == 0x004C && vl >= 3) {
                    const char *at = apple_type(v[2]);
                    extra = at ? at : fmt("type 0x%02x", v[2]);
                    f.add("Apple: " + extra, i + 4, 1);
                } else if (cn) {
                    extra = cn;
                }
                if (!extra.empty()) p.info += ", " + std::string(cid == 0x004C ? "Apple " : "") + extra;
                break;
            }
            default:
                f.label = fmt("AD type 0x%02x, %d bytes", t, vl);
            }
            i += l + 1;
        }
    }
    ll.add("CRC: 0x" + hex(d[6 + len] | d[7 + len] << 8 | d[8 + len] << 16, 6) + " (valid)", 6 + len, 3);
}

}  // namespace shark
