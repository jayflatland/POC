/*
Aviation (VHF airband) scanner for the B200.

Watches every AM channel in the airband at once from a single B200
tuning (up to 38.4 MS/s), plays the newest transmission through pacat, and logs every
transmission (played or not).

    108-118 MHz   navigation: VOR / ILS beacons (continuous; skipped)
    118-137 MHz   voice: tower, ground, approach, center, ATIS, 121.5

    ./build/airband                         # 108-137 MHz, 25 kHz channels
    ./build/airband --start 118 --end 137
    ./build/airband --lockout 121.5,123.45
    ./build/airband --spacing 8.33          # European 8.33 kHz channels
    ./build/airband --freq 124.775          # just listen to one channel (e.g. local ATIS)

Sample rate: by default the smallest multiple of 0.96 MS/s that covers
--start..--end (38.4 MS/s for the full band). Over USB 2 the B200 streams
9.6 MS/s cleanly (11.52 overflows), so scan a slice of up to ~7.6 MHz:

    ./build/airband --start 118 --end 124   # 7.68 MS/s, fits USB 2
    ./build/airband --start 118 --end 125.6 # 9.60 MS/s, the USB 2 maximum
    ./build/airband --rate 9.6 ...          # or force a rate (MS/s)

Detection: FFT power in each channel vs. that channel's learned noise
floor AND vs. the current median of its ~1 MHz neighborhood (so broadband
noise that lifts every channel doesn't open the squelch), with open/close
hysteresis. Channels that are on at startup, on for longer than
--continuous seconds, or open more than half the time (beacons, ATIS
loops, spurs) are skipped until they go quiet.

Playback: the newest open channel gets the speaker and keeps it through a
--hang second gap (to catch the reply on the same frequency).

Demod: shift channel to 0 Hz -> CIC (order 2) decimate by FS/240k to 240 kHz
-> FIR decimate by 5 to 48 kHz -> envelope -> carrier AGC -> audio.

Keys: s skip this transmission, l lock out this channel, u clear lockouts,
+/- volume, q quit.
*/

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <poll.h>
#include <sstream>
#include <string>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <boost/program_options.hpp>
#include <fftw3.h>
#include <uhd/usrp/multi_usrp.hpp>

using cf = std::complex<float>;
namespace po = boost::program_options;

static const double PI = 3.14159265358979323846;
static const double MAX_FS = 38.4e6;
static const double FS_STEP = 960e3;  // rates are multiples of this (so DECIM1 is an integer)
static double FS = MAX_FS;            // set from --rate / the scan range in main()
static int DECIM1 = 160;              // CIC: FS -> 240 kS/s
static const int DECIM2 = 5;          // FIR: 240 kS/s -> 48 kS/s
static const double AUDIO_RATE = 48e3;
static const double USABLE = 0.8;
static const double GRID_ANCHOR = 118e6;

struct Config {
    double start, end, spacing;
    double gain, threshold_db, hysteresis_db;
    double hang, continuous_sec;
    std::string antenna, args, file, audio_cmd, lockout;
    double manual;
    bool no_audio;
};
static Config cfg;

static std::atomic<bool> g_stop{false};
static std::atomic<long> g_overflows{0};
static std::atomic<float> g_volume{0.6f};
static std::atomic<bool> g_key_skip{false}, g_key_lock{false}, g_key_unlock{false};

static void on_sigint(int) {
    g_stop = true;
}

static double wall_now() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

static std::string clock_str(double t) {
    time_t s = (time_t)t;
    struct tm tm;
    localtime_r(&s, &tm);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%d", tm.tm_hour, tm.tm_min, tm.tm_sec, (int)((t - s) * 10));
    return buf;
}

static double parse_mhz(const std::string &s) {
    double v = std::stod(s);
    return v < 1e5 ? v * 1e6 : v;  // accept 118, 118.0, or 118000000
}


// -------------------- audio out (pipe to pacat) --------------------

class AudioOut {
public:
    void start(const std::string &cmd) {
        pipe_ = popen(cmd.c_str(), "w");
        if (!pipe_) {
            std::cerr << "Couldn't start audio command: " << cmd << std::endl;
        }
        thread_ = std::thread([this] { run(); });
    }

    void push(std::vector<int16_t> &&samples) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queued_ > AUDIO_RATE / 2) {
            return;  // sound card fell behind: drop rather than stall the radio
        }
        queued_ += samples.size();
        queue_.push_back(std::move(samples));
        cv_.notify_one();
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
        }
        cv_.notify_one();
        if (thread_.joinable()) {
            thread_.join();
        }
        if (pipe_) {
            pclose(pipe_);
        }
    }

private:
    void run() {
        while (true) {
            std::vector<int16_t> buf;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return !queue_.empty() || !running_; });
                if (!running_) {
                    break;
                }
                buf = std::move(queue_.front());
                queue_.pop_front();
                queued_ -= buf.size();
            }
            if (pipe_ && !write_failed_) {
                if (fwrite(buf.data(), sizeof(int16_t), buf.size(), pipe_) != buf.size() || fflush(pipe_) != 0) {
                    std::cerr << "Audio pipe closed, continuing without audio" << std::endl;
                    write_failed_ = true;
                }
            }
        }
    }

    FILE *pipe_ = nullptr;
    bool write_failed_ = false;
    bool running_ = true;
    size_t queued_ = 0;
    std::deque<std::vector<int16_t>> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
};

static AudioOut g_audio;


// -------------------- AM demodulator for one channel --------------------

static std::vector<float> design_lowpass(double fs, double cutoff, double transition) {
    int n = (int)std::ceil(5.5 * fs / transition) | 1;
    int m = n / 2;
    std::vector<float> taps(n);
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        double x = i - m;
        double fc = cutoff / fs;
        double sinc = (x == 0) ? 2.0 * fc : std::sin(2.0 * PI * fc * x) / (PI * x);
        double w = 0.42 - 0.5 * std::cos(2.0 * PI * i / (n - 1)) + 0.08 * std::cos(4.0 * PI * i / (n - 1));
        taps[i] = (float)(sinc * w);
        sum += taps[i];
    }
    for (auto &t : taps) {
        t /= sum;
    }
    return taps;
}

class AmDemod {
public:
    void init(double spacing) {
        double cutoff = spacing >= 20e3 ? 8e3 : 3.5e3;
        double trans = spacing >= 20e3 ? 4e3 : 1.5e3;
        taps_ = design_lowpass(FS / DECIM1, cutoff, trans);
        ntaps_ = taps_.size();
        hist_.assign(2 * ntaps_, cf(0, 0));
        lp_alpha_ = (float)(1.0 - std::exp(-2.0 * PI * 4000.0 / AUDIO_RATE));
        hp_alpha_ = (float)std::exp(-2.0 * PI * 150.0 / AUDIO_RATE);
        agc_alpha_ = (float)(1.0 - std::exp(-1.0 / (0.15 * AUDIO_RATE)));
    }

    void tune(double offset_hz) {
        step_ = std::polar(1.0f, (float)(-2.0 * PI * offset_hz / FS));
        // restart the AGC once the filters have flushed the old channel (~2 ms)
        warmup_ = (int)(0.002 * AUDIO_RATE);
        warm_sum_ = 0.0f;
        carrier_ = 0.0f;
    }

    // always runs (so sample accounting stays exact); audible=false outputs silence
    void process(const cf *x, size_t n, bool audible, std::vector<int16_t> &out) {
        const float cic_gain = 1.0f / ((float)DECIM1 * DECIM1);
        for (size_t i = 0; i < n; i++) {
            cf v = x[i] * phasor_;
            phasor_ *= step_;
            // order-2 CIC as triangular weights over two periods: y = D*S - T + T_prev
            s_ += v;
            t_ += v * (float)idx_;
            if (++idx_ < DECIM1) {
                continue;
            }
            cf y = ((float)DECIM1 * s_ - t_ + t_prev_) * cic_gain;
            t_prev_ = t_;
            s_ = t_ = 0.0f;
            idx_ = 0;
            fir_in(y, audible, out);
        }
        phasor_ /= std::abs(phasor_);
    }

private:
    void fir_in(cf y, bool audible, std::vector<int16_t> &out) {
        hist_[pos_] = hist_[pos_ + ntaps_] = y;
        pos_ = (pos_ + 1) % ntaps_;
        if (++count_ < DECIM2) {
            return;
        }
        count_ = 0;
        const cf *h = &hist_[pos_];
        float re = 0.0f, im = 0.0f;
        for (int k = 0; k < ntaps_; k++) {
            re += h[k].real() * taps_[k];
            im += h[k].imag() * taps_[k];
        }
        float env = std::sqrt(re * re + im * im);
        if (warmup_ > 0) {
            warm_sum_ += env;
            if (--warmup_ == 0) {
                carrier_ = warm_sum_ / (0.002f * (float)AUDIO_RATE);
            }
            out.push_back(0);
            return;
        }
        // with the squelch closed the carrier is gone; freeze the AGC instead of chasing noise
        if (audible) {
            carrier_ += agc_alpha_ * (env - carrier_);
        }
        float a = (env - carrier_) / std::max(carrier_, 1e-9f);  // modulation, normalized to the carrier
        lp_ += lp_alpha_ * (a - lp_);
        float hp = lp_ - hp_prev_in_ + hp_alpha_ * hp_out_;  // DC blocker
        hp_prev_in_ = lp_;
        hp_out_ = hp;
        float s = audible ? hp * g_volume * 32767.0f : 0.0f;
        out.push_back((int16_t)std::max(-32767.0f, std::min(32767.0f, s)));
    }

    cf phasor_{1.0f, 0.0f}, step_{1.0f, 0.0f};
    cf s_{0.0f, 0.0f}, t_{0.0f, 0.0f}, t_prev_{0.0f, 0.0f};
    int idx_ = 0;
    std::vector<float> taps_;
    std::vector<cf> hist_;
    int ntaps_ = 0, pos_ = 0, count_ = 0;
    float carrier_ = 0.0f, lp_ = 0.0f, lp_alpha_ = 0.5f, agc_alpha_ = 0.001f;
    float hp_alpha_ = 0.98f, hp_prev_in_ = 0.0f, hp_out_ = 0.0f;
    int warmup_ = 0;
    float warm_sum_ = 0.0f;
};


// -------------------- scanner --------------------

struct Channel {
    double freq = 0.0;
    int b0 = 0, b1 = 0;
    float floor_db = 0.0f, floor_lin = 0.0f;
    bool open = false;
    double open_since = 0.0, last_open = 0.0, below_since = 0.0;
    float peak_snr = 0.0f;
    bool continuous = false;  // beacon / ATIS / stuck carrier: skipped while it lasts
    float duty = 0.0f;        // fraction of recent time the squelch was open (~15 s average)
    bool locked = false;
    bool skip = false;        // skip the current transmission only
    bool heard = false;       // was audible for part of the current transmission
    long count = 0, played = 0;
    double airtime = 0.0;
    float max_snr = 0.0f;
};

class Scanner {
public:
    void init(double center) {
        center_ = center;
        df_ = FS / N;
        frame_time_ = (double)N * AVG / FS;
        open_lin_ = (float)std::pow(10.0, cfg.threshold_db / 10.0);
        close_lin_ = (float)std::pow(10.0, (cfg.threshold_db - cfg.hysteresis_db) / 10.0);
        duty_alpha_ = (float)(frame_time_ / 15.0);

        in_ = fftwf_alloc_complex(N);
        out_ = fftwf_alloc_complex(N);
        plan_ = fftwf_plan_dft_1d(N, in_, out_, FFTW_FORWARD, FFTW_MEASURE);
        window_.resize(N);
        for (int i = 0; i < N; i++) {
            window_[i] = (float)(0.5 - 0.5 * std::cos(2.0 * PI * i / N));
        }
        acc_.assign(N, 0.0f);

        // channels on the grid anchored at 118.000 MHz
        double half = 0.35 * cfg.spacing;
        long k0 = (long)std::ceil((cfg.start - GRID_ANCHOR) / cfg.spacing - 1e-9);
        for (long k = k0;; k++) {
            double f = GRID_ANCHOR + k * cfg.spacing;
            if (f > cfg.end + 1.0) {
                break;
            }
            if (std::abs(f - center_) > USABLE * FS / 2.0) {
                continue;
            }
            Channel c;
            c.freq = f;
            c.b0 = (int)std::ceil((f - half - center_) / df_ + N / 2);
            c.b1 = (int)std::floor((f + half - center_) / df_ + N / 2);
            if (c.b1 < c.b0) {
                c.b1 = c.b0;
            }
            chans_.push_back(c);
        }
        cal_.assign(chans_.size() * CAL_FRAMES, 0.0f);

        std::stringstream ss(cfg.lockout);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (tok.empty()) {
                continue;
            }
            double f = parse_mhz(tok);
            for (auto &c : chans_) {
                if (std::abs(c.freq - f) < cfg.spacing / 2.0) {
                    c.locked = true;
                }
            }
        }
        demod_.init(cfg.spacing);
        demod_.tune(0.0);
        if (cfg.manual > 0.0) {
            for (size_t i = 0; i < chans_.size(); i++) {
                if (std::abs(chans_[i].freq - cfg.manual) < cfg.spacing / 2.0) {
                    manual_ = (int)i;
                }
            }
            if (manual_ < 0) {
                fprintf(stderr, "--freq %.3f MHz isn't a channel in the scan range\n", cfg.manual / 1e6);
                exit(1);
            }
        }
    }

    size_t chunk() const { return (size_t)N * AVG; }
    size_t num_channels() const { return chans_.size(); }
    double first_freq() const { return chans_.front().freq; }
    double last_freq() const { return chans_.back().freq; }

    // one detection frame worth of samples
    void process_chunk(const cf *x, double t) {
        for (int f = 0; f < AVG; f++) {
            const cf *p = x + (size_t)f * N;
            for (int i = 0; i < N; i++) {
                cf v = p[i] * window_[i];
                in_[i][0] = v.real();
                in_[i][1] = v.imag();
            }
            fftwf_execute(plan_);
            for (int b = 0; b < N; b++) {
                int k = (b + N / 2) % N;
                acc_[b] += out_[k][0] * out_[k][0] + out_[k][1] * out_[k][1];
            }
        }
        handle_frame(t);
        std::fill(acc_.begin(), acc_.end(), 0.0f);

        audio_buf_.clear();
        // manual mode is squelch-off, like a plain receiver
        bool audible = cur_ >= 0 && !cfg.no_audio &&
                       (cur_ == manual_ || (chans_[cur_].open && !chans_[cur_].continuous));
        demod_.process(x, chunk(), audible, audio_buf_);
        if (!cfg.no_audio) {
            g_audio.push(std::move(audio_buf_));
            audio_buf_ = std::vector<int16_t>();
        }
    }

    void print_summary(double t) {
        for (auto &c : chans_) {
            if (c.open) {
                end_transmission(c, t);
            }
        }
        std::vector<const Channel *> rows;
        for (auto &c : chans_) {
            if (c.count > 0) {
                rows.push_back(&c);
            }
        }
        if (rows.empty()) {
            printf("\nNo transmissions heard.\n");
            return;
        }
        std::sort(rows.begin(), rows.end(), [](auto *a, auto *b) { return a->count > b->count; });
        printf("\n  FREQ (MHz)   TX   PLAYED   AIRTIME   MAX SNR\n");
        for (auto *c : rows) {
            printf("  %9.3f   %4ld   %5ld   %7.1f s   %5.1f dB%s\n", c->freq / 1e6, c->count, c->played, c->airtime,
                   c->max_snr, c->locked ? "   (locked out)" : "");
        }
    }

private:
    static const int N = 8192;       // 4.7 kHz bins
    static const int AVG = 4;        // 0.85 ms detection frames
    static const int CAL_FRAMES = 600;
    static const size_t GROUP = 40;              // channels per local-noise group (~1 MHz)
    static constexpr float LOCAL_OPEN = 4.0f;    // 6 dB over the group's current median to open
    static constexpr float LOCAL_STAY = 2.0f;    // 3 dB to stay open
    static constexpr double PREEMPT_AFTER = 10.0; // a transmission this long yields to a new one

    void handle_frame(double t) {
        frames_++;
        if (!calibrated_) {
            for (size_t i = 0; i < chans_.size(); i++) {
                cal_[i * CAL_FRAMES + cal_count_] = 10.0f * std::log10(chan_power(chans_[i]) + 1e-30f);
            }
            if (++cal_count_ == CAL_FRAMES) {
                calibrate(t);
            }
            return;
        }

        handle_keys(t);

        power_.resize(chans_.size());
        for (size_t i = 0; i < chans_.size(); i++) {
            power_[i] = chan_power(chans_[i]);
        }
        // current median power of each ~1 MHz group: broadband noise raises it along with every
        // channel, while a real transmission sits in one channel well above it
        size_t ngroups = (chans_.size() + GROUP - 1) / GROUP;
        group_med_.resize(ngroups);
        std::vector<float> tmp;
        for (size_t g = 0; g < ngroups; g++) {
            tmp.assign(power_.begin() + g * GROUP, power_.begin() + std::min(chans_.size(), (g + 1) * GROUP));
            std::nth_element(tmp.begin(), tmp.begin() + tmp.size() / 2, tmp.end());
            group_med_[g] = tmp[tmp.size() / 2];
        }
        for (size_t i = 0; i < chans_.size(); i++) {
            Channel &c = chans_[i];
            float p = power_[i];
            float snr_lin = p / c.floor_lin;
            float local = group_med_[i / GROUP];
            bool can_open = snr_lin > open_lin_ && p > local * LOCAL_OPEN && !leakage(i) && has_carrier(c, p);
            bool can_stay = snr_lin > close_lin_ && p > local * LOCAL_STAY;
            c.duty += duty_alpha_ * ((c.open ? 1.0f : 0.0f) - c.duty);
            if (!c.open) {
                if (can_open) {
                    c.open = true;
                    c.open_since = c.last_open = t;
                    c.below_since = 0.0;
                    c.peak_snr = 0.0f;
                    c.heard = false;
                }
            } else if (can_stay) {
                c.last_open = t;
                c.below_since = 0.0;
            } else if (c.below_since == 0.0) {
                c.below_since = t;
            } else if (t - c.below_since > 0.08) {  // debounce short fades
                end_transmission(c, t);
            }
            if (c.open) {
                c.peak_snr = std::max(c.peak_snr, 10.0f * std::log10(snr_lin));
                bool long_on = t - c.open_since > cfg.continuous_sec;
                if (!c.continuous && (long_on || c.duty > 0.5f)) {
                    c.continuous = true;
                    if (long_on) {
                        printf("%s  [continuous] %8.3f MHz  on for %.0f s, skipping it until it stops\n",
                               clock_str(t).c_str(), c.freq / 1e6, t - c.open_since);
                    } else {
                        printf("%s  [continuous] %8.3f MHz  on %.0f%% of the time, skipping it until it settles\n",
                               clock_str(t).c_str(), c.freq / 1e6, c.duty * 100.0f);
                    }
                    fflush(stdout);
                }
            } else {
                // only quiet channels move their floor; hot channels barely do
                float db = 10.0f * std::log10(p + 1e-30f);
                if (frames_ % 8 == 0) {
                    c.floor_db += 0.01f * (db - c.floor_db);
                    c.floor_lin = std::pow(10.0f, c.floor_db / 10.0f);
                }
            }
        }
        if (frames_ % 2048 == 0) {
            clamp_floors();
        }

        choose_channel(t);
        if (cur_ >= 0 && chans_[cur_].open && (t - chans_[cur_].last_open < cfg.hang)) {
            chans_[cur_].heard = true;
        }
    }

    // a strong signal spills into the next channel or two; ignore anything 20+ dB under a neighbor
    bool leakage(size_t i) const {
        for (size_t k = (i >= 2 ? i - 2 : 0); k < std::min(chans_.size(), i + 3); k++) {
            if (k != i && power_[k] > power_[i] * 100.0f) {
                return true;
            }
        }
        return false;
    }

    // AM keeps most of its power in the carrier: the strongest two adjacent bins should hold
    // most of the channel's power, where noise spreads across all of them
    bool has_carrier(const Channel &c, float total) const {
        if (c.b1 - c.b0 < 2) {
            return true;  // too few bins to tell
        }
        float best = 0.0f;
        for (int b = c.b0; b < c.b1; b++) {
            best = std::max(best, acc_[b] + acc_[b + 1]);
        }
        return best > 0.6f * total;
    }

    float chan_power(const Channel &c) const {
        float s = 0.0f;
        for (int b = c.b0; b <= c.b1; b++) {
            s += acc_[b];
        }
        return s;
    }

    void calibrate(double t) {
        std::vector<float> tmp(CAL_FRAMES);
        for (size_t i = 0; i < chans_.size(); i++) {
            std::copy(&cal_[i * CAL_FRAMES], &cal_[(i + 1) * CAL_FRAMES], tmp.begin());
            std::nth_element(tmp.begin(), tmp.begin() + CAL_FRAMES / 2, tmp.end());
            chans_[i].floor_db = tmp[CAL_FRAMES / 2];
        }
        cal_.clear();
        cal_.shrink_to_fit();
        std::vector<float> median_db(chans_.size());
        for (size_t i = 0; i < chans_.size(); i++) {
            median_db[i] = chans_[i].floor_db;
        }
        clamp_floors();
        calibrated_ = true;

        // a channel whose median during calibration sat well over its (clamped) floor was on
        // more than half that time: a beacon, ATIS, or a spur. So was anything on right now.
        int startup = 0;
        for (size_t i = 0; i < chans_.size(); i++) {
            Channel &c = chans_[i];
            float p = chan_power(c);
            bool was_on = median_db[i] - c.floor_db > (float)cfg.threshold_db - 3.0f;
            if (was_on || p / c.floor_lin > open_lin_) {
                c.open = c.continuous = true;
                c.open_since = c.last_open = t;
                c.duty = 1.0f;
                startup++;
            }
        }
        fprintf(stderr, "Noise floor learned. %d channel(s) already on (beacons/ATIS), skipping those. Listening...\n\n",
                startup);
    }

    // a channel's floor can't sit more than 3 dB over the median of its neighbors,
    // so a carrier that's always on can't become its own noise floor
    void clamp_floors() {
        const int half = 20;
        std::vector<float> win;
        std::vector<float> ref(chans_.size());
        for (size_t i = 0; i < chans_.size(); i++) {
            win.clear();
            for (size_t k = (i > half ? i - half : 0); k < std::min(chans_.size(), i + half + 1); k++) {
                win.push_back(chans_[k].floor_db);
            }
            std::nth_element(win.begin(), win.begin() + win.size() / 2, win.end());
            ref[i] = win[win.size() / 2];
        }
        for (size_t i = 0; i < chans_.size(); i++) {
            chans_[i].floor_db = std::min(chans_[i].floor_db, ref[i] + 3.0f);
            chans_[i].floor_lin = std::pow(10.0f, chans_[i].floor_db / 10.0f);
        }
    }

    // a channel must hold its squelch open for 250 ms before it gets the speaker: radios key the
    // carrier before anyone talks, so this costs no speech but drops noise blips
    bool eligible(const Channel &c, double t) const {
        return c.open && !c.continuous && !c.locked && !c.skip && t - c.open_since >= 0.25;
    }

    void choose_channel(double t) {
        if (manual_ >= 0) {
            if (cur_ != manual_) {
                cur_ = manual_;
                demod_.tune(chans_[cur_].freq - center_);
                printf("%s  >> %8.3f MHz (manual)\n", clock_str(t).c_str(), chans_[cur_].freq / 1e6);
                fflush(stdout);
            }
            return;
        }
        if (cur_ >= 0) {
            Channel &c = chans_[cur_];
            bool keep = !c.locked && !c.skip && !c.continuous && (c.open || t - c.last_open < cfg.hang);
            bool long_tx = c.open && t - c.open_since > PREEMPT_AFTER;
            if (keep && (!c.open || long_tx)) {
                // in the hang time, or on something that's been talking a long while:
                // move to anything that's newly talking
                for (auto &o : chans_) {
                    if (&o != &c && eligible(o, t) && (!c.open || o.open_since > c.open_since + PREEMPT_AFTER)) {
                        keep = false;
                        break;
                    }
                }
            }
            if (keep) {
                return;
            }
            cur_ = -1;
        }
        int best = -1;
        for (size_t i = 0; i < chans_.size(); i++) {
            if (eligible(chans_[i], t) && (best < 0 || chans_[i].open_since > chans_[best].open_since)) {
                best = (int)i;
            }
        }
        if (best >= 0) {
            cur_ = best;
            demod_.tune(chans_[best].freq - center_);
            printf("%s  >> %8.3f MHz\n", clock_str(t).c_str(), chans_[best].freq / 1e6);
            fflush(stdout);
        }
    }

    void end_transmission(Channel &c, double t) {
        double dur = c.last_open - c.open_since + frame_time_;
        c.open = false;
        c.skip = false;
        if (c.continuous) {
            if (c.duty > 0.2f) {
                return;  // still flickering on and off: stay skipped
            }
            printf("%s  [continuous ended] %8.3f MHz\n", clock_str(t).c_str(), c.freq / 1e6);
            fflush(stdout);
            c.continuous = false;
            return;
        }
        if (dur < 0.25) {
            return;  // a blip, not a transmission
        }
        c.count++;
        c.airtime += dur;
        c.max_snr = std::max(c.max_snr, c.peak_snr);
        if (c.heard) {
            c.played++;
        }
        printf("%s     %8.3f MHz  %5.1f s  snr %4.1f dB  %s\n", clock_str(c.open_since).c_str(), c.freq / 1e6, dur,
               c.peak_snr, c.heard ? "played" : (c.locked ? "locked out" : "missed"));
        fflush(stdout);
    }

    void handle_keys(double t) {
        if (g_key_skip.exchange(false) && cur_ >= 0) {
            chans_[cur_].skip = true;
            printf("%s  (skipping %.3f MHz)\n", clock_str(t).c_str(), chans_[cur_].freq / 1e6);
        }
        if (g_key_lock.exchange(false) && cur_ >= 0) {
            chans_[cur_].locked = true;
            printf("%s  (locked out %.3f MHz)\n", clock_str(t).c_str(), chans_[cur_].freq / 1e6);
        }
        if (g_key_unlock.exchange(false)) {
            for (auto &c : chans_) {
                c.locked = false;
            }
            printf("%s  (lockouts cleared)\n", clock_str(t).c_str());
        }
        fflush(stdout);
    }

    double center_ = 0.0, df_ = 0.0, frame_time_ = 0.0;
    float open_lin_ = 10.0f, close_lin_ = 4.0f, duty_alpha_ = 1e-4f;
    fftwf_complex *in_ = nullptr, *out_ = nullptr;
    fftwf_plan plan_;
    std::vector<float> window_, acc_, cal_, power_, group_med_;
    std::vector<Channel> chans_;
    int cal_count_ = 0;
    bool calibrated_ = false;
    long frames_ = 0;
    int cur_ = -1;
    int manual_ = -1;
    AmDemod demod_;
    std::vector<int16_t> audio_buf_;
};

static Scanner g_scan;


// -------------------- sample blocks between rx and dsp threads --------------------

struct Block {
    std::vector<cf> data;
    size_t n = 0;
    double t0 = 0.0;
};

static std::mutex g_q_mutex;
static std::condition_variable g_q_cv;
static std::deque<std::unique_ptr<Block>> g_full, g_free;
static long g_dropped = 0;

static void rx_thread_fn(uhd::rx_streamer::sptr rx, size_t block_size) {
    uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    cmd.stream_now = true;
    rx->issue_stream_cmd(cmd);
    std::vector<cf> scratch(block_size);
    uhd::rx_metadata_t md;
    while (!g_stop) {
        std::unique_ptr<Block> blk;
        {
            std::lock_guard<std::mutex> lock(g_q_mutex);
            if (!g_free.empty()) {
                blk = std::move(g_free.front());
                g_free.pop_front();
            }
        }
        cf *dst = blk ? blk->data.data() : scratch.data();
        size_t n = rx->recv(dst, block_size, md, 1.0);
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
            g_overflows++;
        }
        if (!blk) {
            g_dropped++;
            continue;
        }
        if (n < block_size) {
            std::lock_guard<std::mutex> lock(g_q_mutex);
            g_free.push_back(std::move(blk));
            continue;
        }
        blk->n = n;
        blk->t0 = md.has_time_spec ? md.time_spec.get_real_secs() : wall_now();
        {
            std::lock_guard<std::mutex> lock(g_q_mutex);
            g_full.push_back(std::move(blk));
        }
        g_q_cv.notify_one();
    }
    rx->issue_stream_cmd(uhd::stream_cmd_t(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS));
}


// -------------------- keyboard --------------------

static struct termios g_saved_tty;
static bool g_tty_raw = false;

static void restore_tty() {
    if (g_tty_raw) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_tty);
        g_tty_raw = false;
    }
}

static void key_thread_fn() {
    if (!isatty(STDIN_FILENO)) {
        return;
    }
    tcgetattr(STDIN_FILENO, &g_saved_tty);
    struct termios raw = g_saved_tty;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    g_tty_raw = true;
    while (!g_stop) {
        struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
        if (poll(&pfd, 1, 100) <= 0) {
            continue;
        }
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) {
            continue;
        }
        switch (c) {
        case 's': g_key_skip = true; break;
        case 'l': g_key_lock = true; break;
        case 'u': g_key_unlock = true; break;
        case '+': case '=': g_volume = std::min(2.0f, g_volume + 0.1f); break;
        case '-': g_volume = std::max(0.0f, g_volume - 0.1f); break;
        case 'q': g_stop = true; break;
        }
    }
    restore_tty();
}


// -------------------- main --------------------

int main(int argc, char *argv[]) {
    std::string start_s, end_s;
    double spacing_khz, duration, rate_msps;
    po::options_description desc("Options");
    desc.add_options()
        ("help,h", "show help")
        ("start", po::value<std::string>(&start_s)->default_value("108"), "start frequency (MHz)")
        ("end", po::value<std::string>(&end_s)->default_value("137"), "end frequency (MHz); range must fit in ~30 MHz")
        ("rate", po::value<double>(&rate_msps)->default_value(0.0),
            "sample rate in MS/s, a multiple of 0.96 up to 38.4 (0 = smallest that covers the range; USB 2 tops out at 9.6)")
        ("spacing", po::value<double>(&spacing_khz)->default_value(25.0), "channel spacing in kHz (25 or 8.33)")
        ("threshold,t", po::value<double>(&cfg.threshold_db)->default_value(8.0), "squelch: dB over the channel's noise floor to open")
        ("hysteresis", po::value<double>(&cfg.hysteresis_db)->default_value(3.0), "squelch closes this many dB below --threshold")
        ("hang", po::value<double>(&cfg.hang)->default_value(2.0), "seconds to stay on a channel after it goes quiet")
        ("continuous", po::value<double>(&cfg.continuous_sec)->default_value(20.0), "seconds before a signal is treated as continuous and skipped")
        ("freq,f", po::value<double>(&cfg.manual)->default_value(0.0), "manual: just play this one channel (MHz), squelch off")
        ("lockout", po::value<std::string>(&cfg.lockout)->default_value(""), "comma-separated frequencies (MHz) to never play")
        ("gain,g", po::value<double>(&cfg.gain)->default_value(40.0), "rx gain (dB)")
        ("antenna,a", po::value<std::string>(&cfg.antenna)->default_value("TX/RX"), "rx antenna (TX/RX or RX2)")
        ("args", po::value<std::string>(&cfg.args)->default_value("type=b200"), "UHD device args")
        ("no-audio", po::bool_switch(&cfg.no_audio), "log transmissions only")
        ("duration", po::value<double>(&duration)->default_value(0.0), "stop after this many seconds (0 = until q / Ctrl-C)")
        ("file", po::value<std::string>(&cfg.file)->default_value(""), "process an fc32 recording instead (needs --center and --rate)")
        ("center", po::value<double>()->default_value(0.0), "center frequency of --file (MHz)")
        ("audio-cmd", po::value<std::string>(&cfg.audio_cmd)->default_value(
            "pacat --raw --format=s16le --rate=48000 --channels=1 --latency-msec=150 --client-name=airband"),
            "command that plays raw s16le 48 kHz mono audio from stdin");
    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
        cfg.start = parse_mhz(start_s);
        cfg.end = parse_mhz(end_s);
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n" << desc << std::endl;
        return 1;
    }
    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 0;
    }
    cfg.spacing = spacing_khz < 10.0 ? 25e3 / 3.0 : spacing_khz * 1e3;
    if (cfg.manual > 0.0 && cfg.manual < 1e5) {
        cfg.manual *= 1e6;
    }
    if (cfg.end <= cfg.start) {
        std::cerr << "--end must be above --start" << std::endl;
        return 1;
    }
    if (rate_msps > 0.0) {
        FS = std::round(rate_msps * 1e6 / FS_STEP) * FS_STEP;
        if (FS < FS_STEP || FS > MAX_FS || std::abs(FS - rate_msps * 1e6) > 1.0) {
            std::cerr << "--rate must be a multiple of 0.96 MS/s between 0.96 and 38.4" << std::endl;
            return 1;
        }
    } else if (!cfg.file.empty()) {
        FS = MAX_FS;  // the old fixed rate, for existing recordings
    } else {
        double need = (cfg.end - cfg.start + 2 * cfg.spacing) / USABLE;
        FS = std::min(MAX_FS, std::ceil(need / FS_STEP - 1e-9) * FS_STEP);
    }
    DECIM1 = (int)std::lround(FS / (AUDIO_RATE * DECIM2));
    if (cfg.end - cfg.start > USABLE * FS - 2 * cfg.spacing) {
        std::cerr << "Range must be positive and at most " << (USABLE * FS - 2 * cfg.spacing) / 1e6
                  << " MHz wide at " << FS / 1e6 << " MS/s (one B200 tuning)" << std::endl;
        return 1;
    }

    // put DC halfway between two channels so the B200's DC spike doesn't sit on one
    double mid = (cfg.start + cfg.end) / 2.0;
    double center = GRID_ANCHOR + (std::round((mid - GRID_ANCHOR) / cfg.spacing) + 0.5) * cfg.spacing;

    uhd::usrp::multi_usrp::sptr usrp;
    uhd::rx_streamer::sptr rx;
    if (cfg.file.empty()) {
        usrp = uhd::usrp::multi_usrp::make(cfg.args);
        usrp->set_rx_rate(FS, 0);
        usrp->set_rx_freq(uhd::tune_request_t(center), 0);
        usrp->set_rx_gain(cfg.gain, 0);
        usrp->set_rx_antenna(cfg.antenna, 0);
        usrp->set_rx_bandwidth(FS, 0);
        usrp->set_time_now(uhd::time_spec_t(wall_now()));
        center = usrp->get_rx_freq(0);
        if (std::abs(usrp->get_rx_rate(0) - FS) > 1.0) {
            std::cerr << "B200 gave " << usrp->get_rx_rate(0) << " S/s instead of " << FS << std::endl;
            return 1;
        }
        uhd::stream_args_t stream_args("fc32", "sc16");
        stream_args.channels = {0};
        rx = usrp->get_rx_stream(stream_args);
    } else {
        center = vm["center"].as<double>() * 1e6;
        if (center <= 0.0) {
            std::cerr << "--file needs --center" << std::endl;
            return 1;
        }
    }

    g_scan.init(center);
    fprintf(stderr, "Airband scanner: %zu channels, %.3f - %.3f MHz (%.2f kHz spacing), tuned to %.4f MHz at %.2f MS/s, "
            "gain %.0f dB\n", g_scan.num_channels(), g_scan.first_freq() / 1e6, g_scan.last_freq() / 1e6,
            cfg.spacing / 1e3, center / 1e6, FS / 1e6, usrp ? usrp->get_rx_gain(0) : 0.0);
    fprintf(stderr, "Keys: s skip  l lockout  u clear lockouts  +/- volume  q quit\n");
    fprintf(stderr, "Learning the noise floor...\n");

    if (!cfg.no_audio) {
        g_audio.start(cfg.audio_cmd);
    }
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);
    std::thread keys(key_thread_fn);

    size_t chunk = g_scan.chunk();
    double t_start = wall_now();
    double t_last = t_start;
    if (!cfg.file.empty()) {
        std::ifstream in(cfg.file, std::ios::binary);
        std::vector<cf> buf(chunk);
        double t = t_start;
        while (!g_stop && in.read((char *)buf.data(), chunk * sizeof(cf))) {
            g_scan.process_chunk(buf.data(), t);
            t += chunk / FS;
        }
        t_last = t;
    } else {
        size_t block_size = chunk * 24;  // ~20 ms
        for (int i = 0; i < 48; i++) {
            auto b = std::make_unique<Block>();
            b->data.resize(block_size);
            g_free.push_back(std::move(b));
        }
        std::thread rx_thread(rx_thread_fn, rx, block_size);
        while (!g_stop) {
            std::unique_ptr<Block> blk;
            {
                std::unique_lock<std::mutex> lock(g_q_mutex);
                g_q_cv.wait_for(lock, std::chrono::milliseconds(200), [] { return !g_full.empty(); });
                if (!g_full.empty()) {
                    blk = std::move(g_full.front());
                    g_full.pop_front();
                }
            }
            if (blk) {
                for (size_t off = 0; off + chunk <= blk->n; off += chunk) {
                    t_last = blk->t0 + off / FS;
                    g_scan.process_chunk(blk->data.data() + off, t_last);
                }
                std::lock_guard<std::mutex> lock(g_q_mutex);
                g_free.push_back(std::move(blk));
            }
            if (duration > 0.0 && wall_now() - t_start >= duration) {
                g_stop = true;
            }
        }
        rx_thread.join();
    }
    g_stop = true;
    keys.join();
    restore_tty();

    g_scan.print_summary(t_last);
    fprintf(stderr, "\nRan %.0f s, %ld overflows, %ld blocks dropped\n", wall_now() - t_start, g_overflows.load(),
            g_dropped);
    if (!cfg.no_audio) {
        g_audio.stop();
    }
    return 0;
}
