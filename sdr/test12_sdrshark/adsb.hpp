#pragma once

// ADS-B / Mode S receiver at 4 MS/s (1090 MHz, pulse position modulation) and dissector.

#include <cmath>
#include <complex>
#include <cstdarg>
#include <functional>
#include <map>
#include <set>
#include <vector>

#include "packet.hpp"

namespace shark {

class ModeS {
public:
    using Emit = std::function<void(Packet &&)>;
    static constexpr double RATE = 4e6;

    ModeS() {
        for (int i = 0; i < 256; i++) {
            uint32_t c = (uint32_t)i << 16;
            for (int k = 0; k < 8; k++) c = (c & 0x800000) ? ((c << 1) ^ 0xFFF409) : (c << 1);
            crc_table_[i] = c & 0xFFFFFF;
        }
    }

    // complex samples at 4 MS/s; t0 = capture time of the first sample
    void process(const std::complex<float> *x, size_t n, double t0, const Emit &emit) {
        mag_.resize(n);
        for (size_t i = 0; i < n; i++) mag_[i] = std::abs(x[i]);
        // keep the tail of the previous block so messages straddling blocks decode
        buf_.insert(buf_.end(), mag_.begin(), mag_.end());
        double buf_t0 = t0 - (double)(buf_.size() - n) / RATE;
        size_t limit = buf_.size() >= MSG_SAMPLES ? buf_.size() - MSG_SAMPLES : 0;
        size_t i = 0;
        while (i < limit) {
            int used = try_decode(i, buf_t0 + i / RATE, emit);
            i += used > 0 ? used : 1;
        }
        buf_.erase(buf_.begin(), buf_.begin() + std::min(i, buf_.size()));
    }

private:
    static const size_t MSG_SAMPLES = 32 + 112 * 4;

    uint32_t crc(const uint8_t *m, int bytes) const {
        uint32_t c = 0;
        for (int i = 0; i < bytes; i++) c = ((c << 8) ^ crc_table_[((c >> 16) ^ m[i]) & 0xFF]) & 0xFFFFFF;
        return c;
    }

    int try_decode(size_t n, double t, const Emit &emit) {
        const float *m = &buf_[n];
        // preamble pulses at 0, 1.0, 3.5, 4.5 us (2 samples each at 4 MS/s)
        float hi = m[0] + m[1] + m[4] + m[5] + m[14] + m[15] + m[18] + m[19];
        float lo = m[2] + m[3] + m[8] + m[9] + m[10] + m[11] + m[22] + m[23];
        if (hi < 3.0f * lo || hi < 8 * floor_ * 3.0f) {
            floor_ = 0.9999f * floor_ + 0.0001f * m[0];
            return 0;
        }
        if (m[0] < m[2] || m[4] < m[3] || m[14] < m[12] || m[18] < m[21]) return 0;

        uint8_t msg[14] = {};
        float level = 0.0f;
        for (int b = 0; b < 112; b++) {
            const float *s = m + 32 + 4 * b;
            float a = s[0] + s[1], c = s[2] + s[3];
            if (a > c) msg[b / 8] |= 0x80 >> (b % 8);
            level += std::max(a, c);
        }
        int df = msg[0] >> 3;
        int bytes = df >= 16 ? 14 : 7;
        uint32_t residual = crc(msg, bytes - 3) ^ ((msg[bytes - 3] << 16) | (msg[bytes - 2] << 8) | msg[bytes - 1]);

        bool ok = false;
        uint32_t icao = 0;
        if (df == 17 || df == 18) {
            ok = residual == 0;
            icao = (msg[1] << 16) | (msg[2] << 8) | msg[3];
            if (ok) known_.insert(icao);
        } else if (df == 11) {
            icao = (msg[1] << 16) | (msg[2] << 8) | msg[3];
            ok = (residual & ~0x7Fu) == 0;  // parity overlaid with interrogator ID
            if (ok) known_.insert(icao);
        } else if (df == 0 || df == 4 || df == 5 || df == 16 || df == 20 || df == 21) {
            icao = residual;  // address/parity: valid only for aircraft we already know
            ok = known_.count(icao) > 0;
        }
        if (!ok) return 0;

        Packet p;
        p.time = t;
        p.channel = "1090";
        p.freq = 1090e6;
        p.rssi = 20.0f * std::log10(level / 112.0f / 2.0f + 1e-9f);
        p.link = Link::ModeS;
        p.data.assign(msg, msg + bytes);
        dissect(p, icao, t);
        emit(std::move(p));
        return 32 + bytes * 8 * 4;
    }

    // ---- dissection ----
    static std::string callsign(const uint8_t *me) {
        static const char *cs = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ##### ###############0123456789######";
        uint64_t v = 0;
        for (int i = 1; i < 7; i++) v = (v << 8) | me[i];
        std::string s;
        for (int i = 7; i >= 0; i--) s += cs[(v >> (6 * i)) & 0x3F];
        while (!s.empty() && (s.back() == ' ' || s.back() == '#')) s.pop_back();
        return s;
    }

    struct Cpr {
        int lat = 0, lon = 0;
        double t = -100.0;
    };
    struct Aircraft {
        Cpr even, odd;
        std::string callsign;
    };

    static double cpr_mod(double a, double b) {
        double r = std::fmod(a, b);
        return r < 0 ? r + b : r;
    }
    static int nl(double lat) {
        if (std::abs(lat) >= 87.0) return 1;
        double a = 1 - std::cos(M_PI / 30.0);
        double b = std::cos(M_PI / 180.0 * std::abs(lat));
        return (int)std::floor(2 * M_PI / std::acos(1 - a / (b * b)));
    }
    // global CPR decode from an even/odd pair (airborne)
    static bool cpr_decode(const Aircraft &ac, bool latest_odd, double *lat, double *lon) {
        if (std::abs(ac.even.t - ac.odd.t) > 10.0) return false;
        double lat0 = ac.even.lat / 131072.0, lat1 = ac.odd.lat / 131072.0;
        double lon0 = ac.even.lon / 131072.0, lon1 = ac.odd.lon / 131072.0;
        double j = std::floor(59 * lat0 - 60 * lat1 + 0.5);
        double rlat0 = 6.0 * (cpr_mod(j, 60) + lat0), rlat1 = 360.0 / 59 * (cpr_mod(j, 59) + lat1);
        if (rlat0 >= 270) rlat0 -= 360;
        if (rlat1 >= 270) rlat1 -= 360;
        if (nl(rlat0) != nl(rlat1)) return false;
        double rl = latest_odd ? rlat1 : rlat0;
        int nlv = nl(rl);
        int ni = std::max(nlv - (latest_odd ? 1 : 0), 1);
        double m = std::floor(lon0 * (nlv - 1) - lon1 * nlv + 0.5);
        double lonv = 360.0 / ni * (cpr_mod(m, ni) + (latest_odd ? lon1 : lon0));
        if (lonv >= 180) lonv -= 360;
        *lat = rl;
        *lon = lonv;
        return true;
    }

    void dissect(Packet &p, uint32_t icao, double t) {
        const uint8_t *m = p.data.data();
        int df = m[0] >> 3;
        p.protocol = df == 17 || df == 18 ? "ADS-B" : "Mode S";
        p.source = hex(icao, 6);
        p.destination = "broadcast";
        Field &top = p.tree.add(fmt("Mode S, DF %d, %d bits", df, (int)p.data.size() * 8), 0, (int)p.data.size());
        top.add(fmt("Downlink Format: %d", df), 0, 1);
        int bytes = (int)p.data.size();
        Aircraft &ac = aircraft_[icao];

        if (df == 17 || df == 18) {
            top.add(fmt("Capability: %d", m[0] & 7), 0, 1);
            top.add("ICAO address: " + hex(icao, 6), 1, 3);
            const uint8_t *me = m + 4;
            int tc = me[0] >> 3;
            Field &mef = top.add(fmt("Extended squitter message, type code %d", tc), 4, 7);
            if (tc >= 1 && tc <= 4) {
                ac.callsign = callsign(me);
                mef.add(fmt("Category: %d", me[0] & 7), 4, 1);
                mef.add("Callsign: " + ac.callsign, 5, 6);
                p.info = "Identification: " + ac.callsign;
            } else if ((tc >= 9 && tc <= 18) || (tc >= 20 && tc <= 22)) {
                int alt_code = (me[1] << 4) | (me[2] >> 4);
                int alt = -1;
                if (tc <= 18 && (alt_code & 0x10)) alt = ((((alt_code & 0xFE0) >> 1) | (alt_code & 0xF)) * 25) - 1000;
                bool odd = (me[2] >> 2) & 1;
                int clat = ((me[2] & 3) << 15) | (me[3] << 7) | (me[4] >> 1);
                int clon = ((me[4] & 1) << 16) | (me[5] << 8) | me[6];
                (odd ? ac.odd : ac.even) = Cpr{clat, clon, t};
                mef.add(alt >= 0 ? fmt("Altitude: %d ft", alt) : std::string("Altitude: (GNSS / unavailable)"), 5, 2);
                mef.add(fmt("CPR format: %s", odd ? "odd" : "even"), 6, 1);
                mef.add(fmt("CPR latitude: %d", clat), 6, 3);
                mef.add(fmt("CPR longitude: %d", clon), 8, 3);
                double lat, lon;
                std::string pos;
                if (cpr_decode(ac, odd, &lat, &lon)) {
                    mef.add(fmt("Position: %.5f, %.5f", lat, lon));
                    pos = fmt(" at %.4f, %.4f", lat, lon);
                }
                p.info = "Airborne position" + (alt >= 0 ? fmt(", %d ft", alt) : std::string()) + pos;
            } else if (tc == 19) {
                int st = me[0] & 7;
                if (st == 1 || st == 2) {
                    int ew_dir = (me[1] >> 2) & 1, ew = ((me[1] & 3) << 8) | me[2];
                    int ns_dir = (me[3] >> 7) & 1, ns = ((me[3] & 0x7F) << 3) | (me[4] >> 5);
                    int vr_sign = (me[4] >> 3) & 1, vr = ((me[4] & 7) << 6) | (me[5] >> 2);
                    double vx = (ew - 1) * (ew_dir ? -1 : 1), vy = (ns - 1) * (ns_dir ? -1 : 1);
                    double spd = std::hypot(vx, vy), hdg = cpr_mod(std::atan2(vx, vy) * 180 / M_PI, 360);
                    int rate = (vr - 1) * 64 * (vr_sign ? -1 : 1);
                    mef.add(fmt("Ground speed: %.0f kt", spd), 5, 3);
                    mef.add(fmt("Track: %.0f deg", hdg), 5, 3);
                    mef.add(fmt("Vertical rate: %+d ft/min", rate), 8, 2);
                    p.info = fmt("Velocity %.0f kt, track %.0f, %+d ft/min", spd, hdg, rate);
                } else {
                    p.info = fmt("Velocity (airspeed subtype %d)", st);
                }
            } else if (tc == 28) {
                p.info = "Aircraft status";
            } else if (tc == 29) {
                p.info = "Target state and status";
            } else if (tc == 31) {
                p.info = "Operational status";
            } else {
                p.info = fmt("Type code %d", tc);
            }
            if (!ac.callsign.empty() && p.info.find(ac.callsign) == std::string::npos) p.info += "  [" + ac.callsign + "]";
        } else if (df == 11) {
            top.add(fmt("Capability: %d", m[0] & 7), 0, 1);
            top.add("ICAO address: " + hex(icao, 6), 1, 3);
            p.info = "All-call reply";
        } else if (df == 4 || df == 20 || df == 0 || df == 16) {
            int ac13 = ((m[2] & 0x1F) << 8) | m[3];
            int alt = decode_ac13(ac13);
            top.add(alt > -9999 ? fmt("Altitude: %d ft", alt) : std::string("Altitude: unknown"), 2, 2);
            p.info = df == 0 || df == 16 ? "Air-air surveillance" : "Surveillance, altitude reply";
            if (alt > -9999) p.info += fmt(", %d ft", alt);
        } else if (df == 5 || df == 21) {
            int id = ((m[2] & 0x1F) << 8) | m[3];
            int sq = squawk(id);
            top.add(fmt("Squawk: %04d", sq), 2, 2);
            p.info = fmt("Surveillance, identity reply, squawk %04d", sq);
        }
        top.add(fmt("Parity: %s", hex((m[bytes - 3] << 16) | (m[bytes - 2] << 8) | m[bytes - 1], 6).c_str()), bytes - 3, 3);
        if (!ac.callsign.empty() && df != 17 && df != 18) p.info += "  [" + ac.callsign + "]";
    }

    static int decode_ac13(int ac) {
        if (ac == 0) return -9999;
        bool m_bit = ac & 0x40, q_bit = ac & 0x10;
        if (m_bit) return -9999;  // metric altitude: rare
        if (q_bit) {
            int n = ((ac & 0x1F80) >> 2) | ((ac & 0x20) >> 1) | (ac & 0x0F);
            return n * 25 - 1000;
        }
        return -9999;  // Gillham-coded: not decoded
    }

    static int squawk(int id) {
        // bits: C1 A1 C2 A2 C4 A4 X B1 D1 B2 D2 B4 D4
        auto bit = [&](int pos) { return (id >> (12 - pos)) & 1; };
        int a = bit(1) | bit(3) << 1 | bit(5) << 2;
        int b = bit(7) | bit(9) << 1 | bit(11) << 2;
        int c = bit(0) | bit(2) << 1 | bit(4) << 2;
        int d = bit(8) | bit(10) << 1 | bit(12) << 2;
        return a * 1000 + b * 100 + c * 10 + d;
    }

    uint32_t crc_table_[256];
    std::vector<float> mag_, buf_;
    float floor_ = 0.001f;
    std::set<uint32_t> known_;
    std::map<uint32_t, Aircraft> aircraft_;
};

}  // namespace shark
