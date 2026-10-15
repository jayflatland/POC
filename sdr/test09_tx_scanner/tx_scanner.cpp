/*
Transmission scanner for the B200.

Watches a frequency range and prints every transmission it detects: time,
center frequency, bandwidth, duration, and SNR.

If the range fits in the B200's instantaneous bandwidth it streams
continuously on one tuning, so nothing is missed. Wider ranges are swept by
hopping as fast as the radio allows.

Detection: averaged FFT power per bin vs. a learned per-bin noise floor.
Adjacent hot bins form a signal, tracked over time until it goes quiet.
A signal must clear --threshold to start but only has to stay within
--hysteresis of it to continue, so a weak steady signal near the threshold
doesn't break up into hundreds of fake bursts. A frequency that keeps
bursting (8+ in 10 s) gets one [repeating] line and its later bursts are
only counted, until it's been quiet for 10 s (--show-all prints them all).
The floor is clamped to a few dB over the floor of nearby frequencies, so
a constant carrier can't hide by raising its own floor; signals that stay
on longer than --continuous are reported once as [continuous] instead of
as bursts.

    ./build/tx_scanner                                  # 902-928 MHz ISM band
    ./build/tx_scanner --start 430M --end 440M
    ./build/tx_scanner --start 100M --end 1000M --dwell 0.003
    ./build/tx_scanner --threshold 8 --duration 60

Ctrl-C prints a summary of the busiest frequencies.
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
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <boost/program_options.hpp>
#include <fftw3.h>
#include <uhd/usrp/multi_usrp.hpp>

using cf = std::complex<float>;
namespace po = boost::program_options;

static const double PI = 3.14159265358979323846;
static const double USABLE = 0.8;      // fraction of the sample rate that's flat enough to use
static const double CELL_HZ = 25e3;    // frequency grouping for the summary / continuous tracking

struct Config {
    double start, end;
    double rate;
    double gain;
    double threshold_db;
    double hysteresis_db;
    double dwell;
    double hold_ms;
    double continuous_sec;
    bool show_all;
    double duration;
    double max_rate;
    double center;
    int fft_n;
    int avg;
    std::string antenna, args;
};
static Config cfg;

static std::atomic<bool> g_stop{false};
static std::atomic<long> g_dropped{0};
static std::atomic<long> g_overflows{0};
static std::atomic<long> g_sweeps{0};

static void on_sigint(int) {
    g_stop = true;
}

static std::string clock_str(double t) {
    time_t s = (time_t)t;
    struct tm tm;
    localtime_r(&s, &tm);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, (int)((t - s) * 1000));
    return buf;
}

static double wall_now() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

static std::string dur_str(double sec) {
    char buf[32];
    if (sec < 1.0) {
        snprintf(buf, sizeof(buf), "%.1f ms", sec * 1e3);
    } else {
        snprintf(buf, sizeof(buf), "%.2f s", sec);
    }
    return buf;
}

static std::string bw_str(double hz) {
    char buf[32];
    if (hz < 1e6) {
        snprintf(buf, sizeof(buf), "%.0f kHz", hz / 1e3);
    } else {
        snprintf(buf, sizeof(buf), "%.2f MHz", hz / 1e6);
    }
    return buf;
}

// "915M", "2.4G", "500k", or plain Hz
static double parse_freq(const std::string &s) {
    size_t pos;
    double v = std::stod(s, &pos);
    if (pos < s.size()) {
        char c = s[pos];
        if (c == 'k' || c == 'K') v *= 1e3;
        else if (c == 'm' || c == 'M') v *= 1e6;
        else if (c == 'g' || c == 'G') v *= 1e9;
    }
    return v;
}


// -------------------- sample blocks between rx and dsp threads --------------------

struct Block {
    int seg = 0;
    double t0 = 0.0;     // wall time of the first sample
    bool fresh = false;  // not contiguous with the previous block for this segment
    bool sweep_end = false;
    std::vector<cf> data;
    size_t n = 0;
};

class BlockQueue {
public:
    void init(size_t count, size_t block_size) {
        for (size_t i = 0; i < count; i++) {
            auto b = std::make_unique<Block>();
            b->data.resize(block_size);
            free_.push_back(std::move(b));
        }
    }
    std::unique_ptr<Block> get_free() {
        std::lock_guard<std::mutex> lock(m_);
        if (free_.empty()) {
            return nullptr;
        }
        auto b = std::move(free_.back());
        free_.pop_back();
        return b;
    }
    void put_free(std::unique_ptr<Block> b) {
        std::lock_guard<std::mutex> lock(m_);
        free_.push_back(std::move(b));
    }
    void push(std::unique_ptr<Block> b) {
        {
            std::lock_guard<std::mutex> lock(m_);
            full_.push_back(std::move(b));
        }
        cv_.notify_one();
    }
    // returns nullptr once stopped and drained
    std::unique_ptr<Block> pop() {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait_for(lock, std::chrono::milliseconds(200), [this] { return !full_.empty() || done_; });
        if (full_.empty()) {
            return nullptr;
        }
        auto b = std::move(full_.front());
        full_.pop_front();
        return b;
    }
    void finish() {
        {
            std::lock_guard<std::mutex> lock(m_);
            done_ = true;
        }
        cv_.notify_all();
    }
    bool done() {
        std::lock_guard<std::mutex> lock(m_);
        return done_ && full_.empty();
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<std::unique_ptr<Block>> free_;
    std::deque<std::unique_ptr<Block>> full_;
    bool done_ = false;
};

static BlockQueue g_queue;


// -------------------- detection --------------------

struct Tx {
    double t_start = 0.0, t_last = 0.0;
    int lo = 0, hi = 0;
    float peak_p = 0.0f;
    float peak_floor = 1.0f;
    double sum_p = 0.0, sum_pb = 0.0;
    int missed = 0;
    bool cut_start = false;
    bool continuous = false;
    bool updated = false;
};

struct Segment {
    double center = 0.0;
    std::vector<uint8_t> usable;
    std::vector<float> floor_db, floor_lin;
    std::vector<float> cal;  // calibration frames, bin-major
    int cal_count = 0;
    bool calibrated = false;
    std::vector<Tx> active;
    long frames = 0;
};

struct CellStats {
    long count = 0;
    double total_dur = 0.0;
    float max_snr = 0.0f;
    double bw_sum = 0.0;
};

class Detector {
public:
    void init(double rate, int n, int avg, int nseg, bool hopping) {
        rate_ = rate;
        n_ = n;
        avg_ = avg;
        hopping_ = hopping;
        df_ = rate / n;
        frame_time_ = (double)n * avg / rate;
        hold_frames_ = std::max(1, (int)std::ceil(cfg.hold_ms * 1e-3 / frame_time_));
        cal_frames_ = hopping ? 48 : 400;
        thr_lin_ = (float)std::pow(10.0, cfg.threshold_db / 10.0);
        cont_lin_ = (float)std::pow(10.0, (cfg.threshold_db - cfg.hysteresis_db) / 10.0);

        in_ = fftwf_alloc_complex(n);
        out_ = fftwf_alloc_complex(n);
        plan_ = fftwf_plan_dft_1d(n, in_, out_, FFTW_FORWARD, FFTW_MEASURE);
        window_.resize(n);
        double wsum = 0.0;
        for (int i = 0; i < n; i++) {
            window_[i] = (float)(0.5 - 0.5 * std::cos(2.0 * PI * i / n));
            wsum += window_[i];
        }
        full_scale_ = (float)(wsum * wsum);  // |FFT|^2 of a full-scale tone
        acc_.assign(n, 0.0f);
        hot_.assign(n, 0);

        segs_.resize(nseg);
    }

    Segment &segment(int i) { return segs_[i]; }
    double frame_time() const { return frame_time_; }
    double bin_hz() const { return df_; }

    void setup_segment(int i, double center) {
        Segment &s = segs_[i];
        s.center = center;
        s.usable.assign(n_, 0);
        for (int b = 0; b < n_; b++) {
            double off = (b - n_ / 2) * df_;
            double f = center + off;
            bool in_band = std::abs(off) <= USABLE * rate_ / 2.0;
            bool in_range = f >= cfg.start && f <= cfg.end;
            bool dc = std::abs(b - n_ / 2) <= 1;
            s.usable[b] = in_band && in_range && !dc;
        }
        s.floor_db.assign(n_, -200.0f);
        s.floor_lin.assign(n_, 0.0f);
        s.cal.assign((size_t)n_ * cal_frames_, 0.0f);
    }

    void process(Block &blk) {
        Segment &s = segs_[blk.seg];
        if (blk.fresh) {
            flush(s, false);
            acc_count_ = 0;
            std::fill(acc_.begin(), acc_.end(), 0.0f);
        }
        block_t0_ = blk.t0;
        first_frame_of_block_ = true;
        for (size_t off = 0; off + n_ <= blk.n; off += n_) {
            for (int i = 0; i < n_; i++) {
                cf v = blk.data[off + i] * window_[i];
                in_[i][0] = v.real();
                in_[i][1] = v.imag();
            }
            fftwf_execute(plan_);
            for (int b = 0; b < n_; b++) {
                int k = (b + n_ / 2) % n_;  // fftshift
                acc_[b] += out_[k][0] * out_[k][0] + out_[k][1] * out_[k][1];
            }
            if (++acc_count_ == avg_) {
                double t = blk.t0 + (double)(off + n_) / rate_ - frame_time_;
                for (auto &v : acc_) {
                    v /= avg_;
                }
                handle_frame(s, t);
                std::fill(acc_.begin(), acc_.end(), 0.0f);
                acc_count_ = 0;
                first_frame_of_block_ = false;
            }
        }
        if (hopping_) {
            flush(s, true);  // dwell over: anything still active was cut off
            acc_count_ = 0;
            std::fill(acc_.begin(), acc_.end(), 0.0f);
        }
        if (blk.sweep_end) {
            end_sweep();
        }
    }

    void flush_all() {
        for (auto &s : segs_) {
            flush(s, true);
        }
    }

    void print_summary() {
        if (cells_.empty() && cont_cells_.empty()) {
            printf("\nNo transmissions detected.\n");
            return;
        }
        std::vector<std::pair<long, CellStats>> rows(cells_.begin(), cells_.end());
        std::sort(rows.begin(), rows.end(), [](auto &a, auto &b) { return a.second.count > b.second.count; });
        printf("\nBusiest frequencies (%.0f kHz cells), %ld bursts total:\n", CELL_HZ / 1e3, total_bursts_);
        printf("    FREQ (MHz)   BURSTS   AVG DUR     AVG BW    MAX SNR\n");
        for (size_t i = 0; i < rows.size() && i < 40; i++) {
            auto &c = rows[i].second;
            printf("  %11.3f   %6ld   %9s  %9s   %5.1f dB\n", rows[i].first * CELL_HZ / 1e6, c.count,
                   dur_str(c.total_dur / c.count).c_str(), bw_str(c.bw_sum / c.count).c_str(), c.max_snr);
        }
        if (!cont_cells_.empty()) {
            printf("\nContinuous signals seen:\n");
            for (auto &kv : cont_cells_) {
                printf("  %11.3f MHz  %s\n", kv.first * CELL_HZ / 1e6, kv.second.c_str());
            }
        }
    }

private:
    void calibrate(Segment &s) {
        // per-bin median of the calibration frames, then clamp to the local floor
        std::vector<float> tmp(cal_frames_);
        for (int b = 0; b < n_; b++) {
            std::copy(&s.cal[(size_t)b * cal_frames_], &s.cal[(size_t)(b + 1) * cal_frames_], tmp.begin());
            std::nth_element(tmp.begin(), tmp.begin() + cal_frames_ / 2, tmp.end());
            s.floor_db[b] = tmp[cal_frames_ / 2];
        }
        s.cal.clear();
        s.cal.shrink_to_fit();
        clamp_floor(s);
        s.calibrated = true;
    }

    // a bin's floor can't sit more than 3 dB over the median floor of the surrounding
    // ~100 bins, so a narrow constant signal stays visible
    void clamp_floor(Segment &s) {
        const int half = 50;
        std::vector<float> ref(n_);
        std::vector<float> win;
        for (int b = 0; b < n_; b++) {
            win.clear();
            for (int k = std::max(0, b - half); k <= std::min(n_ - 1, b + half); k++) {
                win.push_back(s.floor_db[k]);
            }
            std::nth_element(win.begin(), win.begin() + win.size() / 2, win.end());
            ref[b] = win[win.size() / 2];
        }
        for (int b = 0; b < n_; b++) {
            s.floor_db[b] = std::min(s.floor_db[b], ref[b] + 3.0f);
            s.floor_lin[b] = std::pow(10.0f, s.floor_db[b] / 10.0f);
        }
    }

    void handle_frame(Segment &s, double t) {
        s.frames++;
        if (!s.calibrated) {
            for (int b = 0; b < n_; b++) {
                s.cal[(size_t)b * cal_frames_ + s.cal_count] = 10.0f * std::log10(acc_[b] + 1e-30f);
            }
            if (++s.cal_count == cal_frames_) {
                calibrate(s);
            }
            return;
        }

        for (int b = 0; b < n_; b++) {
            // 2 = strong enough to start a transmission, 1 = strong enough to continue one
            hot_[b] = !s.usable[b] ? 0 : acc_[b] > s.floor_lin[b] * thr_lin_ ? 2 : acc_[b] > s.floor_lin[b] * cont_lin_ ? 1 : 0;
        }

        for (auto &tx : s.active) {
            tx.updated = false;
        }
        // contiguous runs of hot bins (1-bin gaps allowed) are one signal
        int b = 0;
        while (b < n_) {
            if (!hot_[b]) {
                b++;
                continue;
            }
            int lo = b, hi = b;
            while (hi + 1 < n_ && (hot_[hi + 1] || (hi + 2 < n_ && hot_[hi + 2]))) {
                hi += hot_[hi + 1] ? 1 : 2;
            }
            add_span(s, lo, hi, t);
            b = hi + 1;
        }

        for (size_t i = 0; i < s.active.size();) {
            Tx &tx = s.active[i];
            if (!tx.updated && ++tx.missed > hold_frames_) {
                finish(s, tx, false);
                s.active.erase(s.active.begin() + i);
                continue;
            }
            if (!hopping_ && !tx.continuous && tx.t_last - tx.t_start >= cfg.continuous_sec) {
                tx.continuous = true;
                double f = center_freq(s, tx);
                printf("%s  [continuous] %10.3f MHz  bw %9s  on since %s  snr %5.1f dB\n", clock_str(t).c_str(),
                       f / 1e6, bw_str((tx.hi - tx.lo + 1) * df_).c_str(), clock_str(tx.t_start).c_str(),
                       snr(tx));
                fflush(stdout);
            }
            i++;
        }

        // slow floor tracking: quiet bins follow the noise, hot bins barely move
        if (s.frames % 8 == 0) {
            for (int k = 0; k < n_; k++) {
                if (!s.usable[k]) {
                    continue;
                }
                float db = 10.0f * std::log10(acc_[k] + 1e-30f);
                float a = hot_[k] ? 0.0005f : 0.02f;
                s.floor_db[k] += a * (db - s.floor_db[k]);
                s.floor_lin[k] = std::pow(10.0f, s.floor_db[k] / 10.0f);
            }
        }
        if (t - last_repeat_check_ >= 1.0) {
            check_repeating_ended(t);
            last_repeat_check_ = t;
        }
        if (s.frames % 1024 == 0) {
            clamp_floor(s);
        }
    }

    void add_span(Segment &s, int lo, int hi, double t) {
        float peak = 0.0f;
        int peak_bin = lo;
        double sp = 0.0, spb = 0.0;
        bool can_start = false;
        for (int k = lo; k <= hi; k++) {
            if (!hot_[k]) {
                continue;
            }
            can_start |= hot_[k] == 2;
            sp += acc_[k];
            spb += acc_[k] * k;
            if (acc_[k] > peak) {
                peak = acc_[k];
                peak_bin = k;
            }
        }
        for (auto &tx : s.active) {
            if (lo <= tx.hi + 2 && hi >= tx.lo - 2) {
                tx.lo = std::min(tx.lo, lo);
                tx.hi = std::max(tx.hi, hi);
                tx.t_last = t;
                tx.sum_p += sp;
                tx.sum_pb += spb;
                if (peak > tx.peak_p) {
                    tx.peak_p = peak;
                    tx.peak_floor = s.floor_lin[peak_bin];
                }
                tx.missed = 0;
                tx.updated = true;
                return;
            }
        }
        if (!can_start) {
            return;
        }
        Tx tx;
        tx.t_start = tx.t_last = t;
        tx.lo = lo;
        tx.hi = hi;
        tx.peak_p = peak;
        tx.peak_floor = s.floor_lin[peak_bin];
        tx.sum_p = sp;
        tx.sum_pb = spb;
        tx.cut_start = hopping_ && first_frame_of_block_;
        tx.updated = true;
        s.active.push_back(tx);
    }

    double center_freq(const Segment &s, const Tx &tx) const {
        double bin = tx.sum_p > 0 ? tx.sum_pb / tx.sum_p : (tx.lo + tx.hi) / 2.0;
        return s.center + (bin - n_ / 2) * df_;
    }

    float snr(const Tx &tx) const {
        return 10.0f * std::log10(tx.peak_p / std::max(tx.peak_floor, 1e-30f));
    }

    void finish(Segment &s, Tx &tx, bool cut_end) {
        double f = center_freq(s, tx);
        double bw = (tx.hi - tx.lo + 1) * df_;
        double dur = tx.t_last - tx.t_start + frame_time_;
        long cell = std::lround(f / CELL_HZ);

        if (tx.continuous) {
            printf("%s  [continuous ended] %10.3f MHz  was on for %s\n", clock_str(tx.t_last).c_str(), f / 1e6,
                   dur_str(dur).c_str());
            cont_cells_[cell] = "on for " + dur_str(dur);
            fflush(stdout);
            return;
        }
        if (hopping_) {
            if (tx.cut_start && cut_end) {
                full_dwell_.insert(cell);  // on for the whole dwell: maybe continuous
            }
            if (cont_cells_live_.count(cell)) {
                return;  // already reported as continuous
            }
        }

        total_bursts_++;
        CellStats &c = cells_[cell];
        c.count++;
        c.total_dur += dur;
        c.bw_sum += bw;
        c.max_snr = std::max(c.max_snr, snr(tx));

        if (!cfg.show_all && is_repeating(cell, tx.t_start)) {
            return;
        }

        float peak_db = 10.0f * std::log10(tx.peak_p / full_scale_ + 1e-30f);
        printf("%s  %10.3f MHz  bw %9s  dur %s%9s  snr %5.1f dB  peak %6.1f dBFS\n", clock_str(tx.t_start).c_str(),
               f / 1e6, bw_str(bw).c_str(), (tx.cut_start || cut_end) ? ">" : " ", dur_str(dur).c_str(), snr(tx),
               peak_db);
        fflush(stdout);
    }

    // a cell (or its neighbor) that keeps bursting gets one [repeating] line instead of a line per burst
    bool is_repeating(long cell, double t) {
        for (long k = cell - 1; k <= cell + 1; k++) {
            auto it = repeating_.find(k);
            if (it != repeating_.end()) {
                it->second.hidden++;
                it->second.last = t;
                it->second.starts.push_back(t);
                while (it->second.starts.front() < t - REPEAT_WINDOW) {
                    it->second.starts.pop_front();
                }
                return true;
            }
        }
        auto &starts = recent_[cell];
        starts.push_back(t);
        while (starts.front() < t - REPEAT_WINDOW) {
            starts.pop_front();
        }
        if ((int)starts.size() < REPEAT_COUNT) {
            return false;
        }
        Repeating r;
        r.since = starts.front();
        r.last = t;
        r.starts = starts;
        double rate = (starts.size() - 1) / std::max(1e-3, starts.back() - starts.front());
        printf("%s  [repeating] %10.3f MHz  %d bursts in %.1f s (~%.1f/s); hiding its bursts until it goes quiet\n",
               clock_str(t).c_str(), cell * CELL_HZ / 1e6, (int)starts.size(), starts.back() - starts.front(), rate);
        fflush(stdout);
        repeating_[cell] = r;
        recent_.erase(cell);
        return false;
    }

    void check_repeating_ended(double now) {
        for (auto it = repeating_.begin(); it != repeating_.end();) {
            if (now - it->second.last > REPEAT_WINDOW) {
                printf("%s  [repeating ended] %10.3f MHz  ran %s, %ld bursts hidden\n", clock_str(now).c_str(),
                       it->first * CELL_HZ / 1e6, dur_str(it->second.last - it->second.since).c_str(),
                       it->second.hidden);
                fflush(stdout);
                it = repeating_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = recent_.begin(); it != recent_.end();) {
            if (it->second.empty() || it->second.back() < now - REPEAT_WINDOW) {
                it = recent_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void flush(Segment &s, bool cut_end) {
        for (auto &tx : s.active) {
            finish(s, tx, cut_end);
        }
        s.active.clear();
    }

    // sweep mode: a cell that fills the whole dwell 3 sweeps in a row is continuous
    void end_sweep() {
        for (auto it = streak_.begin(); it != streak_.end();) {
            if (!full_dwell_.count(it->first)) {
                if (cont_cells_live_.count(it->first)) {
                    printf("%s  [continuous ended] %10.3f MHz\n", clock_str(wall_now()).c_str(),
                           it->first * CELL_HZ / 1e6);
                    cont_cells_live_.erase(it->first);
                }
                it = streak_.erase(it);
            } else {
                ++it;
            }
        }
        for (long cell : full_dwell_) {
            if (++streak_[cell] == 3 && !cont_cells_live_.count(cell)) {
                cont_cells_live_.insert(cell);
                cont_cells_[cell] = "seen across sweeps";
                printf("%s  [continuous] %10.3f MHz  (on for the full dwell 3 sweeps in a row)\n",
                       clock_str(wall_now()).c_str(), cell * CELL_HZ / 1e6);
            }
        }
        full_dwell_.clear();
        fflush(stdout);
    }

    double rate_ = 0.0, df_ = 0.0, frame_time_ = 0.0;
    int n_ = 0, avg_ = 1, hold_frames_ = 1, cal_frames_ = 64;
    bool hopping_ = false;
    float thr_lin_ = 10.0f, cont_lin_ = 4.0f, full_scale_ = 1.0f;
    fftwf_complex *in_ = nullptr, *out_ = nullptr;
    fftwf_plan plan_;
    std::vector<float> window_, acc_;
    std::vector<uint8_t> hot_;
    int acc_count_ = 0;
    double block_t0_ = 0.0;
    bool first_frame_of_block_ = false;
    std::vector<Segment> segs_;
    std::map<long, CellStats> cells_;
    std::map<long, std::string> cont_cells_;
    long total_bursts_ = 0;
    std::set<long> full_dwell_, cont_cells_live_;
    std::map<long, int> streak_;

    static constexpr double REPEAT_WINDOW = 10.0;  // seconds
    static constexpr int REPEAT_COUNT = 8;         // bursts within the window
    struct Repeating {
        double since = 0.0, last = 0.0;
        long hidden = 0;
        std::deque<double> starts;
    };
    std::map<long, Repeating> repeating_;
    std::map<long, std::deque<double>> recent_;
    double last_repeat_check_ = 0.0;
};

static Detector g_det;


// -------------------- radio --------------------

static void rx_continuous(uhd::usrp::multi_usrp::sptr usrp, uhd::rx_streamer::sptr rx, size_t block_size) {
    uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    cmd.stream_now = true;
    rx->issue_stream_cmd(cmd);

    std::vector<cf> scratch(block_size);
    uhd::rx_metadata_t md;
    bool next_fresh = true;
    while (!g_stop) {
        auto blk = g_queue.get_free();
        cf *dst = blk ? blk->data.data() : scratch.data();
        size_t n = rx->recv(dst, block_size, md, 1.0);
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
            g_overflows++;
            next_fresh = true;
        } else if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
            next_fresh = true;
        }
        if (!blk) {
            g_dropped++;  // dsp is behind
            next_fresh = true;
            continue;
        }
        if (n == 0) {
            g_queue.put_free(std::move(blk));
            continue;
        }
        blk->seg = 0;
        blk->n = n;
        blk->t0 = md.has_time_spec ? md.time_spec.get_real_secs() : wall_now();
        blk->fresh = next_fresh;
        next_fresh = false;
        g_queue.push(std::move(blk));
    }
    rx->issue_stream_cmd(uhd::stream_cmd_t(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS));
}

static void rx_sweep(uhd::usrp::multi_usrp::sptr usrp, uhd::rx_streamer::sptr rx,
                     const std::vector<double> &centers, size_t dwell_samps, size_t settle_samps) {
    std::vector<cf> scratch(settle_samps + 1);
    uhd::rx_metadata_t md;
    while (!g_stop) {
        for (size_t i = 0; i < centers.size() && !g_stop; i++) {
            usrp->set_rx_freq(uhd::tune_request_t(centers[i]), 0);
            for (int k = 0; k < 50; k++) {
                if (usrp->get_rx_sensor("lo_locked", 0).to_bool()) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            auto blk = g_queue.get_free();
            if (!blk) {
                g_dropped++;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
            cmd.num_samps = settle_samps + dwell_samps;
            cmd.stream_now = true;
            rx->issue_stream_cmd(cmd);

            size_t got = 0;
            while (got < settle_samps) {
                size_t n = rx->recv(scratch.data(), settle_samps - got, md, 1.0);
                if (n == 0) {
                    break;
                }
                got += n;
            }
            size_t have = 0;
            double t0 = 0.0;
            while (have < dwell_samps) {
                size_t n = rx->recv(blk->data.data() + have, dwell_samps - have, md, 1.0);
                if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
                    g_overflows++;
                    continue;
                }
                if (n == 0) {
                    break;
                }
                if (have == 0) {
                    t0 = md.has_time_spec ? md.time_spec.get_real_secs() : wall_now();
                }
                have += n;
                if (md.end_of_burst) {
                    break;
                }
            }
            blk->seg = (int)i;
            blk->n = have;
            blk->t0 = t0;
            blk->fresh = true;
            blk->sweep_end = i + 1 == centers.size();
            g_queue.push(std::move(blk));
        }
        g_sweeps++;
    }
}


// -------------------- main --------------------

int main(int argc, char *argv[]) {
    std::string start_s, end_s, center_s;
    po::options_description desc("Options");
    desc.add_options()
        ("help,h", "show help")
        ("start", po::value<std::string>(&start_s)->default_value("902M"), "start frequency (e.g. 902M)")
        ("end", po::value<std::string>(&end_s)->default_value("928M"), "end frequency (e.g. 928M)")
        ("threshold,t", po::value<double>(&cfg.threshold_db)->default_value(10.0), "detection threshold over the noise floor (dB)")
        ("hysteresis", po::value<double>(&cfg.hysteresis_db)->default_value(4.0), "a transmission continues until it drops this many dB below --threshold")
        ("gain,g", po::value<double>(&cfg.gain)->default_value(40.0), "rx gain (dB)")
        ("rate", po::value<double>(&cfg.rate)->default_value(0.0), "sample rate (default: just enough for the range, up to --max-rate)")
        ("max-rate", po::value<double>(&cfg.max_rate)->default_value(50e6), "highest sample rate to use")
        ("center", po::value<std::string>(&center_s)->default_value(""), "single-tuning mode: center frequency (moves the DC blind spot)")
        ("fft", po::value<int>(&cfg.fft_n)->default_value(0), "FFT size (default: ~25 kHz bins)")
        ("avg", po::value<int>(&cfg.avg)->default_value(4), "FFTs averaged per detection frame")
        ("dwell", po::value<double>(&cfg.dwell)->default_value(0.005), "sweep mode: seconds per tuning")
        ("hold", po::value<double>(&cfg.hold_ms)->default_value(3.0), "ms of silence that ends a transmission")
        ("continuous", po::value<double>(&cfg.continuous_sec)->default_value(2.0), "seconds before a signal is reported as continuous")
        ("show-all", po::bool_switch(&cfg.show_all), "print every burst, even from frequencies marked [repeating]")
        ("duration", po::value<double>(&cfg.duration)->default_value(0.0), "stop after this many seconds (0 = until Ctrl-C)")
        ("antenna,a", po::value<std::string>(&cfg.antenna)->default_value("TX/RX"), "rx antenna (TX/RX or RX2)")
        ("args", po::value<std::string>(&cfg.args)->default_value("type=b200"), "UHD device args");
    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
        cfg.start = parse_freq(start_s);
        cfg.end = parse_freq(end_s);
        cfg.center = center_s.empty() ? 0.0 : parse_freq(center_s);
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n" << desc << std::endl;
        return 1;
    }
    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 0;
    }
    if (cfg.end <= cfg.start) {
        std::cerr << "--end must be above --start" << std::endl;
        return 1;
    }

    double span = cfg.end - cfg.start;
    double rate = cfg.rate;
    if (rate <= 0.0) {
        rate = std::min(cfg.max_rate, std::max(4e6, std::ceil(span / USABLE / 1e6) * 1e6));
    }

    auto usrp = uhd::usrp::multi_usrp::make(cfg.args);
    usrp->set_rx_rate(rate, 0);
    rate = usrp->get_rx_rate(0);
    usrp->set_rx_gain(cfg.gain, 0);
    usrp->set_rx_antenna(cfg.antenna, 0);
    usrp->set_rx_bandwidth(rate, 0);
    usrp->set_time_now(uhd::time_spec_t(wall_now()));

    // tuning plan
    double usable = USABLE * rate;
    std::vector<double> centers;
    bool hopping = span > usable;
    if (!hopping) {
        centers.push_back(cfg.center > 0.0 ? cfg.center : (cfg.start + cfg.end) / 2.0);
    } else {
        std::vector<double> sorted;
        for (double c = cfg.start + usable / 2.0; c - usable / 2.0 < cfg.end; c += usable) {
            sorted.push_back(c);
        }
        // The B200 driver re-runs a ~100 ms calibration whenever it tunes more than 100 MHz
        // from where it last calibrated; a normal retune is ~3 ms. So visit the tunings in
        // groups that fit within +/-90 MHz of the group's first (calibration) tuning.
        const double half_group = 90e6;
        size_t i = 0;
        while (i < sorted.size()) {
            size_t anchor = i;
            while (anchor + 1 < sorted.size() && sorted[anchor + 1] - sorted[i] <= half_group) {
                anchor++;
            }
            size_t end = anchor;
            while (end + 1 < sorted.size() && sorted[end + 1] - sorted[anchor] <= half_group) {
                end++;
            }
            centers.push_back(sorted[anchor]);
            for (size_t k = i; k <= end; k++) {
                if (k != anchor) {
                    centers.push_back(sorted[k]);
                }
            }
            i = end + 1;
        }
    }

    int n = cfg.fft_n;
    if (n <= 0) {
        n = 256;
        while (rate / n > 25e3 && n < 8192) {
            n *= 2;
        }
    }
    g_det.init(rate, n, cfg.avg, (int)centers.size(), hopping);
    for (size_t i = 0; i < centers.size(); i++) {
        g_det.setup_segment((int)i, centers[i]);
    }

    uhd::stream_args_t stream_args("fc32", "sc16");
    stream_args.channels = {0};
    auto rx = usrp->get_rx_stream(stream_args);

    size_t chunk = (size_t)n * cfg.avg;
    size_t block_size, dwell_samps = 0, settle_samps = 0;
    if (!hopping) {
        block_size = chunk * std::max<size_t>(1, (size_t)(0.02 * rate / chunk));  // ~20 ms blocks
        g_queue.init(64, block_size);
        usrp->set_rx_freq(uhd::tune_request_t(centers[0]), 0);
    } else {
        dwell_samps = chunk * std::max<size_t>(1, (size_t)std::round(cfg.dwell * rate / chunk));
        settle_samps = (size_t)(0.0005 * rate);
        block_size = dwell_samps;
        g_queue.init(std::min<size_t>(256, std::max<size_t>(16, centers.size() * 2)), block_size);
    }

    fprintf(stderr, "Scanning %.3f - %.3f MHz: %.2f MS/s, %d-pt FFT (%.1f kHz bins), %.0f us frames, "
            "threshold %.0f dB, gain %.0f dB, antenna %s\n",
            cfg.start / 1e6, cfg.end / 1e6, rate / 1e6, n, g_det.bin_hz() / 1e3, g_det.frame_time() * 1e6,
            cfg.threshold_db, usrp->get_rx_gain(0), usrp->get_rx_antenna(0).c_str());
    if (!hopping) {
        fprintf(stderr, "Single tuning at %.3f MHz, streaming continuously (blind spot +/-%.0f kHz at center)\n",
                centers[0] / 1e6, 1.5 * g_det.bin_hz() / 1e3);
    } else {
        fprintf(stderr, "Sweeping %zu tunings of %.1f MHz, %.1f ms dwell\n", centers.size(), usable / 1e6,
                dwell_samps / rate * 1e3);
    }
    fprintf(stderr, "Learning the noise floor...\n\n");

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    std::thread rx_thread = hopping ? std::thread(rx_sweep, usrp, rx, centers, dwell_samps, settle_samps)
                                    : std::thread(rx_continuous, usrp, rx, block_size);

    double t_start = wall_now();
    double last_status = t_start;
    long last_sweeps = 0;
    while (true) {
        auto blk = g_queue.pop();
        if (blk) {
            g_det.process(*blk);
            g_queue.put_free(std::move(blk));
        }
        double now = wall_now();
        if (cfg.duration > 0.0 && now - t_start >= cfg.duration) {
            g_stop = true;
        }
        if (hopping && now - last_status >= 10.0) {
            long s = g_sweeps;
            fprintf(stderr, "  [%.1f sweeps/s, %ld dropped, %ld overflows]\n", (s - last_sweeps) / (now - last_status),
                    g_dropped.load(), g_overflows.load());
            last_sweeps = s;
            last_status = now;
        }
        if (g_stop) {
            break;
        }
    }

    rx_thread.join();
    while (auto blk = g_queue.pop()) {
        g_det.process(*blk);
    }
    g_det.flush_all();
    g_det.print_summary();
    fprintf(stderr, "\nRan %.1f s, %ld blocks dropped, %ld overflows\n", wall_now() - t_start, g_dropped.load(),
            g_overflows.load());
    return 0;
}
