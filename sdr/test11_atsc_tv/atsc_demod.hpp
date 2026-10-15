#pragma once

// ATSC 8-VSB demodulator: IQ at 2 samples/symbol in, equalized symbols out.
//
//   rotate pilot to 0 Hz -> complex matched filter (overlap-save FFT)
//   -> pilot PLL -> real part -> DC removal + AGC
//   -> symbol timing (interpolator, early/late on segment syncs)
//   -> segment sync -> field sync (learned from the signal)
//   -> LMS equalizer (trained on syncs, then decision-directed)

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <vector>

#include <fftw3.h>

using cf = std::complex<float>;

namespace atsc {

static const double PI = 3.14159265358979323846;
static const double SYMBOL_RATE = 4.5e6 / 286.0 * 684.0;  // 10.762237... MHz
static const double SAMPLE_RATE = 2.0 * SYMBOL_RATE;
static const int SEG_SYMS = 832;
static const int DATA_SYMS = 828;
static const int FIELD_SEGS = 313;  // 1 field sync + 312 data segments
static const float SYNC[4] = {5, -5, -5, 5};

// -------------------- front end --------------------

class Frontend {
public:
    void init() {
        // RRC (alpha 0.1152) with Nyquist at Rs/4, shifted up by Rs/4: the VSB passband
        // from the pilot (0 Hz after the rotation) to Rs/2
        const double alpha = 0.1152;
        const double T = 4.0;  // samples per "symbol" of the half-rate RRC
        std::vector<cf> h(TAPS);
        double norm = 0.0;
        for (int i = 0; i < TAPS; i++) {
            double t = (i - (TAPS - 1) / 2.0) / T;
            double v;
            if (std::abs(t) < 1e-9) {
                v = 1.0 - alpha + 4.0 * alpha / PI;
            } else if (std::abs(std::abs(4.0 * alpha * t) - 1.0) < 1e-9) {
                v = alpha / std::sqrt(2.0) *
                    ((1 + 2 / PI) * std::sin(PI / (4 * alpha)) + (1 - 2 / PI) * std::cos(PI / (4 * alpha)));
            } else {
                v = (std::sin(PI * t * (1 - alpha)) + 4 * alpha * t * std::cos(PI * t * (1 + alpha))) /
                    (PI * t * (1 - 16 * alpha * alpha * t * t));
            }
            double w = 0.5 - 0.5 * std::cos(2.0 * PI * (i + 0.5) / TAPS);  // gentle window on the tails
            v *= w;
            norm += v;
            double shift = 2.0 * PI * 0.125 * (i - (TAPS - 1) / 2.0);  // +Rs/4 = fs/8
            h[i] = std::polar((float)v, (float)shift);
        }
        for (auto &v : h) {
            v /= (float)norm;
        }

        fft_in_ = fftwf_alloc_complex(FFT_N);
        fft_out_ = fftwf_alloc_complex(FFT_N);
        fwd_ = fftwf_plan_dft_1d(FFT_N, fft_in_, fft_out_, FFTW_FORWARD, FFTW_MEASURE);
        inv_ = fftwf_plan_dft_1d(FFT_N, fft_out_, fft_in_, FFTW_BACKWARD, FFTW_MEASURE);
        std::memset(fft_in_, 0, sizeof(fftwf_complex) * FFT_N);
        for (int i = 0; i < TAPS; i++) {
            fft_in_[i][0] = h[i].real();
            fft_in_[i][1] = h[i].imag();
        }
        fftwf_execute(fwd_);
        H_.resize(FFT_N);
        for (int k = 0; k < FFT_N; k++) {
            H_[k] = cf(fft_out_[k][0], fft_out_[k][1]) / (float)FFT_N;
        }
        hist_.assign(TAPS - 1, cf(0, 0));
    }

    // IQ (channel centered at 0 Hz) in, real 2-sps baseband out
    void process(const cf *in, size_t n, std::vector<float> &out) {
        for (size_t i = 0; i < n; i++) {
            // pilot sits at -Rs/4 = -fs/8: rotate by +45 degrees per sample
            static const cf rot[8] = {{1, 0}, {0.70710678f, 0.70710678f}, {0, 1}, {-0.70710678f, 0.70710678f},
                                      {-1, 0}, {-0.70710678f, -0.70710678f}, {0, -1}, {0.70710678f, -0.70710678f}};
            pending_.push_back(in[i] * rot[rot_idx_]);
            rot_idx_ = (rot_idx_ + 1) & 7;
            if (pending_.size() == (size_t)HOP) {
                filter_block(out);
                pending_.clear();
            }
        }
    }

    bool pilot_locked() const { return locked_; }
    double pilot_offset_hz() const { return freq_ * SAMPLE_RATE / (2.0 * PI); }

private:
    static const int FFT_N = 8192;
    static const int TAPS = 1025;
    static const int HOP = FFT_N - TAPS + 1;

    void filter_block(std::vector<float> &out) {
        // overlap-save: [last TAPS-1 inputs][HOP new inputs]
        for (int i = 0; i < TAPS - 1; i++) {
            fft_in_[i][0] = hist_[i].real();
            fft_in_[i][1] = hist_[i].imag();
        }
        for (int i = 0; i < HOP; i++) {
            fft_in_[TAPS - 1 + i][0] = pending_[i].real();
            fft_in_[TAPS - 1 + i][1] = pending_[i].imag();
        }
        std::copy(pending_.end() - (TAPS - 1), pending_.end(), hist_.begin());
        fftwf_execute(fwd_);
        for (int k = 0; k < FFT_N; k++) {
            cf v = cf(fft_out_[k][0], fft_out_[k][1]) * H_[k];
            fft_out_[k][0] = v.real();
            fft_out_[k][1] = v.imag();
        }
        fftwf_execute(inv_);

        if (!acquired_) {
            acquire_pilot();
        }

        // pilot PLL, updated every 64 samples
        const int SUB = 64;
        for (int base = 0; base < HOP; base += SUB) {
            cf step = std::polar(1.0f, (float)-freq_);
            cf acc = 0.0f;
            int end = std::min(HOP, base + SUB);
            for (int i = base; i < end; i++) {
                cf x(fft_in_[TAPS - 1 + i][0], fft_in_[TAPS - 1 + i][1]);
                cf v = x * phasor_;
                phasor_ *= step;
                acc += v;
                float r = v.real();
                // DC (the pilot) removal and AGC
                dc_ += 1e-4f * (r - dc_);
                r -= dc_;
                pwr_ += 1e-5f * (r * r - pwr_);
                out.push_back(r * std::sqrt(21.0f / std::max(pwr_, 1e-20f)));
            }
            phasor_ /= std::abs(phasor_);
            // narrow loop: the VSB data spectrum starts right at the pilot
            pilot_ = 0.98f * pilot_ + 0.02f * acc;
            float err = std::arg(pilot_);
            phasor_ *= std::polar(1.0f, -0.004f * err);
            freq_ += 2e-7 * err;
            err_avg_ = 0.995f * err_avg_ + 0.005f * std::abs(err);
            locked_ = err_avg_ < 0.4f;
        }
    }

    // coarse pilot frequency from the first block's spectrum (within +/-150 kHz)
    void acquire_pilot() {
        std::vector<float> mag(FFT_N, 0.0f);
        // the filtered output is in fft_in_; re-FFT it to find the pilot line
        std::vector<cf> tmp(FFT_N);
        for (int i = 0; i < FFT_N; i++) {
            tmp[i] = cf(fft_in_[i][0], fft_in_[i][1]);
        }
        fftwf_complex *a = fftwf_alloc_complex(FFT_N), *b = fftwf_alloc_complex(FFT_N);
        fftwf_plan p = fftwf_plan_dft_1d(FFT_N, a, b, FFTW_FORWARD, FFTW_ESTIMATE);
        for (int i = 0; i < FFT_N; i++) {
            float w = 0.5f - 0.5f * std::cos(2.0f * (float)PI * i / FFT_N);
            a[i][0] = tmp[i].real() * w;
            a[i][1] = tmp[i].imag() * w;
        }
        fftwf_execute(p);
        int span = (int)(150e3 / (SAMPLE_RATE / FFT_N));
        int best = 0;
        float bestv = -1.0f;
        for (int k = -span; k <= span; k++) {
            int idx = (k + FFT_N) % FFT_N;
            float v = b[idx][0] * b[idx][0] + b[idx][1] * b[idx][1];
            if (v > bestv) {
                bestv = v;
                best = k;
            }
        }
        freq_ = 2.0 * PI * best / FFT_N;
        fftwf_destroy_plan(p);
        fftwf_free(a);
        fftwf_free(b);
        acquired_ = true;
    }

    fftwf_complex *fft_in_, *fft_out_;
    fftwf_plan fwd_, inv_;
    std::vector<cf> H_, hist_, pending_;
    int rot_idx_ = 0;
    bool acquired_ = false, locked_ = false;
    double freq_ = 0.0;
    cf phasor_{1.0f, 0.0f}, pilot_{0.0f, 0.0f};
    float dc_ = 0.0f, pwr_ = 21.0f, err_avg_ = 1.0f;
};


// -------------------- symbol timing + segment sync --------------------

// delivers whole 832-symbol segments, starting with the 4 sync symbols
class SegmentSync {
public:
    using SegmentCb = std::function<void(const float *seg)>;

    void process(const std::vector<float> &in, const SegmentCb &cb) {
        buf_.insert(buf_.end(), in.begin(), in.end());
        while (t_ + 3.0 < (double)buf_.size()) {
            float s = interp(t_);
            float d = interp(t_ + 1.0) - interp(t_ - 1.0);  // slope, for the timing error
            sym_.push_back(s);
            slope_.push_back(d);
            t_ += sps_;
            on_symbol(cb);
        }
        // drop consumed input, keeping a little history for the interpolator
        size_t drop = (size_t)std::max(0.0, std::floor(t_) - 4.0);
        if (drop > 0) {
            buf_.erase(buf_.begin(), buf_.begin() + drop);
            t_ -= (double)drop;
        }
    }

    bool locked() const { return locked_; }
    double clock_ppm() const { return (sps_ / 2.0 - 1.0) * 1e6; }
    // the B200's LO and sample clock share a reference, so the pilot's frequency
    // error predicts the sample clock error
    void set_clock_ppm(double ppm) { sps_ = 2.0 * (1.0 + ppm * 1e-6); }

private:
    float interp(double t) const {
        long i = (long)std::floor(t);
        float mu = (float)(t - i);
        if (i < 1 || i + 2 >= (long)buf_.size()) {
            return 0.0f;
        }
        float y0 = buf_[i - 1], y1 = buf_[i], y2 = buf_[i + 1], y3 = buf_[i + 2];
        // cubic Lagrange
        float c0 = y1;
        float c1 = -y0 / 3.0f - y1 / 2.0f + y2 - y3 / 6.0f;
        float c2 = (y0 + y2) / 2.0f - y1;
        float c3 = (y3 - y0) / 6.0f + (y1 - y2) / 2.0f;
        return ((c3 * mu + c2) * mu + c1) * mu + c0;
    }

    void on_symbol(const SegmentCb &cb) {
        count_++;
        size_t n = sym_.size();
        if (!locked_) {
            if (n >= 4) {
                int pos = (int)((count_ - 4) % SEG_SYMS);
                float c = 0.0f;
                for (int i = 0; i < 4; i++) {
                    c += SYNC[i] * sym_[n - 4 + i];
                }
                score_[pos] = 0.9f * score_[pos] + c;
            }
            if (count_ % SEG_SYMS == 0) {
                int best = (int)(std::max_element(score_, score_ + SEG_SYMS) - score_);
                std::vector<float> sorted(score_, score_ + SEG_SYMS);
                std::nth_element(sorted.begin(), sorted.begin() + SEG_SYMS / 2, sorted.end());
                float spread = 0.0f;
                for (float v : score_) {
                    spread += std::abs(v);
                }
                spread /= SEG_SYMS;
                bool strong = score_[best] > 8.0f * spread;
                if (best == last_best_ && strong) {
                    if (++stable_ >= 4) {
                        locked_ = true;
                        // drop symbols up to the first segment start (global symbol number = best mod 832)
                        long base = count_ - (long)n;  // global number of sym_[0]
                        long first = ((best - base) % SEG_SYMS + SEG_SYMS) % SEG_SYMS;
                        sym_.erase(sym_.begin(), sym_.begin() + first);
                        slope_.erase(slope_.begin(), slope_.begin() + first);
                    }
                } else {
                    stable_ = 0;
                }
                last_best_ = best;
                if (!locked_ && sym_.size() > 8 * SEG_SYMS) {
                    sym_.erase(sym_.begin(), sym_.end() - 4 * SEG_SYMS);
                    slope_.erase(slope_.begin(), slope_.end() - 4 * SEG_SYMS);
                }
            }
            return;
        }

        if (sym_.size() < (size_t)SEG_SYMS) {
            return;
        }
        // one full segment: sync symbols are 0..3
        float c = 0.0f, e = 0.0f;
        for (int i = 0; i < 4; i++) {
            c += SYNC[i] * sym_[i];
            e += SYNC[i] * slope_[i];
        }
        // early/late: a positive peak sampled late has a falling slope
        float err = e / std::max(std::abs(c), 1.0f);
        t_ += 0.003 * err;
        sps_ += 1e-8 * err;
        sps_ = std::min(2.0002, std::max(1.9998, sps_));
        if (c < 20.0f) {
            if (++weak_ > 50) {
                locked_ = false;  // lost it: search again
                weak_ = 0;
                std::fill(score_, score_ + SEG_SYMS, 0.0f);
            }
        } else {
            weak_ = 0;
        }
        cb(sym_.data());
        sym_.erase(sym_.begin(), sym_.begin() + SEG_SYMS);
        slope_.erase(slope_.begin(), slope_.begin() + SEG_SYMS);
    }

    std::vector<float> buf_;
    std::vector<float> sym_, slope_;
    double t_ = 2.0, sps_ = 2.0;
    long count_ = 0;
    float score_[SEG_SYMS] = {};
    int last_best_ = -1, stable_ = 0, weak_ = 0;
    bool locked_ = false;
};


// -------------------- field sync + equalizer --------------------

struct FieldSyncInfo {
    bool locked = false;
    float snr_db = 0.0f;
};

// Finds the field sync segment (the one that repeats every 313 segments), learns its
// training sequence from the signal, and equalizes. Emits data segments with their index
// in the field (0..311).
class Equalizer {
public:
    using DataCb = std::function<void(int seg_index, const float *data828)>;

    Equalizer() {
        w_.assign(NTAPS, 0.0f);
        w_[CENTER] = 1.0f;
        ref_avg_.assign(SEG_SYMS, 0.0f);
        ring_.assign(FIELD_SEGS, std::vector<float>(SEG_SYMS, 0.0f));
        slot_score_.assign(FIELD_SEGS, 0.0f);
    }

    void push_segment(const float *seg, const DataCb &cb) {
        int slot = (int)(seg_count_ % FIELD_SEGS);
        // similarity with the segment one field ago (PN511 region): high only for field syncs
        const float *prev = ring_[slot].data();
        float ab = 0, aa = 0, bb = 0;
        for (int i = 4; i < 515; i++) {
            ab += seg[i] * prev[i];
            aa += seg[i] * seg[i];
            bb += prev[i] * prev[i];
        }
        float sim = ab / std::sqrt(aa * bb + 1e-9f);
        slot_score_[slot] = 0.7f * slot_score_[slot] + 0.3f * sim;
        std::copy(seg, seg + SEG_SYMS, ring_[slot].begin());
        seg_count_++;

        if (!fs_locked_ && seg_count_ > 2 * FIELD_SEGS && slot == FIELD_SEGS - 1) {
            int best = (int)(std::max_element(slot_score_.begin(), slot_score_.end()) - slot_score_.begin());
            if (slot_score_[best] > 0.5f) {
                fs_slot_ = best;
                fs_locked_ = true;
                fields_learned_ = 0;
            }
        }

        // equalizer runs one segment behind so it has look-ahead for its pre-cursor taps
        hist_.insert(hist_.end(), seg, seg + SEG_SYMS);
        long this_index = seg_count_ - 1;
        if (hist_.size() >= (size_t)(3 * SEG_SYMS)) {
            long eq_index = this_index - 1;  // the middle segment of hist_
            equalize_middle(eq_index, cb);
            hist_.erase(hist_.begin(), hist_.begin() + SEG_SYMS);
        }
    }

    FieldSyncInfo info() const {
        FieldSyncInfo i;
        i.locked = fs_locked_ && fields_learned_ >= 2;
        i.snr_db = 10.0f * std::log10(21.0f / std::max(mse_, 1e-6f));
        return i;
    }

private:
    static const int NTAPS = 96;
    static const int CENTER = 32;

    static float slice(float y) {
        float v = std::round((y - 1.0f) / 2.0f) * 2.0f + 1.0f;
        return std::max(-7.0f, std::min(7.0f, v));
    }

    void equalize_middle(long index, const DataCb &cb) {
        bool is_fs = fs_locked_ && (index % FIELD_SEGS) == fs_slot_;
        const float *mid = hist_.data() + SEG_SYMS;

        if (is_fs) {
            // learn the field sync's training symbols from the raw (unequalized) signal
            for (int i = 0; i < SEG_SYMS; i++) {
                ref_avg_[i] = 0.8f * ref_avg_[i] + 0.2f * mid[i];
            }
            fields_learned_++;
        }
        bool train_fs = is_fs && fields_learned_ >= 2;

        float out[SEG_SYMS];
        float err_acc = 0.0f;
        int err_n = 0;
        for (int n = 0; n < SEG_SYMS; n++) {
            // y[n] = sum_k w[k] x[n + CENTER - k]
            const float *x = mid + n + CENTER;
            float y = 0.0f;
            for (int k = 0; k < NTAPS; k++) {
                y += w_[k] * x[-k];
            }
            out[n] = y;

            float d;
            float mu;
            if (n < 4) {
                d = SYNC[n];
                mu = 2e-4f;
            } else if (train_fs && known(n)) {
                d = ref_avg_[n] > 0 ? 5.0f : -5.0f;
                mu = 2e-4f;
            } else if (!is_fs && dd_enabled_) {
                d = slice(y);
                mu = 3e-5f;
            } else {
                continue;
            }
            float e = d - y;
            for (int k = 0; k < NTAPS; k++) {
                w_[k] += mu * e * x[-k];
            }
            err_acc += e * e;
            err_n++;
        }
        if (err_n > 0 && (n_is_data(is_fs))) {
            mse_ = 0.9f * mse_ + 0.1f * (err_acc / err_n);
        }
        if (train_fs) {
            fs_mse_ = 0.5f * fs_mse_ + 0.5f * (err_n ? err_acc / err_n : 100.0f);
            dd_enabled_ = fs_mse_ < 21.0f / 10.0f;  // ~10 dB on the training symbols
        }

        if (fs_locked_ && fields_learned_ >= 2 && !is_fs) {
            int seg_index = (int)((index - fs_slot_ - 1) % FIELD_SEGS);
            if (seg_index < 0) seg_index += FIELD_SEGS;
            cb(seg_index, out + 4);
        }
    }

    bool n_is_data(bool is_fs) const { return !is_fs; }

    // field sync positions with a constant training value (PN511, PN63 #1 and #3, mode bits)
    static bool known(int n) {
        return (n >= 4 && n < 578) || (n >= 641 && n < 728);
    }

    std::vector<float> w_, ref_avg_, slot_score_, hist_;
    std::vector<std::vector<float>> ring_;
    long seg_count_ = 0;
    int fs_slot_ = -1, fields_learned_ = 0;
    bool fs_locked_ = false, dd_enabled_ = false;
    float mse_ = 21.0f, fs_mse_ = 100.0f;
};

}  // namespace atsc
