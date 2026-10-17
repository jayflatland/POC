#pragma once

// Capture + decode + attribution.
//
// Radio thread: alternates focus rounds on the busiest channels with survey
// sweeps over all the others (hops ordered to avoid the B200's slow
// recalibration), captures a dwell of 20 MS/s IQ per visit, queues it. Worker threads: run test07's OFDM / DSSS receivers on each
// dwell, then work out who transmitted every PPDU:
//   - decoded frames carry their transmitter address (TA)
//   - ACK / CTS have no TA, but follow the frame they answer by SIFS, and
//     their RA is that frame's transmitter; so an undecodable 802.11n/ac/ax
//     data frame gets its sender from the ACK / Block Ack after it
//   - an ACK whose answered frame we know was sent to X came from X
//   - otherwise an ACK came from the "other end" of its RA: a client's AP,
//     or (RA is an AP) the client of that AP with the closest signal level

#include <atomic>
#include <cstdarg>
#include <cstring>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <thread>

#include <uhd/usrp/multi_usrp.hpp>

#include "dsss_rx.hpp"
#include "mac80211.hpp"
#include "model.hpp"
#include "ofdm_rx.hpp"

namespace wf {

inline double chan_freq(int ch) {
    if (ch >= 1 && ch <= 13) return (2407.0 + 5.0 * ch) * 1e6;
    if (ch == 14) return 2484e6;
    return (5000.0 + 5.0 * ch) * 1e6;
}

inline std::vector<int> band_channels(const std::string &band) {
    std::vector<int> c;
    if (band == "2.4" || band == "all") {
        for (int i = 1; i <= 11; i++) c.push_back(i);
    }
    if (band == "5" || band == "all") {
        for (int i : {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153,
                      157, 161, 165})
            c.push_back(i);
    }
    if (c.empty()) {
        std::stringstream ss(band);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            if (!tok.empty()) c.push_back(std::stoi(tok));
        }
    }
    return c;
}

inline const char *frame_name(int type, int sub) {
    static const char *mgmt[16] = {"Assoc Req", "Assoc Resp", "Reassoc Req", "Reassoc Resp", "Probe Req", "Probe Resp",
                                   "Timing Adv", "Mgmt", "Beacon", "ATIM", "Disassoc", "Auth", "Deauth", "Action",
                                   "Action No Ack", "Mgmt"};
    static const char *ctrl[16] = {"Control", "Control", "Trigger", "TACK", "BF Report Poll", "NDP Announce",
                                   "Control Ext", "Control Wrapper", "Block Ack Req", "Block Ack", "PS-Poll", "RTS", "CTS",
                                   "ACK", "CF-End", "CF-End+Ack"};
    static const char *data[16] = {"Data", "Data", "Data", "Data", "Null", "CF-Ack", "CF-Poll", "CF-Ack+Poll",
                                   "QoS Data", "QoS Data", "QoS Data", "QoS Data", "QoS Null", "Reserved", "QoS CF-Poll",
                                   "QoS CF-Ack+Poll"};
    if (type == 0) return mgmt[sub];
    if (type == 1) return ctrl[sub];
    if (type == 2) return data[sub];
    return "Extension";
}

struct Config {
    std::vector<int> channels = band_channels("all");
    double focus_dwell = 0.25;  // seconds per visit to a focus channel
    double focus_stay = 0.8;    // seconds to cycle a cluster of nearby focus channels before moving on
    double survey_dwell = 0.11; // seconds per visit elsewhere (>= one 102 ms beacon interval)
    double survey_share = 0.35; // fraction of time spent sweeping the other channels
    int focus_n = 3;            // channels to lock onto (pinned ones count)
    double min_activity = 0.01; // airtime fraction for a channel to qualify as active
    double gain = 40.0;
    std::string antenna = "TX/RX";
    std::string args = "type=b200";
    int workers = 3;
    double raw_keep = 600.0;    // seconds of per-packet detail to keep (airtime bins are kept for the session)
    std::string file;           // replay a 20 MS/s cf32 recording of one channel
    int file_channel = 0;
    bool realtime = false;      // pace file replay at real time
};

class Engine {
public:
    static constexpr double FS = 20e6;
    Model model;

    ~Engine() { stop(); }

    void start(const Config &cfg) {
        cfg_ = cfg;
        survey_share_ = cfg.survey_share;
        focus_n_ = cfg.focus_n;
        stop_ = false;
        done_ = false;
        session_wall_ = wall_now();
        size_t max_dwell = (size_t)(std::max(cfg_.focus_dwell, cfg_.survey_dwell) * FS) + SETTLE;
        for (int i = 0; i < cfg_.workers + 3; i++) {
            auto d = std::make_unique<Dwell>();
            d->x.reserve(max_dwell);
            free_.push_back(std::move(d));
        }
        // FFTW planning isn't thread-safe: build every receiver here, before the threads
        for (int i = 0; i < cfg_.workers; i++) {
            rx_.push_back(std::make_unique<Receivers>());
            rx_.back()->ofdm.report_undecoded = true;
            rx_.back()->dsss.report_undecoded = true;
        }
        for (int i = 0; i < cfg_.workers; i++) {
            workers_.emplace_back([this, i] { worker(*rx_[i]); });
        }
        radio_ = std::thread([this] {
            try {
                if (cfg_.file.empty()) radio_loop();
                else file_loop();
            } catch (const std::exception &e) {
                set_status(std::string("error: ") + e.what());
            }
            done_ = true;
        });
    }

    void stop() {
        stop_ = true;
        pool_cv_.notify_all();
        if (radio_.joinable()) radio_.join();
        work_cv_.notify_all();
        for (auto &w : workers_) {
            if (w.joinable()) w.join();
        }
        workers_.clear();
    }

    // radio thread finished (file replay done, or an error) and every dwell is processed
    bool finished() {
        std::lock_guard<std::mutex> l(pool_mu_);
        return done_ && full_.empty() && busy_ == 0;
    }

    double session_wall() const { return session_wall_; }
    const std::vector<int> &channels() const { return cfg_.channels; }
    int tuned() const { return tuned_; }

    void set_pinned(int ch, bool on) {
        std::lock_guard<std::mutex> l(sched_mu_);
        if (on) pinned_.insert(ch);
        else pinned_.erase(ch);
    }
    std::set<int> pinned() {
        std::lock_guard<std::mutex> l(sched_mu_);
        return pinned_;
    }
    // live-adjustable scheduling
    void set_survey_share(double s) { survey_share_ = s; }
    void set_focus_n(int n) { focus_n_ = n; }
    double survey_share() const { return survey_share_; }
    int focus_n() const { return focus_n_; }

    std::set<int> focus() {
        std::lock_guard<std::mutex> l(sched_mu_);
        return focus_;
    }

    std::string status() {
        std::lock_guard<std::mutex> l(status_mu_);
        return status_;
    }

private:
    static constexpr size_t SETTLE = (size_t)(0.003 * FS);  // discard after retuning

    struct Dwell {
        int ch = 0;
        double t0 = 0.0;  // session time of x[0]
        bool survey = false;
        std::vector<cf> x;
    };

    struct Receivers {
        OfdmRx ofdm;
        DsssRx dsss;
    };

    // one PPDU, while working out who sent it
    struct Obs {
        double t = 0, end = 0;
        float rssi = 0;
        int format = 0;
        const char *phy = "";
        const char *what = "";
        int bytes = -1;
        bool decoded = false, has_ta = false, response = false;
        int type = -1, sub = -1;
        bool to_ds = false, from_ds = false;
        std::string ra, ta, bssid;
        bool ra_unicast = false;
        std::vector<uint8_t> psdu;
        int sender = -1, recv = -1, net = -1;
        bool sender_certain = false, guess = false;
    };

    static double wall_now() {
        return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    void set_status(const std::string &s) {
        std::lock_guard<std::mutex> l(status_mu_);
        status_ = s;
    }

    // ---------------- dwell buffers ----------------

    std::unique_ptr<Dwell> get_free() {
        std::unique_lock<std::mutex> l(pool_mu_);
        pool_cv_.wait(l, [&] { return !free_.empty() || stop_; });
        if (stop_) return nullptr;
        auto d = std::move(free_.back());
        free_.pop_back();
        return d;
    }

    void queue(std::unique_ptr<Dwell> d) {
        std::lock_guard<std::mutex> l(pool_mu_);
        full_.push_back(std::move(d));
        work_cv_.notify_one();
    }

    void worker(Receivers &rx) {
        while (true) {
            std::unique_ptr<Dwell> d;
            {
                std::unique_lock<std::mutex> l(pool_mu_);
                work_cv_.wait_for(l, std::chrono::milliseconds(100), [&] { return !full_.empty() || stop_; });
                if (full_.empty()) {
                    if (stop_) return;
                    continue;
                }
                d = std::move(full_.front());
                full_.pop_front();
                busy_++;
            }
            process(*d, rx);
            std::lock_guard<std::mutex> l(pool_mu_);
            busy_--;
            dwells_done_++;
            free_.push_back(std::move(d));
            pool_cv_.notify_one();
        }
    }

    // ---------------- scheduling ----------------

    std::vector<int> choose_focus() {
        // a wide (40/80 MHz) network is heard on every 20 MHz channel it covers, but its
        // beacons and narrow traffic are only on its primary: credit activity to that
        std::map<int, float> score;
        {
            std::lock_guard<std::mutex> l(model.mu);
            for (int ch : cfg_.channels) {
                auto it = model.chans.find(ch);
                if (it == model.chans.end()) continue;
                const ChannelInfo &ci = it->second;
                int target = ch;
                auto dom = std::max_element(ci.net_air.begin(), ci.net_air.end(),
                                            [](auto &a, auto &b) { return a.second < b.second; });
                if (dom != ci.net_air.end() && dom->second > 0.5f * ci.activity) {
                    int p = primary_of(dom->first);
                    if (p > 0 && std::count(cfg_.channels.begin(), cfg_.channels.end(), p)) target = p;
                }
                score[target] = std::max(score[target], ci.activity);
            }
        }
        std::vector<std::pair<float, int>> scored;
        for (auto &s : score) scored.push_back({s.second, s.first});
        std::sort(scored.begin(), scored.end(), [](auto &a, auto &b) { return a.first > b.first; });
        std::lock_guard<std::mutex> l(sched_mu_);
        std::vector<int> f;
        for (int ch : cfg_.channels) {
            if (pinned_.count(ch)) f.push_back(ch);
        }
        for (auto &s : scored) {
            if ((int)f.size() >= focus_n_) break;
            if (s.first < cfg_.min_activity) break;
            if (!pinned_.count(s.second)) f.push_back(s.second);
        }
        std::sort(f.begin(), f.end());
        focus_ = std::set<int>(f.begin(), f.end());
        return f;
    }

    // beacon channel of a network's busiest AP (model.mu held)
    int primary_of(int net) {
        int best = 0;
        double air = -1;
        for (auto &d : model.devices) {
            if (d.is_ap && d.net == net && d.primary > 0 && d.row.airtime > air) {
                air = d.row.airtime;
                best = d.primary;
            }
        }
        return best;
    }

    void radio_loop() {
        usrp_ = uhd::usrp::multi_usrp::make(cfg_.args);
        usrp_->set_rx_antenna(cfg_.antenna, 0);
        usrp_->set_rx_gain(cfg_.gain, 0);
        usrp_->set_rx_rate(FS, 0);
        usrp_->set_rx_bandwidth(FS, 0);
        if (std::abs(usrp_->get_rx_rate(0) - FS) > 1.0) throw std::runtime_error("B200 won't do 20 MS/s");
        usrp_->set_time_now(uhd::time_spec_t(0.0));
        session_wall_ = wall_now();
        uhd::stream_args_t sa("fc32", "sc16");
        sa.channels = {0};
        rx_stream_ = usrp_->get_rx_stream(sa);

        // Time is shared between focus rounds and a survey sweep over every other
        // channel; the first sweep runs alone so focus has something to go on.
        std::vector<int> pending;  // channels the current sweep hasn't visited yet
        bool first = true;
        double sweep_start = wall_now(), t_focus = 0.0, t_survey = 0.0;
        std::vector<int> f;
        while (!stop_) {
            if (!first) f = choose_focus();
            if (pending.empty()) {
                if (!first || t_survey > 0) {
                    sweep_secs_ = wall_now() - sweep_start;
                    first = false;
                    f = choose_focus();
                }
                for (int ch : cfg_.channels) {
                    if (!std::count(f.begin(), f.end(), ch)) pending.push_back(ch);
                }
                std::sort(pending.begin(), pending.end(), [](int a, int b) { return chan_freq(a) < chan_freq(b); });
                sweep_start = wall_now();
            }
            double share = std::clamp(survey_share_.load(), 0.0, 0.95);
            bool survey = first || f.empty() || (!pending.empty() && t_survey < share / (1.0 - share) * t_focus);
            double t0 = wall_now();
            if (survey) survey_batch(pending, first ? "initial survey" : "survey");
            else focus_round(f);
            (survey ? t_survey : t_focus) += wall_now() - t0;
        }
        usrp_.reset();
    }

    // UHD recalibrates the AD9361 (~155 ms) whenever the LO lands more than 100 MHz
    // from where it last calibrated; a smaller hop costs ~3 ms. So hops are planned
    // in clusters reachable from the current calibration point.
    static constexpr double CAL_SPAN = 95e6;
    bool cheap(int ch) const { return cal_freq_ > 0 && std::abs(chan_freq(ch) - cal_freq_) <= CAL_SPAN; }

    // survey channels until ~1.5 s or the cheap ones run out (at most one recalibration)
    void survey_batch(std::vector<int> &pending, const char *why) {
        double t0 = wall_now();
        bool jumped = false;
        while (!pending.empty() && !stop_ && wall_now() - t0 < 1.5) {
            auto it = std::find_if(pending.begin(), pending.end(), [&](int ch) { return cheap(ch); });
            if (it == pending.end()) {
                if (jumped) break;
                // land where the most unvisited channels are within reach
                int best = 0;
                for (auto c = pending.begin(); c != pending.end(); ++c) {
                    int n = (int)std::count_if(pending.begin(), pending.end(), [&](int o) {
                        return std::abs(chan_freq(o) - chan_freq(*c)) <= CAL_SPAN;
                    });
                    if (n > best) {
                        best = n;
                        it = c;
                    }
                }
                jumped = true;
            }
            int ch = *it;
            pending.erase(it);
            capture(ch, cfg_.survey_dwell, true, why);
        }
    }

    // each cluster of focus channels gets cycled for focus_stay seconds; the cluster
    // reachable without recalibrating goes first
    void focus_round(std::vector<int> f) {
        std::sort(f.begin(), f.end(), [](int a, int b) { return chan_freq(a) < chan_freq(b); });
        std::vector<std::vector<int>> groups;
        for (int ch : f) {
            if (groups.empty() || chan_freq(ch) - chan_freq(groups.back().front()) > CAL_SPAN) groups.push_back({});
            groups.back().push_back(ch);
        }
        std::stable_partition(groups.begin(), groups.end(), [&](const std::vector<int> &g) {
            return std::any_of(g.begin(), g.end(), [&](int ch) { return cheap(ch); });
        });
        for (auto &g : groups) {
            double t0 = wall_now();
            do {
                for (int ch : g) {
                    if (stop_) return;
                    capture(ch, cfg_.focus_dwell, false, "focus");
                }
            } while (wall_now() - t0 < cfg_.focus_stay && !stop_);
        }
    }

    void capture(int ch, double dur, bool survey, const char *why) {
        auto d = get_free();
        if (!d) return;
        tuned_ = ch;
        double tt = wall_now();
        usrp_->set_rx_freq(uhd::tune_request_t(chan_freq(ch)), 0);
        if (wall_now() - tt > 0.04 || cal_freq_ <= 0) cal_freq_ = chan_freq(ch);  // that one recalibrated
        for (int i = 0; i < 100 && !usrp_->get_rx_sensor("lo_locked", 0).to_bool(); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        size_t want = SETTLE + (size_t)(dur * FS);
        d->x.resize(want);
        uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
        cmd.num_samps = want;
        cmd.stream_now = true;
        rx_stream_->issue_stream_cmd(cmd);
        uhd::rx_metadata_t md;
        size_t got = 0;
        double t_first = -1.0;
        while (got < want) {
            size_t n = rx_stream_->recv(&d->x[got], want - got, md, 1.0);
            if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) break;
            if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
                overflows_++;
                continue;
            }
            if (t_first < 0 && md.has_time_spec) t_first = md.time_spec.get_real_secs() - got / FS;
            got += n;
            if (md.end_of_burst) break;
        }
        if (got <= SETTLE + 4000 || t_first < 0) {
            std::lock_guard<std::mutex> l(pool_mu_);
            free_.push_back(std::move(d));
            return;
        }
        d->x.erase(d->x.begin(), d->x.begin() + SETTLE);
        d->x.resize(got - SETTLE);
        d->ch = ch;
        d->t0 = t_first + SETTLE / FS;
        d->survey = survey;
        update_status(why);
        queue(std::move(d));
    }

    void update_status(const char *why) {
        double now = wall_now();
        long done;
        size_t backlog;
        {
            std::lock_guard<std::mutex> l(pool_mu_);
            done = dwells_done_;
            backlog = full_.size();
        }
        if (now - rate_t_ > 1.0) {
            rate_ = (done - rate_done_) / (now - rate_t_);
            rate_t_ = now;
            rate_done_ = done;
        }
        long frames;
        {
            std::lock_guard<std::mutex> l(model.mu);
            frames = model.frames_decoded + model.frames_timing_only;
        }
        std::string sweep = sweep_secs_ > 0 ? fmt("full sweep %.0f s", sweep_secs_.load()) : "first sweep";
        set_status(fmt("ch %d (%s) | %s | %.1f dwells/s | backlog %zu | %ld PPDUs | %ld overflows", tuned_.load(), why,
                       sweep.c_str(), rate_, backlog, frames, overflows_.load()));
    }

    __attribute__((format(printf, 1, 2))) static std::string fmt(const char *f, ...) {
        char b[256];
        va_list ap;
        va_start(ap, f);
        vsnprintf(b, sizeof(b), f, ap);
        va_end(ap);
        return b;
    }

    void file_loop() {
        std::ifstream in(cfg_.file, std::ios::binary);
        if (!in) throw std::runtime_error("can't open " + cfg_.file);
        int ch = cfg_.file_channel;
        tuned_ = ch;
        size_t n = (size_t)(cfg_.focus_dwell * FS);
        double t = 0.0;
        double wall0 = wall_now();
        session_wall_ = wall0;
        while (!stop_) {
            auto d = get_free();
            if (!d) return;
            d->x.resize(n);
            in.read((char *)d->x.data(), n * sizeof(cf));
            size_t got = in.gcount() / sizeof(cf);
            if (got < 4000) {
                std::lock_guard<std::mutex> l(pool_mu_);
                free_.push_back(std::move(d));
                break;
            }
            d->x.resize(got);
            d->ch = ch;
            d->t0 = t;
            t += got / FS;
            if (cfg_.realtime) {
                double ahead = (wall0 + t) - wall_now();
                if (ahead > 0) std::this_thread::sleep_for(std::chrono::duration<double>(ahead));
            }
            update_status("file");
            queue(std::move(d));
        }
        set_status(fmt("file done: %.1f s of channel %d", t, ch));
    }

    // ---------------- decode + attribute ----------------

    static std::string mac_at(const uint8_t *p, bool clear_group_bit = false) {
        uint8_t m[6];
        memcpy(m, p, 6);
        if (clear_group_bit) m[0] &= ~1;  // RTS / BAR use the TA's group bit to signal bandwidth
        return mac_to_string(m);
    }

    static void parse_mac(Obs &o) {
        const auto &d = o.psdu;
        size_t n = d.size();
        o.type = (d[0] >> 2) & 3;
        o.sub = d[0] >> 4;
        o.to_ds = d[1] & 1;
        o.from_ds = d[1] & 2;
        o.what = frame_name(o.type, o.sub);
        o.ra = mac_at(&d[4]);
        o.ra_unicast = !(d[4] & 1);
        if (o.type == 1) {
            static const bool ta_sub[16] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1};
            if (ta_sub[o.sub] && n >= 20) {
                o.ta = mac_at(&d[10], true);
                o.has_ta = true;
            }
            o.response = o.sub == 13 || o.sub == 12 || o.sub == 9;  // ACK, CTS, Block Ack
        } else if (n >= 24) {
            o.ta = mac_at(&d[10]);
            o.has_ta = true;
            std::string a3 = mac_at(&d[16]);
            if (o.type == 0) o.bssid = a3;
            else if (o.to_ds && !o.from_ds) o.bssid = o.ra;
            else if (o.from_ds && !o.to_ds) o.bssid = o.ta;
            else if (!o.to_ds && !o.from_ds) o.bssid = a3;
        }
    }

    void process(Dwell &dw, Receivers &rx) {
        std::vector<Obs> obs;
        RxStats st;
        auto on_frame = [&](const RxFrame &f) {
            if (f.end <= f.start) return;
            Obs o;
            o.t = dw.t0 + f.start / FS;
            o.end = dw.t0 + f.end / FS;
            o.rssi = f.power_db;
            o.format = f.format;
            o.phy = intern(f.phy);
            if (f.format == FMT_LEGACY) {
                if (!fcs_ok(f.psdu)) {
                    o.what = "bad FCS";
                } else {
                    o.decoded = true;
                    o.psdu = f.psdu;
                    o.bytes = (int)f.psdu.size();
                    parse_mac(o);
                }
            }
            obs.push_back(std::move(o));
        };
        rx.ofdm.process(dw.x, FS, on_frame, st);
        if (dw.ch <= 14) rx.dsss.process(dw.x, FS, on_frame, st);
        std::sort(obs.begin(), obs.end(), [](const Obs &a, const Obs &b) { return a.t < b.t; });
        // the DSSS and OFDM detectors can both report one PPDU; keep the first
        std::vector<Obs> uniq;
        for (auto &o : obs) {
            if (!uniq.empty() && o.t < uniq.back().end && o.format == uniq.back().format && !o.decoded) continue;
            uniq.push_back(std::move(o));
        }

        double dwell_end = dw.t0 + dw.x.size() / FS;
        std::lock_guard<std::mutex> l(model.mu);
        attribute(uniq, dw.ch);
        if (getenv("WF_DEBUG")) {
            for (auto &o : uniq) {
                printf("DBG %10.6f %7.1fus %6.1fdB %-9s %-14s ta=%s ra=%s sender=%s\n", o.t, (o.end - o.t) * 1e6, o.rssi,
                       o.phy, o.what, o.ta.c_str(), o.ra.c_str(),
                       o.sender >= 0 ? model.devices[o.sender].mac.c_str() : (o.net >= 0 ? "net" : "-"));
            }
        }
        commit(uniq, dw.ch);
        auto &ci = model.chans[dw.ch];
        double air = 0;
        for (auto &o : uniq) air += o.end - o.t;
        double span = dwell_end - dw.t0;
        float frac = (float)(air / span);
        bool first = ci.last_visit < 0;
        ci.activity = first ? frac : 0.5f * ci.activity + 0.5f * frac;
        for (auto &na : ci.net_air) na.second *= 0.5f;
        for (auto &na : dwell_net_air) ci.net_air[na.first] += (first ? 1.0f : 0.5f) * (float)(na.second / span);
        ci.last_visit = dwell_end;
        ci.frames += (long)uniq.size();
        model.cover(dw.ch, dw.t0, dwell_end);
        if (model.latest - last_trim_ > 10.0) {
            last_trim_ = model.latest;
            model.trim(model.latest - cfg_.raw_keep);
        }
    }

    // the device at the other end of a link with `x` (who sends ACKs to x)
    std::pair<int, int> counterpart(int x, float rssi) {
        if (x < 0) return {-1, -1};
        const Device &d = model.devices[x];
        if (!d.is_ap) return {d.ap, -1};
        int best = -1;
        float best_diff = 4.0f;
        for (size_t i = 0; i < model.devices.size(); i++) {
            const Device &c = model.devices[i];
            if (c.ap == x && c.rssi_init && std::abs(c.rssi - rssi) < best_diff) {
                best_diff = std::abs(c.rssi - rssi);
                best = (int)i;
            }
        }
        return {best, best < 0 ? d.net : -1};
    }

    void learn(Obs &o, int ch) {
        Model &m = model;
        int s = o.has_ta ? m.device(o.ta, o.t) : -1;
        int r = o.ra_unicast ? m.device(o.ra, o.t) : -1;
        o.sender = s;
        o.recv = r;
        o.sender_certain = s >= 0;
        if (o.type == 0) {
            BeaconInfo bi;
            if ((o.sub == 8 || o.sub == 5) && parse_beacon(o.psdu, bi)) {
                m.mark_ap(s);
                std::string key = bi.hidden ? "hidden:" + bi.bssid : bi.ssid;
                int n = m.network(key);
                Network &net = m.nets[n];
                net.ssid = bi.hidden ? "" : bi.ssid;
                net.hidden = bi.hidden;
                net.placeholder = false;
                net.security = bi.security;
                m.devices[s].net = n;
                if (bi.channel > 0) m.devices[s].ch = m.devices[s].primary = bi.channel;
                if (o.sub == 5) o.recv = -1;  // probe responses aren't an association
            } else if (o.sub == 0 || o.sub == 2) {
                m.associate(s, r);
            } else if (o.sub == 1 || o.sub == 3) {
                m.mark_ap(s);
                m.associate(r, s);
            } else if (o.sub != 4 && s >= 0 && o.bssid != o.ta) {
                int b = m.find_device(o.bssid);
                if (b >= 0 && m.devices[b].is_ap) m.associate(s, b);
            }
        } else if (o.type == 2 && s >= 0) {
            if (o.to_ds && !o.from_ds) {
                m.associate(s, r);
            } else if (o.from_ds && !o.to_ds) {
                m.mark_ap(s);
                m.associate(r, s);
            } else if (o.to_ds && o.from_ds) {
                m.mark_ap(s);
                if (r >= 0) m.mark_ap(r);
            }
        } else if (o.type == 1 && o.sub == 10 && s >= 0) {
            m.associate(s, r);  // PS-Poll goes to the BSSID
        } else if (o.type == 1 && s >= 0 && r >= 0) {
            // Block Ack / BAR / RTS between an AP and a device it serves
            if (m.devices[s].is_ap && !m.devices[r].is_ap && m.devices[r].ap < 0) m.associate(r, s);
            else if (m.devices[r].is_ap && !m.devices[s].is_ap && m.devices[s].ap < 0) m.associate(s, r);
        }
        (void)ch;
    }

    // which of two candidate senders a frame's signal level points to
    int closer(int a, int b, float rssi) {
        auto diff = [&](int d) {
            return d >= 0 && model.devices[d].rssi_init ? std::abs(model.devices[d].rssi - rssi) : 99.0f;
        };
        return diff(a) <= diff(b) ? a : b;
    }

    // last resort for a frame nobody vouches for: a device heard on this channel whose
    // average level is within 2 dB, and no other within 2 dB more than that
    int guess_by_rssi(float rssi, int ch, double t) {
        int best = -1;
        float d1 = 99.0f, d2 = 99.0f;
        for (size_t i = 0; i < model.devices.size(); i++) {
            const Device &d = model.devices[i];
            if (!d.rssi_init || t - d.last > 60.0) continue;
            if (!std::count(d.heard_on.begin(), d.heard_on.end(), ch)) continue;
            float df = std::abs(d.rssi - rssi);
            if (df < d1) {
                d2 = d1;
                d1 = df;
                best = (int)i;
            } else if (df < d2) {
                d2 = df;
            }
        }
        return d1 <= 2.0f && d2 - d1 >= 2.0f ? best : -1;
    }

    void attribute(std::vector<Obs> &obs, int ch) {
        for (auto &o : obs) {
            if (o.decoded) learn(o, ch);
        }
        for (size_t i = 0; i < obs.size(); i++) {
            Obs &o = obs[i];
            if (!o.decoded || !o.response) continue;
            // the frame this one answers ended a SIFS (10/16 us) earlier
            Obs *p = nullptr;
            for (size_t j = i; j-- > 0 && o.t - obs[j].end < 60e-6;) {
                double gap = o.t - obs[j].end;
                if (gap > 2e-6 && gap < 40e-6) {
                    p = &obs[j];
                    break;
                }
            }
            if (p) {
                if (p->sender < 0) p->sender = o.recv;  // RA of the response = whoever sent the frame
                if (o.sender < 0) {
                    if (p->recv >= 0) o.sender = p->recv;
                    else std::tie(o.sender, o.net) = counterpart(o.recv, o.rssi);
                }
                if (p->sender >= 0 && o.sender >= 0) {
                    const auto &a = model.devices[p->sender], &b = model.devices[o.sender];
                    if (a.is_ap && !b.is_ap && b.ap < 0) model.associate(o.sender, p->sender);
                    if (b.is_ap && !a.is_ap && a.ap < 0) model.associate(p->sender, o.sender);
                }
            } else if (o.sender < 0) {
                // the frame it answers was missed. A CTS may also be CTS-to-self (RA = its sender)
                auto cp = counterpart(o.recv, o.rssi);
                if (o.sub == 12 && closer(o.recv, cp.first, o.rssi) == o.recv) {
                    o.sender = o.recv;
                    o.recv = -1;
                } else {
                    std::tie(o.sender, o.net) = cp;
                }
            }
        }
        for (auto &o : obs) {
            if (o.sender < 0 && o.net < 0) {
                o.sender = guess_by_rssi(o.rssi, ch, o.t);
                o.guess = o.sender >= 0;
            }
        }
    }

    void commit(std::vector<Obs> &obs, int ch) {
        Model &m = model;
        dwell_net_air.clear();
        for (auto &o : obs) {
            Burst b;
            b.t = o.t;
            b.dur = (float)(o.end - o.t);
            b.rssi = o.rssi;
            b.bytes = o.bytes;
            b.ch = (int16_t)ch;
            b.format = (uint8_t)o.format;
            b.phy = o.phy;
            b.what = o.decoded ? o.what : (o.what[0] ? o.what : "");
            b.peer = o.recv;
            b.guess = o.guess;
            if (o.guess) m.frames_guessed++;
            if (o.decoded) m.frames_decoded++;
            else m.frames_timing_only++;
            m.air_total += b.dur;
            if (o.sender >= 0) {
                Device &d = m.devices[o.sender];
                bool to_ap = o.recv >= 0 && m.devices[o.recv].is_ap;
                b.dir = d.is_ap ? DIR_DOWN : (d.ap >= 0 || to_ap) ? DIR_UP : DIR_OTHER;
                d.row.add(b);
                d.last = std::max(d.last, o.t);
                int n = m.net_of(o.sender);
                if (n >= 0) dwell_net_air[n] += b.dur;
                if (!d.is_ap || d.ch == 0) d.ch = ch;
                if (!std::count(d.heard_on.begin(), d.heard_on.end(), ch)) d.heard_on.push_back(ch);
                if (o.sender_certain) {
                    d.rssi = d.rssi_init ? 0.9f * d.rssi + 0.1f * o.rssi : o.rssi;
                    d.rssi_init = true;
                }
                m.frames_attributed++;
                m.air_attributed += b.dur;
            } else if (o.net >= 0) {
                b.dir = DIR_UP;
                m.nets[o.net].unknown.add(b);
                dwell_net_air[o.net] += b.dur;
                m.frames_attributed++;
                m.air_attributed += b.dur;
            } else {
                b.dir = DIR_OTHER;
                m.chans[ch].unattributed.add(b);
            }
        }
    }

    Config cfg_;
    std::atomic<bool> stop_{false}, done_{false};
    std::atomic<int> tuned_{0};
    std::atomic<long> overflows_{0};
    std::atomic<double> sweep_secs_{0.0};
    std::atomic<double> survey_share_{0.35};
    std::atomic<int> focus_n_{3};  // how long the last full survey sweep took
    double cal_freq_ = 0.0;                // where the AD9361 last calibrated
    double session_wall_ = 0.0;
    std::thread radio_;
    std::vector<std::thread> workers_;
    std::vector<std::unique_ptr<Receivers>> rx_;

    std::mutex pool_mu_;
    std::condition_variable pool_cv_, work_cv_;
    std::vector<std::unique_ptr<Dwell>> free_;
    std::deque<std::unique_ptr<Dwell>> full_;
    int busy_ = 0;
    long dwells_done_ = 0;

    std::mutex sched_mu_;
    std::set<int> pinned_, focus_;

    std::mutex status_mu_;
    std::string status_;
    double rate_t_ = 0.0, rate_ = 0.0;
    long rate_done_ = 0;
    double last_trim_ = 0.0;
    std::map<int, double> dwell_net_air;  // scratch for commit(), model.mu held

    uhd::usrp::multi_usrp::sptr usrp_;
    uhd::rx_streamer::sptr rx_stream_;
};

}  // namespace wf
