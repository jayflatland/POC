/*
Loopback test for the WiFi decoders: builds beacon frames, transmits them
in software (OFDM at every rate, DSSS 1/2 Mbps), adds CFO and noise, and
checks that the receivers recover them with a valid FCS.
*/

#include <cstdio>
#include <random>

#include "dsss_rx.hpp"
#include "mac80211.hpp"
#include "ofdm_rx.hpp"

static int failures = 0;

static void check(bool ok, const char *what) {
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        failures++;
    }
}

static std::vector<uint8_t> make_beacon(const std::string &ssid) {
    std::vector<uint8_t> f = {0x80, 0x00, 0x00, 0x00};
    for (int i = 0; i < 6; i++) f.push_back(0xFF);
    uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    f.insert(f.end(), bssid, bssid + 6);
    f.insert(f.end(), bssid, bssid + 6);
    f.push_back(0x10);
    f.push_back(0x00);
    for (int i = 0; i < 8; i++) f.push_back(0);  // timestamp
    f.insert(f.end(), {0x64, 0x00, 0x11, 0x04});  // interval, capability (privacy)
    f.push_back(0);
    f.push_back((uint8_t)ssid.size());
    f.insert(f.end(), ssid.begin(), ssid.end());
    f.insert(f.end(), {3, 1, 6});  // DS parameter set: channel 6
    f.insert(f.end(), {48, 20, 1, 0, 0x00, 0x0F, 0xAC, 4, 1, 0, 0x00, 0x0F, 0xAC, 4, 1, 0, 0x00, 0x0F, 0xAC, 2, 0, 0});
    uint32_t crc = crc32_ieee(f.data(), f.size());
    for (int i = 0; i < 4; i++) f.push_back((crc >> (8 * i)) & 0xFF);
    return f;
}

// -------------------- OFDM transmitter --------------------

struct TxRate {
    int mbps, code, nbpsc, ncbps, ndbps, coding;
};
static const TxRate TX_RATES[] = {
    {6, 13, 1, 48, 24, 2}, {9, 15, 1, 48, 36, 4}, {12, 5, 2, 96, 48, 2}, {18, 7, 2, 96, 72, 4},
    {24, 9, 4, 192, 96, 2}, {36, 11, 4, 192, 144, 4}, {48, 1, 6, 288, 192, 3}, {54, 3, 6, 288, 216, 4}};

static std::vector<cf> ifft64(const cf *X) {
    std::vector<cf> out(64);
    for (int n = 0; n < 64; n++) {
        cf s = 0.0f;
        for (int k = 0; k < 64; k++) {
            s += X[k] * std::polar(1.0f, (float)(2.0 * PI * k * n / 64.0));
        }
        out[n] = s / 8.0f;
    }
    return out;
}

static void interleave(const uint8_t *in, int ncbps, int nbpsc, uint8_t *out) {
    int s = std::max(nbpsc / 2, 1);
    for (int k = 0; k < ncbps; k++) {
        int i = (ncbps / 16) * (k % 16) + k / 16;
        int j = s * (i / s) + (i + ncbps - (16 * i) / ncbps) % s;
        out[j] = in[k];
    }
}

static float pam(const uint8_t *b, int n) {
    if (n == 1) return b[0] ? 1.0f : -1.0f;
    if (n == 2) { static const float l[4] = {-3, -1, 3, 1}; return l[b[0] * 2 + b[1]]; }
    static const float l[8] = {-7, -5, -1, -3, 7, 5, 1, 3};
    return l[b[0] * 4 + b[1] * 2 + b[2]];
}

static cf map_bits(const uint8_t *b, int nbpsc) {
    if (nbpsc == 1) return cf(b[0] ? 1.0f : -1.0f, 0.0f);
    int h = nbpsc / 2;
    float scale = nbpsc == 2 ? 1.0f / std::sqrt(2.0f) : nbpsc == 4 ? 1.0f / std::sqrt(10.0f) : 1.0f / std::sqrt(42.0f);
    return cf(pam(b, h), pam(b + h, h)) * scale;
}

static std::vector<cf> ofdm_symbol(const uint8_t *coded, int nbpsc, int ncbps, float pol) {
    std::vector<uint8_t> il(ncbps);
    interleave(coded, ncbps, nbpsc, il.data());
    cf X[64] = {};
    int i = 0;
    for (int k = -26; k <= 26; k++) {
        if (k == 0 || std::abs(k) == 7 || std::abs(k) == 21) continue;
        X[(k + 64) % 64] = map_bits(&il[i * nbpsc], nbpsc);
        i++;
    }
    X[64 - 21] = pol;
    X[64 - 7] = pol;
    X[7] = pol;
    X[21] = -pol;
    auto t = ifft64(X);
    std::vector<cf> out(t.end() - 16, t.end());
    out.insert(out.end(), t.begin(), t.end());
    return out;
}

static std::vector<cf> ofdm_tx(const std::vector<uint8_t> &psdu, const TxRate &r, const float *polarity) {
    std::vector<cf> out;
    // STF
    static const int sidx[12] = {-24, -20, -16, -12, -8, -4, 4, 8, 12, 16, 20, 24};
    static const float sval[12] = {1, -1, 1, -1, -1, 1, -1, -1, 1, 1, 1, 1};
    cf S[64] = {};
    for (int i = 0; i < 12; i++) S[(sidx[i] + 64) % 64] = cf(sval[i], sval[i]) * std::sqrt(13.0f / 6.0f);
    auto st = ifft64(S);
    for (int i = 0; i < 160; i++) out.push_back(st[i % 64]);
    // LTF
    static const int L[53] = {1, 1, -1, -1, 1, 1, -1, 1, -1, 1, 1, 1, 1, 1, 1, -1, -1, 1, 1, -1, 1, -1, 1, 1, 1, 1, 0,
                              1, -1, -1, 1, 1, -1, 1, -1, 1, -1, -1, -1, -1, -1, 1, 1, -1, -1, 1, -1, 1, -1, 1, 1, 1, 1};
    cf LX[64] = {};
    for (int k = -26; k <= 26; k++) LX[(k + 64) % 64] = (float)L[k + 26];
    auto lt = ifft64(LX);
    out.insert(out.end(), lt.begin() + 32, lt.end());
    out.insert(out.end(), lt.begin(), lt.end());
    out.insert(out.end(), lt.begin(), lt.end());
    // SIGNAL
    std::vector<uint8_t> sig(24, 0);
    for (int i = 0; i < 4; i++) sig[i] = (r.code >> (3 - i)) & 1;
    for (int i = 0; i < 12; i++) sig[5 + i] = (psdu.size() >> i) & 1;
    int par = 0;
    for (int i = 0; i < 17; i++) par ^= sig[i];
    sig[17] = par;
    auto sc = conv_encode(sig);
    auto sym = ofdm_symbol(sc.data(), 1, 48, polarity[0]);
    out.insert(out.end(), sym.begin(), sym.end());
    // DATA
    int nbits = 16 + 8 * (int)psdu.size() + 6;
    int nsym = (nbits + r.ndbps - 1) / r.ndbps;
    std::vector<uint8_t> bits(nsym * r.ndbps, 0);
    for (size_t i = 0; i < psdu.size(); i++)
        for (int b = 0; b < 8; b++) bits[16 + 8 * i + b] = (psdu[i] >> b) & 1;
    int state = 0x5D;
    for (auto &b : bits) {
        int s = ((state >> 6) ^ (state >> 3)) & 1;
        state = ((state << 1) | s) & 0x7F;
        b ^= s;
    }
    for (int i = 0; i < 6; i++) bits[16 + 8 * psdu.size() + i] = 0;  // tail after scrambling
    auto coded = conv_encode(bits);
    std::vector<uint8_t> punct;
    if (r.coding == 2) punct = coded;
    else if (r.coding == 3) {
        for (size_t i = 0; i + 4 <= coded.size(); i += 4) punct.insert(punct.end(), {coded[i], coded[i + 1], coded[i + 2]});
    } else {
        for (size_t i = 0; i + 6 <= coded.size(); i += 6) punct.insert(punct.end(), {coded[i], coded[i + 1], coded[i + 2], coded[i + 5]});
    }
    for (int s = 0; s < nsym; s++) {
        auto d = ofdm_symbol(&punct[s * r.ncbps], r.nbpsc, r.ncbps, polarity[(s + 1) % 127]);
        out.insert(out.end(), d.begin(), d.end());
    }
    return out;
}

// -------------------- DSSS transmitter --------------------

static std::vector<cf> dsss_tx(const std::vector<uint8_t> &psdu, bool two_mbps, bool short_pre) {
    std::vector<int> bits;
    auto push_byte_lsb = [&](int v, int n) { for (int i = 0; i < n; i++) bits.push_back((v >> i) & 1); };
    if (short_pre) {
        for (int i = 0; i < 56; i++) bits.push_back(0);
        push_byte_lsb(0x05CF, 16);
    } else {
        for (int i = 0; i < 128; i++) bits.push_back(1);
        push_byte_lsb(0xF3A0, 16);
    }
    int hdr_start = bits.size();
    int signal = two_mbps ? 0x14 : 0x0A;
    int length = (int)psdu.size() * (two_mbps ? 4 : 8);
    push_byte_lsb(signal, 8);
    push_byte_lsb(0x04, 8);
    push_byte_lsb(length, 16);
    uint16_t crc = 0xFFFF;
    for (int i = hdr_start; i < hdr_start + 32; i++) {
        int fb = ((crc >> 15) & 1) ^ bits[i];
        crc = (uint16_t)(crc << 1);
        if (fb) crc ^= 0x1021;
    }
    crc ^= 0xFFFF;
    for (int i = 15; i >= 0; i--) bits.push_back((crc >> i) & 1);
    int psdu_start = bits.size();
    for (auto b : psdu) push_byte_lsb(b, 8);

    // scramble
    int hist = 0x1B;
    for (auto &b : bits) {
        int s = b ^ ((hist >> 3) & 1) ^ ((hist >> 6) & 1);
        hist = ((hist << 1) | s) & 0x7F;
        b = s;
    }

    static const float barker[11] = {1, -1, 1, 1, -1, 1, 1, 1, -1, -1, -1};
    std::vector<cf> out;
    float phase = 0.0f;
    auto emit = [&]() {
        for (int k = 0; k < 20; k++) out.push_back(std::polar(1.0f, phase) * barker[k * 11 / 20]);
    };
    size_t i = 0;
    int dq_from = short_pre ? hdr_start : (two_mbps ? psdu_start : (int)bits.size());
    while (i < bits.size()) {
        if ((int)i >= dq_from) {
            static const float dphi[4] = {0, (float)(PI / 2), (float)(3 * PI / 2), (float)PI};  // 00,01,10,11
            phase += dphi[bits[i] * 2 + bits[i + 1]];
            i += 2;
        } else {
            phase += bits[i] ? (float)PI : 0.0f;
            i += 1;
        }
        emit();
    }
    return out;
}

// -------------------- tests --------------------

static int decode_count(const std::vector<cf> &tx, bool use_dsss, double cfo, double snr_db, std::string &ssid_out) {
    std::mt19937 rng(42);
    std::normal_distribution<float> g(0.0f, 1.0f);
    float sig_p = 0.0f;
    for (auto &v : tx) sig_p += std::norm(v);
    sig_p /= tx.size();
    float amp = 0.1f / std::sqrt(sig_p);
    float nstd = 0.1f * std::pow(10.0f, -snr_db / 20.0f) / std::sqrt(2.0f);

    std::vector<cf> x(20000, 0.0f);
    x.insert(x.end(), tx.begin(), tx.end());
    x.insert(x.end(), 20000, 0.0f);
    for (size_t i = 0; i < x.size(); i++) {
        x[i] = x[i] * amp * std::polar(1.0f, (float)(2.0 * PI * cfo * i / 20e6)) + cf(g(rng) * nstd, g(rng) * nstd);
    }

    int good = 0;
    RxStats st;
    auto cb = [&](const RxFrame &f) {
        BeaconInfo info;
        if (fcs_ok(f.psdu) && parse_beacon(f.psdu, info)) {
            good++;
            ssid_out = info.ssid + " " + info.security + " ch" + std::to_string(info.channel);
        }
    };
    static OfdmRx ofdm;
    static DsssRx dsss;
    if (use_dsss) dsss.process(x, 20e6, cb, st);
    else ofdm.process(x, 20e6, cb, st);
    return good;
}

int main() {
    printf("Known values\n");
    const char *msg = "123456789";
    check(crc32_ieee((const uint8_t *)msg, 9) == 0xCBF43926, "CRC-32 check value 0xCBF43926");
    OfdmRx rx;
    const float *pol = rx.polarity();
    bool pol_ok = pol[0] == 1 && pol[1] == 1 && pol[2] == 1 && pol[3] == 1 && pol[4] == -1 && pol[5] == -1 &&
                  pol[6] == -1 && pol[7] == 1;
    check(pol_ok, "pilot polarity starts 1,1,1,1,-1,-1,-1,1");
    std::vector<uint8_t> vb(200);
    std::mt19937 r(1);
    for (auto &b : vb) b = r() & 1;
    for (int i = 0; i < 6; i++) vb.push_back(0);
    auto enc = conv_encode(vb);
    std::vector<float> soft;
    for (auto c : enc) soft.push_back(c ? 1.0f : -1.0f);
    for (int i = 0; i < 20; i++) soft[i * 17] = -soft[i * 17];  // flip some coded bits
    check(viterbi_decode(soft, vb.size()) == vb, "Viterbi corrects 20 flipped coded bits");

    auto beacon = make_beacon("TestNet");
    printf("\nOFDM loopback (CFO +45 kHz, SNR 25 dB)\n");
    for (auto &rate : TX_RATES) {
        std::string got;
        int n = decode_count(ofdm_tx(beacon, rate, pol), false, 45e3, 25.0, got);
        char what[96];
        snprintf(what, sizeof(what), "%2d Mbps -> %s", rate.mbps, n ? got.c_str() : "nothing");
        check(n == 1 && got == "TestNet WPA2 ch6", what);
    }
    printf("\nOFDM 6 Mbps at low SNR\n");
    for (double snr : {10.0, 6.0, 4.0}) {
        std::string got;
        int n = decode_count(ofdm_tx(beacon, TX_RATES[0], pol), false, -30e3, snr, got);
        char what[96];
        snprintf(what, sizeof(what), "SNR %.0f dB -> %s", snr, n ? got.c_str() : "nothing");
        check(n == 1 || snr < 5.0, what);
    }

    printf("\nDSSS loopback (CFO +40 kHz, SNR 10 dB)\n");
    struct { bool two, shortp; const char *name; } dcases[] = {
        {false, false, "1 Mbps long preamble"}, {true, false, "2 Mbps long preamble"}, {true, true, "2 Mbps short preamble"}};
    for (auto &dc : dcases) {
        std::string got;
        int n = decode_count(dsss_tx(beacon, dc.two, dc.shortp), true, 40e3, 10.0, got);
        char what[96];
        snprintf(what, sizeof(what), "%s -> %s", dc.name, n ? got.c_str() : "nothing");
        check(n == 1 && got == "TestNet WPA2 ch6", what);
    }

    printf("\n%s\n", failures ? "SOME TESTS FAILED" : "all tests passed");
    return failures ? 1 : 0;
}
