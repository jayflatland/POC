#pragma once

// Capture engine: drives the B200 for the selected mode, runs the decoders,
// and hands finished packets to a callback (from the DSP thread).

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <uhd/usrp/multi_usrp.hpp>

#include "adsb.hpp"
#include "ble.hpp"
#include "dsss_rx.hpp"
#include "mac80211.hpp"
#include "ofdm_rx.hpp"
#include "packet.hpp"
#include "wifi.hpp"

namespace shark {

enum class Mode { Wifi, Ble, Adsb };

struct CaptureConfig {
    Mode mode = Mode::Wifi;
    std::vector<int> wifi_channels{1, 6, 11};
    double dwell = 0.3;
    double gain = 40.0;
    std::string antenna = "TX/RX";
    std::string args = "type=b200";
    bool keep_bad = false;
    std::string file;      // replay a cf32 recording instead of the radio
    double file_rate = 0;  // its sample rate
    double file_freq = 0;  // and center frequency
};

inline double wifi_freq(int ch) {
    if (ch >= 1 && ch <= 13) return (2407.0 + 5.0 * ch) * 1e6;
    if (ch == 14) return 2484e6;
    return (5000.0 + 5.0 * ch) * 1e6;
}

inline std::vector<int> wifi_channels_for(const std::string &band) {
    std::vector<int> c;
    if (band == "2.4" || band == "all") for (int i = 1; i <= 11; i++) c.push_back(i);
    if (band == "5" || band == "all")
        for (int i : {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165})
            c.push_back(i);
    return c;
}

class Capture {
public:
    using Emit = std::function<void(Packet &&)>;

    ~Capture() { stop(); }

    void start(const CaptureConfig &cfg, Emit emit) {
        stop();
        cfg_ = cfg;
        emit_ = std::move(emit);
        stop_ = false;
        t0_ = now();
        status_ = "starting";
        worker_ = std::thread([this] {
            try {
                run();
            } catch (const std::exception &e) {
                set_status(std::string("error: ") + e.what());
            }
        });
    }

    void stop() {
        stop_ = true;
        if (worker_.joinable()) worker_.join();
    }

    bool running() const { return worker_.joinable() && !stop_; }

    std::string status() {
        std::lock_guard<std::mutex> l(status_mutex_);
        return status_;
    }

private:
    static double now() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }

    void set_status(const std::string &s) {
        std::lock_guard<std::mutex> l(status_mutex_);
        status_ = s;
    }

    void deliver(Packet &&p) {
        p.wall = t0_ + p.time;
        emit_(std::move(p));
    }

    void run() {
        if (!cfg_.file.empty()) {
            run_file();
            return;
        }
        usrp_ = uhd::usrp::multi_usrp::make(cfg_.args);
        usrp_->set_rx_antenna(cfg_.antenna, 0);
        usrp_->set_rx_gain(cfg_.gain, 0);
        if (cfg_.mode == Mode::Wifi) run_wifi();
        else run_continuous();
        usrp_.reset();
    }

    // ---- WiFi: hop channels, capture a burst on each ----
    void run_wifi() {
        const double fs = 20e6;
        usrp_->set_rx_rate(fs, 0);
        usrp_->set_rx_bandwidth(fs, 0);
        uhd::stream_args_t sa("fc32", "sc16");
        sa.channels = {0};
        auto rx = usrp_->get_rx_stream(sa);
        const size_t settle = (size_t)(0.003 * fs);
        std::vector<cf> buf(settle + (size_t)(cfg_.dwell * fs));
        uhd::rx_metadata_t md;
        OfdmRx ofdm;
        DsssRx dsss;
        long frames = 0;
        while (!stop_) {
            for (int ch : cfg_.wifi_channels) {
                if (stop_) break;
                usrp_->set_rx_freq(uhd::tune_request_t(wifi_freq(ch)), 0);
                for (int i = 0; i < 100 && !usrp_->get_rx_sensor("lo_locked", 0).to_bool(); i++)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                set_status(fmt("WiFi channel %d (%.0f MHz), %ld frames", ch, wifi_freq(ch) / 1e6, frames));
                uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
                cmd.num_samps = buf.size();
                cmd.stream_now = true;
                rx->issue_stream_cmd(cmd);
                double t_start = now() - t0_;
                size_t got = 0;
                while (got < buf.size()) {
                    size_t n = rx->recv(&buf[got], buf.size() - got, md, 1.0);
                    if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) break;
                    if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) continue;
                    got += n;
                    if (md.end_of_burst) break;
                }
                if (got <= settle) continue;
                std::vector<cf> x(buf.begin() + settle, buf.begin() + got);
                decode_wifi(x, ch, t_start, ofdm, dsss, frames);
            }
        }
    }

    void decode_wifi(const std::vector<cf> &x, int ch, double t_start, OfdmRx &ofdm, DsssRx &dsss, long &frames) {
        RxStats st;
        auto on_frame = [&](const RxFrame &f) {
            bool good = fcs_ok(f.psdu);
            if (!good && !cfg_.keep_bad) return;
            Packet p;
            p.time = t_start;  // the decoders don't report the sample index; burst start is close enough
            p.channel = fmt("ch %d", ch);
            p.freq = wifi_freq(ch);
            p.rssi = f.power_db;
            p.link = Link::Wifi;
            p.data = f.psdu;
            p.bad_crc = !good;
            dissect_80211(p);
            Field &radio = *p.tree.children.emplace(p.tree.children.begin(), Field{"802.11 radio information", -1, 0, {}});
            radio.add("PHY: " + f.phy);
            radio.add(fmt("Channel: %d (%.0f MHz)", ch, wifi_freq(ch) / 1e6));
            radio.add(fmt("Signal strength: %.1f dBFS", f.power_db));
            frames++;
            deliver(std::move(p));
        };
        ofdm.process(x, 20e6, on_frame, st);
        if (ch <= 14) dsss.process(x, 20e6, on_frame, st);
    }

    // ---- BLE / ADS-B: one tuning, continuous stream ----
    struct Block {
        std::vector<cfloat> s;
        double t;
    };

    void run_continuous() {
        double fs = cfg_.mode == Mode::Ble ? 32e6 : ModeS::RATE;
        double fc = cfg_.mode == Mode::Ble ? 2414e6 : 1090e6;
        usrp_->set_rx_rate(fs, 0);
        usrp_->set_rx_bandwidth(fs, 0);
        usrp_->set_rx_freq(uhd::tune_request_t(fc), 0);
        uhd::stream_args_t sa("fc32", "sc16");
        sa.channels = {0};
        auto rx = usrp_->get_rx_stream(sa);

        std::deque<Block> q;
        std::mutex qm;
        std::condition_variable qcv;
        std::atomic<long> dropped{0}, overflows{0};
        std::atomic<bool> rx_done{false};
        std::thread radio([&] {
            uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
            cmd.stream_now = true;
            rx->issue_stream_cmd(cmd);
            uhd::rx_metadata_t md;
            while (!stop_) {
                Block b;
                b.s.resize(1 << 18);
                b.t = now() - t0_;
                size_t n = rx->recv(b.s.data(), b.s.size(), md, 1.0);
                if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) overflows++;
                if (n == 0) continue;
                b.s.resize(n);
                std::lock_guard<std::mutex> l(qm);
                if (q.size() > 200) {
                    dropped++;
                    continue;
                }
                q.push_back(std::move(b));
                qcv.notify_one();
            }
            rx->issue_stream_cmd(uhd::stream_cmd_t(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS));
            rx_done = true;
        });
        dsp_loop(q, qm, qcv, rx_done, fs, [&] { return fmt(", %ld dropped, %ld overflows", dropped.load(), overflows.load()); });
        radio.join();
    }

    template <typename Extra>
    void dsp_loop(std::deque<Block> &q, std::mutex &qm, std::condition_variable &qcv, std::atomic<bool> &done, double fs,
                  Extra extra) {
        ModeS modes;
        Channelizer chz;
        BleRx ble[2];
        if (cfg_.mode == Mode::Ble) {
            chz.init(fs, {2402e6 - 2414e6, 2426e6 - 2414e6}, 700e3);
            ble[0].init(37, 2402e6);
            ble[1].init(38, 2426e6);
        }
        long count = 0;
        auto emit = [&](Packet &&p) {
            count++;
            deliver(std::move(p));
        };
        double last = 0;
        while (!(stop_ && done)) {
            Block b;
            {
                std::unique_lock<std::mutex> l(qm);
                qcv.wait_for(l, std::chrono::milliseconds(100), [&] { return !q.empty(); });
                if (q.empty()) {
                    if (stop_) break;
                    continue;
                }
                b = std::move(q.front());
                q.pop_front();
            }
            if (cfg_.mode == Mode::Adsb) {
                modes.process(b.s.data(), b.s.size(), b.t, emit);
            } else {
                double t = b.t;
                chz.process(b.s.data(), b.s.size(), [&](int c, const cfloat *z, size_t n) { ble[c].process(z, n, t, emit); });
            }
            if (b.t - last > 0.5) {
                set_status((cfg_.mode == Mode::Adsb ? fmt("ADS-B 1090 MHz, %ld messages", count)
                                                    : fmt("BLE advertising channels 37+38, %ld packets", count)) + extra());
                last = b.t;
            }
        }
    }

    // ---- replay a recording (for testing) ----
    void run_file() {
        std::ifstream in(cfg_.file, std::ios::binary);
        if (!in) throw std::runtime_error("can't open " + cfg_.file);
        double fs = cfg_.file_rate;
        std::deque<Block> q;
        std::mutex qm;
        std::condition_variable qcv;
        std::atomic<bool> done{false};
        std::thread reader([&] {
            double t = 0;
            while (!stop_) {
                Block b;
                b.s.resize(1 << 18);
                if (!in.read((char *)b.s.data(), b.s.size() * sizeof(cfloat)) && in.gcount() == 0) break;
                b.s.resize(in.gcount() / sizeof(cfloat));
                b.t = t;
                t += b.s.size() / fs;
                std::unique_lock<std::mutex> l(qm);
                while (q.size() > 20 && !stop_) {
                    l.unlock();
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    l.lock();
                }
                q.push_back(std::move(b));
                qcv.notify_one();
            }
            done = true;
            stop_ = true;
        });
        if (cfg_.mode == Mode::Wifi) {
            // whole recording at once (it's one channel's burst)
            reader.join();
            std::vector<cf> x;
            for (auto &b : q) x.insert(x.end(), b.s.begin(), b.s.end());
            OfdmRx ofdm;
            DsssRx dsss;
            long frames = 0;
            int ch = cfg_.wifi_channels.empty() ? 1 : cfg_.wifi_channels[0];
            decode_wifi(x, ch, 0.0, ofdm, dsss, frames);
            set_status(fmt("file: %ld frames", frames));
            return;
        }
        dsp_loop(q, qm, qcv, done, fs, [] { return std::string(" (file)"); });
        reader.join();
    }

    CaptureConfig cfg_;
    Emit emit_;
    std::atomic<bool> stop_{true};
    std::thread worker_;
    double t0_ = 0;
    uhd::usrp::multi_usrp::sptr usrp_;
    std::mutex status_mutex_;
    std::string status_;
};

}  // namespace shark
