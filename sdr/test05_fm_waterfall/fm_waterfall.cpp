/*
Live FM receiver with a waterfall display for the B200.

Receives from the B200 (or plays back a .cfile), shows a live spectrum and
waterfall, and demodulates wideband FM (mono) for the station that's tuned in.
Audio is piped to pacat.

    ./build/fm_waterfall                          # live, 101.1 MHz
    ./build/fm_waterfall --record out.cfile       # live, also save raw IQ (fc32)
    ./build/fm_waterfall --file ../test02/test_all_fm.cfile --rate 10e6 --file-freq 101.1e6

Controls:
    click           tune to that frequency (snaps to 100 kHz)
    left/right      tune -/+ 200 kHz
    [ ]             gain -/+ 3 dB (live only)
    + -             volume
    m               mute
    Esc             quit

DSP chain:
    IQ -> mix station to 0 Hz -> lowpass + decimate to ~240 kHz
       -> FM discriminator -> 75us de-emphasis -> 15 kHz lowpass
       -> resample to 48 kHz -> pacat
*/

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/program_options.hpp>
#include <fftw3.h>
#include <uhd/usrp/multi_usrp.hpp>

#include "GL/freeglut.h"
#include "GL/gl.h"

using cf = std::complex<float>;
namespace po = boost::program_options;

static const double PI = 3.14159265358979323846;

static const int FFT_N = 2048;
static const int WF_ROWS = 600;
static const double ROWS_PER_SEC = 50.0;
static const double AUDIO_RATE = 48000.0;
static const double DEMOD_RATE = 240e3;     // target rate for the FM discriminator
static const double FM_DEVIATION = 75e3;    // broadcast FM max deviation
static const double DEEMPH_TAU = 75e-6;     // US de-emphasis time constant
static const double CHANNEL_CUTOFF = 100e3;
static const double CHANNEL_TRANSITION = 60e3;
static const double AUDIO_CUTOFF = 15e3;
static const double AUDIO_TRANSITION = 4e3;

struct Config {
    std::string args;
    std::string antenna;
    std::string file;
    std::string record;
    std::string audio_cmd;
    double station;
    double rate;
    double gain;
    double offset;
    double file_freq;
};
static Config cfg;

// shared between threads
static std::atomic<bool> g_running{true};
static std::atomic<double> g_center{0.0};   // RF center of the IQ stream
static std::atomic<double> g_station{0.0};  // station being demodulated
static std::atomic<double> g_retune{0.0};   // pending hardware retune (0 = none)
static std::atomic<double> g_gain{0.0};
static std::atomic<bool> g_gain_changed{false};
static std::atomic<float> g_volume{0.5f};
static std::atomic<bool> g_mute{false};
static std::atomic<long> g_overflows{0};
static double g_rate = 0.0;                 // actual sample rate, fixed once threads start

static std::mutex g_rows_mutex;
static std::deque<std::vector<float>> g_rows;  // spectrum rows (dB) waiting for the gui


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
        // if the sound card falls behind, drop audio instead of stalling the radio
        if (queued_ > AUDIO_RATE / 2) {
            return;
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


// -------------------- dsp --------------------

// windowed-sinc lowpass (Blackman), unity gain at DC
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

// shifts the station to 0 Hz, lowpass filters, and decimates
class Channelizer {
public:
    void init(double fs, int decim) {
        decim_ = decim;
        taps_ = design_lowpass(fs, CHANNEL_CUTOFF, CHANNEL_TRANSITION);
        ntaps_ = taps_.size();
        hist_.assign(2 * ntaps_, cf(0, 0));  // doubled delay line so the window is always contiguous
    }

    void process(const cf *in, size_t n, double shift_hz, double fs, std::vector<cf> &out) {
        cf step = std::polar(1.0f, (float)(-2.0 * PI * shift_hz / fs));
        for (size_t i = 0; i < n; i++) {
            cf x = in[i] * phasor_;
            phasor_ *= step;
            hist_[pos_] = hist_[pos_ + ntaps_] = x;
            pos_ = (pos_ + 1) % ntaps_;
            if (++count_ == decim_) {
                count_ = 0;
                const cf *h = &hist_[pos_];
                float re = 0.0f, im = 0.0f;
                for (int k = 0; k < ntaps_; k++) {
                    re += h[k].real() * taps_[k];
                    im += h[k].imag() * taps_[k];
                }
                out.emplace_back(re, im);
            }
        }
        phasor_ /= std::abs(phasor_);
    }

private:
    std::vector<float> taps_;
    std::vector<cf> hist_;
    int ntaps_ = 0, pos_ = 0, decim_ = 1, count_ = 0;
    cf phasor_{1.0f, 0.0f};
};

// FM discriminator, de-emphasis, audio lowpass, resample to AUDIO_RATE
class FmDemod {
public:
    void init(double fs) {
        scale_ = (float)(fs / (2.0 * PI * FM_DEVIATION));  // full deviation -> +/-1.0
        alpha_ = (float)(1.0 - std::exp(-1.0 / (fs * DEEMPH_TAU)));
        taps_ = design_lowpass(fs, AUDIO_CUTOFF, AUDIO_TRANSITION);
        ntaps_ = taps_.size();
        hist_.assign(2 * ntaps_, 0.0f);
        rstep_ = fs / AUDIO_RATE;
    }

    void process(const std::vector<cf> &in, std::vector<float> &audio) {
        for (auto &z : in) {
            float d = std::arg(z * std::conj(prev_)) * scale_;
            prev_ = z;
            deemph_ += alpha_ * (d - deemph_);

            hist_[pos_] = hist_[pos_ + ntaps_] = deemph_;
            pos_ = (pos_ + 1) % ntaps_;
            const float *h = &hist_[pos_];
            float y = 0.0f;
            for (int k = 0; k < ntaps_; k++) {
                y += h[k] * taps_[k];
            }

            // linear interpolation between last_ and y
            while (rpos_ <= 1.0) {
                audio.push_back(last_ + (y - last_) * (float)rpos_);
                rpos_ += rstep_;
            }
            rpos_ -= 1.0;
            last_ = y;
        }
    }

private:
    cf prev_{1.0f, 0.0f};
    float scale_ = 1.0f, alpha_ = 1.0f, deemph_ = 0.0f, last_ = 0.0f;
    std::vector<float> taps_, hist_;
    int ntaps_ = 0, pos_ = 0;
    double rpos_ = 0.0, rstep_ = 1.0;
};

// averaged FFT rows for the waterfall
class Spectrum {
public:
    void init(double fs) {
        buf_ = fftwf_alloc_complex(FFT_N);
        plan_ = fftwf_plan_dft_1d(FFT_N, buf_, buf_, FFTW_FORWARD, FFTW_MEASURE);
        window_.resize(FFT_N);
        for (int i = 0; i < FFT_N; i++) {
            window_[i] = (float)(0.5 - 0.5 * std::cos(2.0 * PI * i / (FFT_N - 1)));
        }
        acc_.assign(FFT_N, 0.0f);
        avg_ = std::max(1, (int)(fs / FFT_N / ROWS_PER_SEC));
    }

    void process(const cf *in, size_t n) {
        for (size_t i = 0; i < n; i++) {
            buf_[fill_][0] = in[i].real() * window_[fill_];
            buf_[fill_][1] = in[i].imag() * window_[fill_];
            if (++fill_ < FFT_N) {
                continue;
            }
            fill_ = 0;
            fftwf_execute(plan_);
            for (int k = 0; k < FFT_N; k++) {
                acc_[k] += buf_[k][0] * buf_[k][0] + buf_[k][1] * buf_[k][1];
            }
            if (++frames_ < avg_) {
                continue;
            }
            frames_ = 0;

            std::vector<float> row(FFT_N);
            float norm = 1.0f / ((float)avg_ * FFT_N * FFT_N);
            for (int k = 0; k < FFT_N; k++) {
                int src = (k + FFT_N / 2) % FFT_N;  // fftshift
                row[k] = 10.0f * std::log10(acc_[src] * norm + 1e-20f);
                acc_[src] = 0.0f;
            }
            std::lock_guard<std::mutex> lock(g_rows_mutex);
            if (g_rows.size() < 256) {
                g_rows.push_back(std::move(row));
            }
        }
    }

private:
    fftwf_complex *buf_ = nullptr;
    fftwf_plan plan_;
    std::vector<float> window_, acc_;
    int fill_ = 0, frames_ = 0, avg_ = 1;
};

class Dsp {
public:
    void init(double fs) {
        decim_ = std::max(1, (int)std::floor(fs / DEMOD_RATE + 1e-6));
        spectrum_.init(fs);
        channel_.init(fs, decim_);
        demod_.init(fs / decim_);
    }

    void process(const cf *in, size_t n) {
        spectrum_.process(in, n);

        baseband_.clear();
        channel_.process(in, n, g_station - g_center, g_rate, baseband_);

        audio_.clear();
        demod_.process(baseband_, audio_);

        float vol = g_mute ? 0.0f : g_volume.load();
        std::vector<int16_t> pcm(audio_.size());
        for (size_t i = 0; i < audio_.size(); i++) {
            float s = audio_[i] * vol * 32767.0f;
            pcm[i] = (int16_t)std::max(-32767.0f, std::min(32767.0f, s));
        }
        g_audio.push(std::move(pcm));
    }

    int decim() const { return decim_; }

private:
    int decim_ = 1;
    Spectrum spectrum_;
    Channelizer channel_;
    FmDemod demod_;
    std::vector<cf> baseband_;
    std::vector<float> audio_;
};

static Dsp g_dsp;


// -------------------- sample sources --------------------

static void rx_thread_fn(uhd::usrp::multi_usrp::sptr usrp) {
    uhd::stream_args_t stream_args("fc32", "sc16");
    stream_args.channels = {0};
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);

    FILE *rec = nullptr;
    if (!cfg.record.empty()) {
        rec = fopen(cfg.record.c_str(), "wb");
        if (!rec) {
            std::cerr << "Couldn't open " << cfg.record << " for recording" << std::endl;
        }
    }

    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    stream_cmd.stream_now = true;
    rx_stream->issue_stream_cmd(stream_cmd);

    std::vector<cf> buf(65536);
    uhd::rx_metadata_t md;
    while (g_running) {
        double retune = g_retune.exchange(0.0);
        if (retune != 0.0) {
            usrp->set_rx_freq(uhd::tune_request_t(retune), 0);
            g_center = usrp->get_rx_freq(0);
        }
        if (g_gain_changed.exchange(false)) {
            usrp->set_rx_gain(g_gain, 0);
        }

        size_t n = rx_stream->recv(buf.data(), buf.size(), md, 0.5);
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            continue;
        }
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
            g_overflows++;
            continue;
        }
        if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
            std::cerr << "RX error: " << md.strerror() << std::endl;
            continue;
        }

        if (rec) {
            fwrite(buf.data(), sizeof(cf), n, rec);
        }
        g_dsp.process(buf.data(), n);
    }

    rx_stream->issue_stream_cmd(uhd::stream_cmd_t(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS));
    if (rec) {
        fclose(rec);
    }
}

// plays a .cfile at real-time speed, looping at the end
static void file_thread_fn(FILE *f) {
    std::vector<cf> buf(32768);
    auto t0 = std::chrono::steady_clock::now();
    double samples_done = 0.0;
    while (g_running) {
        size_t n = fread(buf.data(), sizeof(cf), buf.size(), f);
        if (n == 0) {
            rewind(f);
            continue;
        }
        g_dsp.process(buf.data(), n);
        samples_done += n;
        auto target = t0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(samples_done / g_rate));
        std::this_thread::sleep_until(target);
    }
    fclose(f);
}


// -------------------- gui --------------------

static int win_w = 1400, win_h = 900;
static GLuint wf_tex = 0;
static int wf_head = 0;  // next texture row to write (= oldest row on screen)
static std::vector<float> last_row(FFT_N, -120.0f);
static std::vector<uint32_t> row_pixels(FFT_N);
static float db_floor = 0.0f;
static bool have_floor = false;

static const int BAR_H = 44;
static const int AXIS_H = 20;

static float db_lo() { return db_floor - 5.0f; }
static float db_hi() { return db_floor + 55.0f; }

static uint32_t colormap(float v) {
    // black -> purple -> red -> orange -> pale yellow
    static const float pts[5][3] = {
        {0, 0, 0}, {50, 10, 100}, {190, 40, 80}, {250, 150, 30}, {255, 250, 200}};
    v = std::max(0.0f, std::min(1.0f, v)) * 4.0f;
    int i = std::min(3, (int)v);
    float f = v - i;
    uint32_t r = (uint32_t)(pts[i][0] + (pts[i + 1][0] - pts[i][0]) * f);
    uint32_t g = (uint32_t)(pts[i][1] + (pts[i + 1][1] - pts[i][1]) * f);
    uint32_t b = (uint32_t)(pts[i][2] + (pts[i + 1][2] - pts[i][2]) * f);
    return r | (g << 8) | (b << 16) | (0xffu << 24);
}

static double x_to_freq(double x) {
    return g_center + (x / win_w - 0.5) * g_rate;
}

static double freq_to_x(double f) {
    return ((f - g_center) / g_rate + 0.5) * win_w;
}

static void tune_to(double f) {
    double margin = CHANNEL_CUTOFF + 50e3;
    double half = g_rate / 2.0 - margin;
    if (cfg.file.empty()) {
        // live: retune the radio if the station is near the edge or on the DC spike
        double off = f - g_center;
        if (std::abs(off) > half || std::abs(off) < 50e3) {
            g_retune = f - cfg.offset;
        }
    } else {
        f = std::max(g_center - half, std::min(g_center + half, f));
    }
    g_station = f;
}

static void draw_text(float x, float y, const std::string &s) {
    glRasterPos2f(x, y);
    glutBitmapString(GLUT_BITMAP_9_BY_15, (const unsigned char *)s.c_str());
}

static void add_row(const std::vector<float> &row) {
    std::vector<float> tmp(row);
    std::nth_element(tmp.begin(), tmp.begin() + FFT_N / 5, tmp.end());
    float p20 = tmp[FFT_N / 5];
    db_floor = have_floor ? 0.97f * db_floor + 0.03f * p20 : p20;
    have_floor = true;

    float lo = db_lo(), hi = db_hi();
    for (int i = 0; i < FFT_N; i++) {
        row_pixels[i] = colormap((row[i] - lo) / (hi - lo));
    }
    glBindTexture(GL_TEXTURE_2D, wf_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, wf_head, FFT_N, 1, GL_RGBA, GL_UNSIGNED_BYTE, row_pixels.data());
    wf_head = (wf_head + 1) % WF_ROWS;
    last_row = row;
}

static void timer(int) {
    if (glutGetWindow() == 0) {
        return;  // window was closed; main loop is about to return
    }
    std::deque<std::vector<float>> rows;
    {
        std::lock_guard<std::mutex> lock(g_rows_mutex);
        rows.swap(g_rows);
    }
    for (auto &r : rows) {
        add_row(r);
    }
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

static void display() {
    float w = win_w, h = win_h;
    float wf_top = (h - BAR_H) * 0.72f;
    float spec_bot = wf_top + AXIS_H;
    float spec_top = h - BAR_H;

    glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();
    glOrtho(0, w, 0, h, -1, 1);

    // waterfall: oldest row at the bottom, newest at the top
    float t0 = (float)wf_head / WF_ROWS;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, wf_tex);
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, t0);        glVertex2f(0, 0);
    glTexCoord2f(1, t0);        glVertex2f(w, 0);
    glTexCoord2f(1, t0 + 1.0f); glVertex2f(w, wf_top);
    glTexCoord2f(0, t0 + 1.0f); glVertex2f(0, wf_top);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    // spectrum grid every 10 dB
    float lo = db_lo(), hi = db_hi();
    auto db_to_y = [&](float db) {
        float v = std::max(0.0f, std::min(1.0f, (db - lo) / (hi - lo)));
        return spec_bot + v * (spec_top - spec_bot);
    };
    glColor3f(0.18f, 0.18f, 0.22f);
    glBegin(GL_LINES);
    for (float db = std::ceil(lo / 10.0f) * 10.0f; db <= hi; db += 10.0f) {
        glVertex2f(0, db_to_y(db));
        glVertex2f(w, db_to_y(db));
    }
    glEnd();

    // spectrum trace
    glColor3f(0.4f, 0.9f, 1.0f);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i < FFT_N; i++) {
        glVertex2f((i + 0.5f) * w / FFT_N, db_to_y(last_row[i]));
    }
    glEnd();

    // station marker: shaded channel plus a center line
    double station = g_station;
    float sx = freq_to_x(station);
    float bw = CHANNEL_CUTOFF / g_rate * w;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.3f, 1.0f, 0.3f, 0.15f);
    glBegin(GL_QUADS);
    glVertex2f(sx - bw, 0);
    glVertex2f(sx + bw, 0);
    glVertex2f(sx + bw, spec_top);
    glVertex2f(sx - bw, spec_top);
    glEnd();
    glDisable(GL_BLEND);
    glColor3f(0.3f, 1.0f, 0.3f);
    glBegin(GL_LINES);
    glVertex2f(sx, 0);
    glVertex2f(sx, spec_top);
    glEnd();

    // frequency axis
    glColor3f(0.12f, 0.12f, 0.15f);
    glBegin(GL_QUADS);
    glVertex2f(0, wf_top);
    glVertex2f(w, wf_top);
    glVertex2f(w, spec_bot);
    glVertex2f(0, spec_bot);
    glEnd();
    double step = 0.1e6;
    for (double s : {0.1e6, 0.2e6, 0.5e6, 1e6, 2e6, 5e6, 10e6}) {
        step = s;
        if (g_rate / s <= 12) {
            break;
        }
    }
    double f_lo = g_center - g_rate / 2.0;
    double f_hi = g_center + g_rate / 2.0;
    for (double f = std::ceil(f_lo / step) * step; f < f_hi; f += step) {
        float x = freq_to_x(f);
        glColor3f(0.5f, 0.5f, 0.55f);
        glBegin(GL_LINES);
        glVertex2f(x, wf_top);
        glVertex2f(x, wf_top + 5);
        glEnd();
        char label[32];
        snprintf(label, sizeof(label), "%.1f", f / 1e6);
        glColor3f(0.8f, 0.8f, 0.85f);
        draw_text(x - 4.5f * strlen(label), wf_top + 5, label);
    }

    // status bar
    char status[256];
    snprintf(status, sizeof(status),
             "FM %.1f MHz   center %.3f MHz   rate %.2f MS/s   %s   vol %d%%%s   overflows %ld",
             station / 1e6, g_center / 1e6, g_rate / 1e6,
             cfg.file.empty() ? ("gain " + std::to_string((int)g_gain) + " dB").c_str() : "file playback",
             (int)std::lround(g_volume * 100), g_mute ? " (muted)" : "", g_overflows.load());
    glColor3f(1, 1, 1);
    draw_text(10, h - 18, status);
    glColor3f(0.6f, 0.6f, 0.65f);
    draw_text(10, h - 37, "click: tune   left/right: -/+200 kHz   [ ]: gain   + -: volume   m: mute   Esc: quit");

    glutSwapBuffers();
}

static void reshape(int w, int h) {
    win_w = std::max(1, w);
    win_h = std::max(1, h);
    glViewport(0, 0, win_w, win_h);
}

static void mouse(int button, int state, int x, int y) {
    if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        double f = std::round(x_to_freq(x) / 100e3) * 100e3;
        tune_to(f);
    }
}

static void keyboard(unsigned char key, int, int) {
    switch (key) {
    case 27:
        glutLeaveMainLoop();
        break;
    case '+':
    case '=':
        g_volume = std::min(2.0f, g_volume + 0.1f);
        break;
    case '-':
        g_volume = std::max(0.0f, g_volume - 0.1f);
        break;
    case 'm':
        g_mute = !g_mute;
        break;
    case '[':
    case ']':
        if (cfg.file.empty()) {
            g_gain = std::max(0.0, std::min(76.0, g_gain + (key == ']' ? 3.0 : -3.0)));
            g_gain_changed = true;
        }
        break;
    }
}

static void special(int key, int, int) {
    if (key == GLUT_KEY_LEFT) {
        tune_to(g_station - 200e3);
    } else if (key == GLUT_KEY_RIGHT) {
        tune_to(g_station + 200e3);
    }
}


// -------------------- main --------------------

int main(int argc, char *argv[]) {
    glutInit(&argc, argv);

    po::options_description desc("Options");
    desc.add_options()
        ("help,h", "show help")
        ("freq,f", po::value<double>(&cfg.station)->default_value(101.1e6), "station to tune (Hz)")
        ("rate,r", po::value<double>(&cfg.rate)->default_value(2.4e6), "sample rate (Hz); for --file, the rate it was recorded at")
        ("gain,g", po::value<double>(&cfg.gain)->default_value(20.0), "rx gain (dB)")
        ("antenna,a", po::value<std::string>(&cfg.antenna)->default_value("TX/RX"), "rx antenna (TX/RX or RX2)")
        ("args", po::value<std::string>(&cfg.args)->default_value("type=b200"), "UHD device args")
        ("offset", po::value<double>(&cfg.offset)->default_value(250e3), "tune the radio this far below the station to dodge the DC spike (Hz)")
        ("record", po::value<std::string>(&cfg.record)->default_value(""), "also save raw IQ (fc32) to this file")
        ("file", po::value<std::string>(&cfg.file)->default_value(""), "play back a .cfile (fc32) instead of using the radio")
        ("file-freq", po::value<double>(&cfg.file_freq)->default_value(0.0), "center frequency of --file (Hz), defaults to --freq")
        ("audio-cmd", po::value<std::string>(&cfg.audio_cmd)->default_value(
            "pacat --raw --format=s16le --rate=48000 --channels=1 --latency-msec=200 --client-name=fm_waterfall"),
            "command that plays raw s16le 48 kHz mono audio from stdin");
    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n" << desc << std::endl;
        return 1;
    }
    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 0;
    }

    signal(SIGPIPE, SIG_IGN);  // a dead audio pipe shouldn't kill the program

    uhd::usrp::multi_usrp::sptr usrp;
    FILE *file = nullptr;
    if (!cfg.file.empty()) {
        file = fopen(cfg.file.c_str(), "rb");
        if (!file) {
            std::cerr << "Couldn't open " << cfg.file << std::endl;
            return 1;
        }
        fseek(file, 0, SEEK_END);
        long bytes = ftell(file);
        rewind(file);
        if (bytes < (long)sizeof(cf) * FFT_N) {
            std::cerr << cfg.file << " is too small" << std::endl;
            return 1;
        }
        g_rate = cfg.rate;
        g_center = cfg.file_freq != 0.0 ? cfg.file_freq : cfg.station;
        std::cout << "Playing " << cfg.file << ": " << bytes / sizeof(cf) / g_rate << " s at "
                  << g_rate / 1e6 << " MS/s, center " << g_center / 1e6 << " MHz" << std::endl;
    } else {
        usrp = uhd::usrp::multi_usrp::make(cfg.args);
        usrp->set_rx_rate(cfg.rate, 0);
        g_rate = usrp->get_rx_rate(0);
        usrp->set_rx_freq(uhd::tune_request_t(cfg.station - cfg.offset), 0);
        g_center = usrp->get_rx_freq(0);
        usrp->set_rx_gain(cfg.gain, 0);
        usrp->set_rx_antenna(cfg.antenna, 0);
        usrp->set_rx_bandwidth(g_rate, 0);
        std::cout << "B200: " << g_rate / 1e6 << " MS/s, center " << g_center / 1e6 << " MHz, gain "
                  << usrp->get_rx_gain(0) << " dB, antenna " << usrp->get_rx_antenna(0) << std::endl;
    }
    g_station = cfg.station;
    g_gain = cfg.gain;

    g_dsp.init(g_rate);
    std::cout << "Demodulating " << g_station / 1e6 << " MHz, discriminator at "
              << g_rate / g_dsp.decim() / 1e3 << " kHz" << std::endl;

    g_audio.start(cfg.audio_cmd);

    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(win_w, win_h);
    glutCreateWindow("FM Waterfall");
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);

    glGenTextures(1, &wf_tex);
    glBindTexture(GL_TEXTURE_2D, wf_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    std::vector<uint32_t> blank(FFT_N * WF_ROWS, colormap(0.0f));
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FFT_N, WF_ROWS, 0, GL_RGBA, GL_UNSIGNED_BYTE, blank.data());

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(special);
    glutMouseFunc(mouse);
    glutTimerFunc(16, timer, 0);

    std::thread source = usrp ? std::thread(rx_thread_fn, usrp) : std::thread(file_thread_fn, file);

    glutMainLoop();

    g_running = false;
    source.join();
    g_audio.stop();
    return 0;
}
