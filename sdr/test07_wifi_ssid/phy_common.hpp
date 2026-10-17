#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

using cf = std::complex<float>;

static const double PI = 3.14159265358979323846;

// PHY formats. Only Legacy frames carry a psdu; the others are reported (timing only)
// when the receiver's report_undecoded flag is set.
enum RxFormat { FMT_LEGACY = 0, FMT_HT_VHT = 1, FMT_HE = 2, FMT_CCK = 3 };

struct RxFrame {
    std::vector<uint8_t> psdu;  // MAC frame including FCS
    std::string phy;            // e.g. "OFDM 6M", "DSSS 1M"
    float power_db = 0.0f;      // dBFS
    size_t start = 0, end = 0;  // sample span of the whole PPDU (preamble start .. last symbol)
    int format = FMT_LEGACY;
};

using FrameCallback = std::function<void(const RxFrame &)>;

struct RxStats {
    long ofdm_detect = 0;
    long ofdm_signal_ok = 0;
    long dsss_lock = 0;
    long dsss_sfd = 0;
    long dsss_header_ok = 0;
    long dsss_header_bad = 0;
};

inline double power_dbfs(const std::vector<cf> &x, size_t start, size_t len) {
    double sum = 0.0;
    size_t end = std::min(x.size(), start + len);
    for (size_t i = start; i < end; i++) {
        sum += std::norm(x[i]);
    }
    return 10.0 * std::log10(sum / std::max<size_t>(1, end - start) + 1e-20);
}

inline float median_power(const std::vector<cf> &x, size_t block) {
    std::vector<float> p;
    for (size_t i = 0; i + block <= x.size(); i += block) {
        double s = 0.0;
        for (size_t k = 0; k < block; k++) {
            s += std::norm(x[i + k]);
        }
        p.push_back((float)(s / block));
    }
    if (p.empty()) {
        return 0.0f;
    }
    std::nth_element(p.begin(), p.begin() + p.size() / 2, p.end());
    return p[p.size() / 2];
}
