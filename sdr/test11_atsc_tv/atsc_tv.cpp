/*
ATSC 1.0 digital TV receiver for the B200, written from scratch.

Demodulates 8-VSB, decodes the trellis code / interleaver / Reed-Solomon,
demuxes the MPEG transport stream, and shows the picture in its own window
with sound. libavcodec decodes the MPEG-2 video and AC-3 audio; everything
else is here.

    ./build/atsc_tv --scan                     # find stations
    ./build/atsc_tv --channel 30               # watch RF channel 30
    ./build/atsc_tv --channel 30 --program 2   # start on the 3rd subchannel
    ./build/atsc_tv --channel 30 --out tv.ts   # also save the transport stream
    ./build/atsc_tv --channel 30 --vlc         # hand the stream to VLC instead
    ./build/atsc_tv --file rec.sc16 --freq 569e6   # play a recording

Keys: n / p next / previous subchannel, f fullscreen, m mute, + / - volume,
Esc quit.

RF channels (US, post-repack): 2-6 VHF low, 7-13 VHF high, 14-36 UHF.
A station's "virtual" channel (e.g. 5.1) usually differs from its RF
channel; --scan lists RF channels.

Recordings for --file: interleaved int16 I/Q at 21.524476 MS/s (2x the
symbol rate), tuned to the channel center (see --record).
*/

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/program_options.hpp>
#include <uhd/usrp/multi_usrp.hpp>

#include "GL/freeglut.h"
#include "GL/gl.h"

#include "atsc_decode.hpp"
#include "atsc_demod.hpp"
#include "atsc_player.hpp"

namespace po = boost::program_options;

static std::atomic<bool> g_stop{false};
static void on_sigint(int) {
    g_stop = true;
}

static double channel_center(int ch) {
    if (ch >= 2 && ch <= 4) return 57e6 + 6e6 * (ch - 2);
    if (ch >= 5 && ch <= 6) return 79e6 + 6e6 * (ch - 5);
    if (ch >= 7 && ch <= 13) return 177e6 + 6e6 * (ch - 7);
    if (ch >= 14 && ch <= 36) return 473e6 + 6e6 * (ch - 14);
    return 0.0;
}

// simple blocking queue between pipeline stages
template <typename T>
class Queue {
public:
    explicit Queue(size_t max) : max_(max) {}
    bool push(T v) {
        std::unique_lock<std::mutex> lock(m_);
        if (q_.size() >= max_) {
            return false;  // consumer is behind
        }
        q_.push_back(std::move(v));
        cv_.notify_one();
        return true;
    }
    bool pop(T &v) {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait_for(lock, std::chrono::milliseconds(200), [this] { return !q_.empty(); });
        if (q_.empty()) {
            return false;
        }
        v = std::move(q_.front());
        q_.pop_front();
        return true;
    }

private:
    size_t max_;
    std::deque<T> q_;
    std::mutex m_;
    std::condition_variable cv_;
};

struct Segment {
    int index;
    std::vector<float> sym;
};

// -------------------- the receive pipeline --------------------

class Receiver {
public:
    Receiver(double center_hz, FILE *ts_out, FILE *player, atsc::Player *builtin)
        : fc_(center_hz), ts_out_(ts_out), player_(player), builtin_(builtin) {
        fe_.init();
    }

    // stage 1: IQ -> equalized data segments
    void demod(const cf *iq, size_t n, const std::function<void(Segment &&)> &emit) {
        base_.clear();
        fe_.process(iq, n, base_);
        if (!clock_set_ && fe_.pilot_locked()) {
            ss_.set_clock_ppm(-fe_.pilot_offset_hz() / fc_ * 1e6);
            clock_set_ = true;
        }
        ss_.process(base_, [&](const float *seg) {
            eq_.push_segment(seg, [&](int idx, const float *d) {
                emit(Segment{idx, std::vector<float>(d, d + atsc::DATA_SYMS)});
            });
        });
    }

    // stage 2: data segments -> TS packets
    void decode(const Segment &s) {
        dec_.push_segment(s.index, s.sym.data(), [&](const uint8_t *ts, bool ok) {
            if (ts_out_) {
                fwrite(ts, 1, 188, ts_out_);
            }
            if (player_ && !player_failed_) {
                if (fwrite(ts, 1, 188, player_) != 188) {
                    player_failed_ = true;
                    fprintf(stderr, "\nPlayer closed; continuing without it\n");
                }
            }
            if (builtin_) {
                builtin_->push_ts(ts, ok);
            }
            if (ok) {
                int pid = ((ts[1] & 0x1F) << 8) | ts[2];
                pids_[pid]++;
            }
            bytes_ += 188;
        });
    }

    void status(double elapsed) {
        fprintf(stderr, "\r%s   ", status_line(elapsed).c_str());
    }

    std::string status_line(double elapsed) {
        auto fi = eq_.info();
        auto &st = dec_.stats();
        double mbps = elapsed > 0 ? bytes_ * 8.0 / elapsed / 1e6 : 0.0;
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "pilot %s %+5.0f Hz | sync %s %+4.1f ppm | field %s | SNR %4.1f dB | packets %ld ok, %ld fixed, %ld bad | "
                 "%.2f Mb/s",
                 fe_.pilot_locked() ? "LOCK" : "----", fe_.pilot_offset_hz(), ss_.locked() ? "LOCK" : "----",
                 ss_.clock_ppm(), fi.locked ? "LOCK" : "----", fi.snr_db, st.packets - st.failed - st.corrected,
                 st.corrected, st.failed, mbps);
        return buf;
    }

    float snr_db() { return eq_.info().snr_db; }

    void summary() {
        auto &st = dec_.stats();
        fprintf(stderr, "\n\n%ld TS packets: %ld clean, %ld corrected (%ld bytes fixed), %ld uncorrectable\n",
                st.packets, st.packets - st.failed - st.corrected, st.corrected, st.corrected_bytes, st.failed);
        if (!pids_.empty()) {
            fprintf(stderr, "PIDs:");
            for (auto &kv : pids_) {
                if (kv.second > 20) {
                    fprintf(stderr, " 0x%04x(%ld)", kv.first, kv.second);
                }
            }
            fprintf(stderr, "\n");
        }
    }

private:
    double fc_;
    FILE *ts_out_, *player_;
    atsc::Player *builtin_;
    bool player_failed_ = false, clock_set_ = false;
    atsc::Frontend fe_;
    atsc::SegmentSync ss_;
    atsc::Equalizer eq_;
    atsc::DataDecoder dec_;
    std::vector<float> base_;
    std::map<int, long> pids_;
    long bytes_ = 0;
};


// -------------------- scan --------------------

static void scan(uhd::usrp::multi_usrp::sptr usrp, double gain) {
    const double fs = 8e6;
    const int N = 65536;
    usrp->set_rx_rate(fs, 0);
    usrp->set_rx_gain(gain, 0);
    uhd::stream_args_t sa("fc32", "sc16");
    sa.channels = {0};
    auto rx = usrp->get_rx_stream(sa);
    fftwf_complex *in = fftwf_alloc_complex(N), *out = fftwf_alloc_complex(N);
    fftwf_plan plan = fftwf_plan_dft_1d(N, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
    std::vector<cf> buf(4 * N);
    uhd::rx_metadata_t md;

    fprintf(stderr, "Scanning RF channels 2-36 for ATSC pilots...\n");
    printf("  CH   CENTER (MHz)   PILOT\n");
    for (int ch = 2; ch <= 36 && !g_stop; ch++) {
        double fc = channel_center(ch);
        double pilot = fc - 2.690559e6;
        double lo = fc - 1e6;  // keep the pilot away from DC
        usrp->set_rx_freq(uhd::tune_request_t(lo), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
        cmd.num_samps = buf.size();
        cmd.stream_now = true;
        rx->issue_stream_cmd(cmd);
        size_t got = 0;
        while (got < buf.size()) {
            size_t n = rx->recv(&buf[got], buf.size() - got, md, 1.0);
            if (n == 0) break;
            got += n;
        }
        if (got < buf.size()) continue;

        std::vector<float> pw(N, 0.0f);
        for (int b = 1; b < 4; b++) {
            for (int i = 0; i < N; i++) {
                float w = 0.5f - 0.5f * std::cos(2.0f * (float)atsc::PI * i / N);
                in[i][0] = buf[b * N + i].real() * w;
                in[i][1] = buf[b * N + i].imag() * w;
            }
            fftwf_execute(plan);
            for (int i = 0; i < N; i++) {
                pw[i] += out[i][0] * out[i][0] + out[i][1] * out[i][1];
            }
        }
        auto bin_of = [&](double f) { return ((int)std::lround((f - lo) / fs * N) + N) % N; };
        int pb = bin_of(pilot);
        float peak = 0.0f;
        for (int k = -8; k <= 8; k++) peak = std::max(peak, pw[(pb + k + N) % N]);
        std::vector<float> noise;
        for (double f = pilot - 290e3; f < pilot - 60e3; f += fs / N) noise.push_back(pw[bin_of(f)]);
        std::nth_element(noise.begin(), noise.begin() + noise.size() / 2, noise.end());
        float db = 10.0f * std::log10(peak / std::max(noise[noise.size() / 2], 1e-20f));
        if (db > 15.0f) {
            printf("  %2d   %6.1f        %4.1f dB %s\n", ch, fc / 1e6, db, db > 35 ? "strong" : db > 25 ? "" : "weak");
            fflush(stdout);
        }
    }
    fprintf(stderr, "Pilot strength isn't the whole story: try the strongest ones and watch the SNR.\n");
}


// -------------------- video window --------------------

namespace win {
static atsc::Player *player = nullptr;
static Receiver *receiver = nullptr;
static std::function<double()> elapsed;
static GLuint tex = 0;
static int tex_w = 0, tex_h = 0;
static double aspect = 16.0 / 9.0;
static SwsContext *sws = nullptr;
static std::vector<uint8_t> rgb;
static bool have_picture = false, fullscreen = false;
static int win_w = 1280, win_h = 720;
static double last_status = 0.0;

static void upload(AVFrame *f) {
    int w = f->width, h = f->height;
    sws = sws_getCachedContext(sws, w, h, (AVPixelFormat)f->format, w, h, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr,
                               nullptr, nullptr);
    rgb.resize((size_t)w * h * 3);
    uint8_t *dst[1] = {rgb.data()};
    int stride[1] = {w * 3};
    sws_scale(sws, f->data, f->linesize, 0, h, dst, stride);
    // simple deinterlace for 1080i: rebuild the second field from the first
    if (f->flags & AV_FRAME_FLAG_INTERLACED) {
        for (int y = 1; y + 1 < h; y += 2) {
            uint8_t *row = &rgb[(size_t)y * w * 3];
            const uint8_t *a = row - w * 3, *b = row + w * 3;
            for (int i = 0; i < w * 3; i++) row[i] = (uint8_t)((a[i] + b[i] + 1) >> 1);
        }
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (w != tex_w || h != tex_h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
        tex_w = w;
        tex_h = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    }
    AVRational sar = f->sample_aspect_ratio;
    double par = (sar.num > 0 && sar.den > 0) ? (double)sar.num / sar.den : 1.0;
    aspect = (double)w * par / h;
    have_picture = true;
}

static void text(float x, float y, const std::string &s) {
    glRasterPos2f(x, y);
    glutBitmapString(GLUT_BITMAP_HELVETICA_18, (const unsigned char *)s.c_str());
}

static void display() {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();
    glOrtho(0, win_w, 0, win_h, -1, 1);
    if (have_picture) {
        double w = win_w, h = win_w / aspect;
        if (h > win_h) {
            h = win_h;
            w = win_h * aspect;
        }
        double x0 = (win_w - w) / 2, y0 = (win_h - h) / 2;
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex);
        glColor3f(1, 1, 1);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 1); glVertex2f(x0, y0);
        glTexCoord2f(1, 1); glVertex2f(x0 + w, y0);
        glTexCoord2f(1, 0); glVertex2f(x0 + w, y0 + h);
        glTexCoord2f(0, 0); glVertex2f(x0, y0 + h);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    } else {
        glColor3f(0.7f, 0.7f, 0.7f);
        text(20, win_h / 2.0f, "Tuning... " + receiver->status_line(elapsed()));
    }
    if (player->osd_age() < 4.0 || !have_picture) {
        std::string osd = player->osd();
        if (!osd.empty()) {
            glColor3f(0, 0, 0);
            text(22, win_h - 34, osd);
            glColor3f(1, 1, 0.3f);
            text(20, win_h - 32, osd);
        }
    }
    glutSwapBuffers();
}

static void timer(int) {
    if (glutGetWindow() == 0) {
        return;  // window was closed; the main loop is returning
    }
    if (g_stop) {
        glutLeaveMainLoop();
        return;
    }
    double pts;
    if (AVFrame *f = player->take_due_frame(&pts)) {
        upload(f);
        av_frame_free(&f);
        player->note_shown(pts);
    }
    glutPostRedisplay();
    double t = elapsed();
    if (t - last_status >= 0.5) {
        receiver->status(t);
        if (getenv("ATSC_AV_DEBUG")) fprintf(stderr, "\n    A/V: %s\n", player->av_debug().c_str());
        std::string title = player->status() + (player->muted() ? "  [muted]" : "") + "  |  SNR " +
                            std::to_string((int)std::lround(receiver->snr_db())) + " dB";
        glutSetWindowTitle(title.c_str());
        last_status = t;
    }
    glutTimerFunc(5, timer, 0);
}

static void reshape(int w, int h) {
    win_w = std::max(1, w);
    win_h = std::max(1, h);
    glViewport(0, 0, win_w, win_h);
}

static void keyboard(unsigned char key, int, int) {
    switch (key) {
    case 27: g_stop = true; break;
    case 'n': case ' ': player->change_program(+1); break;
    case 'p': player->change_program(-1); break;
    case 'm': player->toggle_mute(); break;
    case '+': case '=': player->volume(+0.1f); break;
    case '-': player->volume(-0.1f); break;
    case 'f':
        fullscreen = !fullscreen;
        if (fullscreen) glutFullScreen();
        else glutReshapeWindow(1280, 720);
        break;
    }
}

static void special(int key, int, int) {
    if (key == GLUT_KEY_RIGHT || key == GLUT_KEY_UP) player->change_program(+1);
    if (key == GLUT_KEY_LEFT || key == GLUT_KEY_DOWN) player->change_program(-1);
}

static void run(int *argc, char **argv) {
    glutInit(argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(win_w, win_h);
    glutCreateWindow("ATSC TV");
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(special);
    glutTimerFunc(5, timer, 0);
    glutMainLoop();
    g_stop = true;
}
}  // namespace win


// -------------------- main --------------------

int main(int argc, char *argv[]) {
    int channel, program;
    double freq, gain;
    std::string antenna, args, file, out_path, record_path, player_cmd;
    bool do_scan = false, use_vlc = false, no_video = false;
    double duration;

    po::options_description desc("Options");
    desc.add_options()
        ("help,h", "show help")
        ("scan", po::bool_switch(&do_scan), "list RF channels with an ATSC pilot")
        ("channel,c", po::value<int>(&channel)->default_value(0), "RF channel to receive (2-36)")
        ("freq,f", po::value<double>(&freq)->default_value(0.0), "channel center frequency in Hz (instead of --channel)")
        ("program,p", po::value<int>(&program)->default_value(0), "subchannel to start on (0 = first)")
        ("gain,g", po::value<double>(&gain)->default_value(55.0), "rx gain (dB); too much overloads on strong signals")
        ("antenna,a", po::value<std::string>(&antenna)->default_value("TX/RX"), "rx antenna (TX/RX or RX2)")
        ("args", po::value<std::string>(&args)->default_value("type=b200"), "UHD device args")
        ("out,o", po::value<std::string>(&out_path)->default_value(""), "save the MPEG transport stream to this file")
        ("record", po::value<std::string>(&record_path)->default_value(""), "also save the raw IQ (sc16) for --file")
        ("file", po::value<std::string>(&file)->default_value(""), "play a recording instead of the radio (needs --freq)")
        ("vlc", po::bool_switch(&use_vlc), "send the stream to VLC instead of the built-in player")
        ("player", po::value<std::string>(&player_cmd)->default_value("vlc --demux=ts --file-caching=1500 - 2>/dev/null"),
            "player command for --vlc (reads a transport stream from stdin)")
        ("no-video", po::bool_switch(&no_video), "no window or sound; just decode (use with --out)")
        ("duration", po::value<double>(&duration)->default_value(0.0), "stop after this many seconds (0 = until closed)");
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
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);
    signal(SIGPIPE, SIG_IGN);

    if (do_scan) {
        auto usrp = uhd::usrp::multi_usrp::make(args);
        usrp->set_rx_antenna(antenna, 0);
        scan(usrp, gain);
        return 0;
    }

    double fc = freq > 0.0 ? freq : channel_center(channel);
    if (fc <= 0.0) {
        std::cerr << "Give --channel (2-36), --freq, or --scan" << std::endl;
        return 1;
    }

    FILE *ts_out = out_path.empty() ? nullptr : fopen(out_path.c_str(), "wb");
    FILE *vlc = use_vlc ? popen(player_cmd.c_str(), "w") : nullptr;
    std::unique_ptr<atsc::Player> player;
    if (!use_vlc && !no_video) {
        player = std::make_unique<atsc::Player>();
        player->start(program);
    }
    Receiver rx(fc, ts_out, vlc, player.get());
    auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };

    std::vector<std::thread> threads;
    Queue<std::vector<cf>> iq_q(64);
    Queue<Segment> seg_q(4096);
    std::atomic<long> dropped{0}, overflows{0};
    uhd::usrp::multi_usrp::sptr usrp;
    uhd::rx_streamer::sptr stream;
    FILE *rec = nullptr;

    if (!file.empty()) {
        // recording: read at real-time speed so the player's clock works
        threads.emplace_back([&] {
            std::ifstream in(file, std::ios::binary);
            if (!in) {
                fprintf(stderr, "Couldn't open %s\n", file.c_str());
                g_stop = true;
                return;
            }
            std::vector<int16_t> raw(1 << 18);
            long samples = 0;
            auto start = std::chrono::steady_clock::now();
            while (!g_stop && (in.read((char *)raw.data(), raw.size() * 2) || in.gcount() > 0)) {
                size_t n = in.gcount() / 4;
                std::vector<cf> iq(n);
                for (size_t i = 0; i < n; i++) iq[i] = cf(raw[2 * i], raw[2 * i + 1]) * (1.0f / 32767.0f);
                iq_q.push(std::move(iq));
                samples += n;
                std::this_thread::sleep_until(start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                          std::chrono::duration<double>(samples / atsc::SAMPLE_RATE)));
            }
            fprintf(stderr, "\nEnd of recording\n");
            std::this_thread::sleep_for(std::chrono::seconds(2));
            g_stop = true;
        });
    } else {
        usrp = uhd::usrp::multi_usrp::make(args);
        usrp->set_rx_rate(atsc::SAMPLE_RATE, 0);
        if (std::abs(usrp->get_rx_rate(0) - atsc::SAMPLE_RATE) > 1.0) {
            std::cerr << "B200 gave " << usrp->get_rx_rate(0) << " S/s, need " << atsc::SAMPLE_RATE << std::endl;
            return 1;
        }
        usrp->set_rx_freq(uhd::tune_request_t(fc), 0);
        usrp->set_rx_gain(gain, 0);
        usrp->set_rx_antenna(antenna, 0);
        usrp->set_rx_bandwidth(8e6, 0);
        fprintf(stderr, "Tuned to %.3f MHz (RF channel %d), %.6f MS/s, gain %.0f dB, antenna %s\n", fc / 1e6, channel,
                atsc::SAMPLE_RATE / 1e6, usrp->get_rx_gain(0), usrp->get_rx_antenna(0).c_str());
        uhd::stream_args_t sa("fc32", "sc16");
        sa.channels = {0};
        stream = usrp->get_rx_stream(sa);
        rec = record_path.empty() ? nullptr : fopen(record_path.c_str(), "wb");
        threads.emplace_back([&] {
            uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
            cmd.stream_now = true;
            stream->issue_stream_cmd(cmd);
            uhd::rx_metadata_t md;
            std::vector<int16_t> recbuf;
            while (!g_stop) {
                std::vector<cf> buf(1 << 18);
                size_t n = stream->recv(buf.data(), buf.size(), md, 1.0);
                if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) overflows++;
                if (n == 0) continue;
                buf.resize(n);
                if (rec) {
                    recbuf.resize(2 * n);
                    for (size_t i = 0; i < n; i++) {
                        recbuf[2 * i] = (int16_t)std::max(-32767.0f, std::min(32767.0f, buf[i].real() * 32767.0f));
                        recbuf[2 * i + 1] = (int16_t)std::max(-32767.0f, std::min(32767.0f, buf[i].imag() * 32767.0f));
                    }
                    fwrite(recbuf.data(), 2, recbuf.size(), rec);
                }
                if (!iq_q.push(std::move(buf))) dropped++;
            }
            stream->issue_stream_cmd(uhd::stream_cmd_t(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS));
        });
    }
    threads.emplace_back([&] {
        std::vector<cf> buf;
        while (!g_stop) {
            if (iq_q.pop(buf)) {
                rx.demod(buf.data(), buf.size(), [&](Segment &&s) {
                    if (!seg_q.push(std::move(s))) dropped++;
                });
            }
        }
    });
    threads.emplace_back([&] {
        Segment s;
        while (!g_stop) {
            if (seg_q.pop(s)) rx.decode(s);
        }
    });
    if (duration > 0.0) {
        threads.emplace_back([&] {
            while (!g_stop && elapsed() < duration) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            g_stop = true;
        });
    }

    if (player) {
        win::player = player.get();
        win::receiver = &rx;
        win::elapsed = elapsed;
        win::run(&argc, argv);
    } else {
        double last = 0.0;
        while (!g_stop) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (elapsed() - last >= 0.5) {
                rx.status(elapsed());
                if (dropped || overflows) fprintf(stderr, "[%ld dropped, %ld overflows] ", dropped.load(), overflows.load());
                last = elapsed();
            }
        }
    }
    g_stop = true;
    for (auto &t : threads) t.join();
    if (rec) fclose(rec);
    if (player) player->stop();
    rx.summary();
    if (dropped || overflows) fprintf(stderr, "%ld dropped, %ld overflows\n", dropped.load(), overflows.load());
    if (ts_out) fclose(ts_out);
    if (vlc) pclose(vlc);
    return 0;
}
