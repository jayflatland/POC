#pragma once

// What the GUI draws: networks (grouped by SSID) -> devices (MAC addresses),
// each with a time-ordered list of the PPDUs it transmitted, plus which
// stretches of time each channel was actually being listened to.

#include <algorithm>
#include <cstdint>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace wf {

enum Dir : uint8_t { DIR_OTHER = 0, DIR_DOWN = 1, DIR_UP = 2 };  // from an AP / to an AP / unknown or neither

// one PPDU on the air
struct Burst {
    double t = 0.0;     // start, seconds since session start
    float dur = 0.0f;   // airtime, seconds
    float rssi = 0.0f;  // dBFS
    int32_t bytes = -1; // MAC frame length when decoded
    int32_t peer = -1;  // device it was addressed to, when known
    int16_t ch = 0;
    uint8_t dir = DIR_OTHER;
    uint8_t format = 0;          // RxFormat
    uint8_t guess = 0;           // sender inferred from signal level only
    const char *phy = "";        // "OFDM 24M", "HT/VHT", ... (interned)
    const char *what = "";       // frame type when decoded ("QoS Data", "Beacon", "ACK")
};

// stable storage for the few distinct strings bursts point at
inline const char *intern(const std::string &s) {
    static std::mutex m;
    static std::set<std::string> pool;
    std::lock_guard<std::mutex> l(m);
    return pool.insert(s).first->c_str();
}

// airtime summary per 10 ms, kept for the whole session (raw bursts are trimmed)
struct Bin {
    int64_t idx = 0;
    float air[3] = {0, 0, 0};  // seconds of airtime by Dir
    uint32_t count = 0;
    float max_rssi = -200.0f;
};

static constexpr double BIN_SEC = 0.01;

struct Row {
    std::deque<Burst> raw;  // sorted by t
    std::vector<Bin> bins;  // sorted by idx
    long packets = 0;
    double airtime = 0.0;

    void add(const Burst &b) {
        packets++;
        airtime += b.dur;
        if (raw.empty() || raw.back().t <= b.t) {
            raw.push_back(b);
        } else {
            auto it = std::upper_bound(raw.begin(), raw.end(), b.t, [](double t, const Burst &x) { return t < x.t; });
            raw.insert(it, b);
        }
        int64_t idx = (int64_t)(b.t / BIN_SEC);
        Bin *bin;
        if (!bins.empty() && bins.back().idx == idx) {
            bin = &bins.back();
        } else if (bins.empty() || bins.back().idx < idx) {
            bins.push_back(Bin{idx});
            bin = &bins.back();
        } else {
            auto it = std::lower_bound(bins.begin(), bins.end(), idx, [](const Bin &x, int64_t i) { return x.idx < i; });
            if (it == bins.end() || it->idx != idx) {
                it = bins.insert(it, Bin{idx});
            }
            bin = &*it;
        }
        bin->air[b.dir] += b.dur;
        bin->count++;
        bin->max_rssi = std::max(bin->max_rssi, b.rssi);
    }

    void trim(double before) {
        while (!raw.empty() && raw.front().t < before) {
            raw.pop_front();
        }
    }

    // airtime in [t0, t1) from the bins
    double airtime_between(double t0, double t1) const {
        auto it = std::lower_bound(bins.begin(), bins.end(), (int64_t)(t0 / BIN_SEC),
                                   [](const Bin &x, int64_t i) { return x.idx < i; });
        double s = 0.0;
        for (; it != bins.end() && it->idx * BIN_SEC < t1; ++it) {
            s += it->air[0] + it->air[1] + it->air[2];
        }
        return s;
    }
};

struct Device {
    std::string mac;
    std::string vendor;
    bool random = false;  // locally administered address (phones randomize these)
    bool is_ap = false;
    int ap = -1;          // client: the AP (device index) it's associated with
    int net = -1;         // AP: its network
    int ch = 0;           // channel last heard on
    int primary = 0;      // AP: channel its beacons advertise
    std::vector<int> heard_on;
    float rssi = -100.0f;  // running average, from frames it certainly sent
    bool rssi_init = false;
    double first = 0.0, last = 0.0;
    Row row;
};

struct Network {
    std::string key;  // SSID, or "bssid:<mac>" until its beacon shows up
    std::string ssid;
    bool hidden = false, placeholder = false;
    std::string security;
    Row unknown;      // traffic known to be in this network from a device we couldn't identify
};

struct ChannelInfo {
    std::vector<std::pair<double, double>> covered;  // listened-to intervals, sorted
    Row unattributed;
    float activity = 0.0f;  // smoothed airtime fraction
    std::map<int, float> net_air;  // smoothed airtime fraction by network (an 80 MHz BSS shows up on 4 channels)
    double last_visit = -1.0;
    long frames = 0;
};

inline std::string vendor_lookup(const std::string &mac) {
    static std::unordered_map<uint32_t, std::string> db;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::ifstream in("/usr/share/ieee-data/oui.csv");
        std::string line;
        while (std::getline(in, line)) {
            // Registry,Assignment,Organization Name,Organization Address (name may be quoted)
            size_t a = line.find(',');
            if (a == std::string::npos) continue;
            size_t b = line.find(',', a + 1);
            if (b == std::string::npos || b - a - 1 != 6) continue;
            uint32_t oui = (uint32_t)strtoul(line.substr(a + 1, 6).c_str(), nullptr, 16);
            std::string name;
            if (b + 1 < line.size() && line[b + 1] == '"') {
                size_t e = line.find('"', b + 2);
                name = line.substr(b + 2, e == std::string::npos ? std::string::npos : e - b - 2);
            } else {
                size_t e = line.find(',', b + 1);
                name = line.substr(b + 1, e == std::string::npos ? std::string::npos : e - b - 1);
            }
            db[oui] = name;
        }
    }
    unsigned v[3];
    if (sscanf(mac.c_str(), "%x:%x:%x", &v[0], &v[1], &v[2]) != 3) return "";
    auto it = db.find((v[0] << 16) | (v[1] << 8) | v[2]);
    return it == db.end() ? "" : it->second;
}

class Model {
public:
    std::mutex mu;  // guards everything below
    std::vector<Device> devices;
    std::vector<Network> nets;
    std::map<int, ChannelInfo> chans;
    double latest = 0.0;  // end of the newest processed capture
    long frames_decoded = 0, frames_timing_only = 0, frames_attributed = 0, frames_guessed = 0;
    double air_total = 0.0, air_attributed = 0.0;

    int device(const std::string &mac, double t) {
        auto it = by_mac_.find(mac);
        if (it != by_mac_.end()) return it->second;
        Device d;
        d.mac = mac;
        unsigned b0 = 0;
        sscanf(mac.c_str(), "%x", &b0);
        d.random = b0 & 0x02;
        if (!d.random) d.vendor = vendor_lookup(mac);
        d.first = d.last = t;
        devices.push_back(std::move(d));
        return by_mac_[mac] = (int)devices.size() - 1;
    }

    int find_device(const std::string &mac) const {
        auto it = by_mac_.find(mac);
        return it == by_mac_.end() ? -1 : it->second;
    }

    int network(const std::string &key) {
        auto it = by_key_.find(key);
        if (it != by_key_.end()) return it->second;
        Network n;
        n.key = key;
        nets.push_back(std::move(n));
        return by_key_[key] = (int)nets.size() - 1;
    }

    // network a device belongs to (-1: unknown / not associated)
    int net_of(int d) const {
        if (d < 0) return -1;
        const Device &dv = devices[d];
        if (dv.is_ap) return dv.net;
        return dv.ap >= 0 ? devices[dv.ap].net : -1;
    }

    void mark_ap(int d) {
        if (d < 0) return;
        Device &dv = devices[d];
        if (dv.is_ap) return;
        dv.is_ap = true;
        dv.ap = -1;
        if (dv.net < 0) {
            int n = network("bssid:" + dv.mac);
            nets[n].placeholder = true;
            dv.net = n;
        }
    }

    void associate(int client, int ap) {
        if (client < 0 || ap < 0 || client == ap || devices[client].is_ap) return;
        mark_ap(ap);
        devices[client].ap = ap;
    }

    void cover(int ch, double t0, double t1) {
        auto &c = chans[ch].covered;
        if (!c.empty() && t0 <= c.back().second + 1e-3 && t0 >= c.back().first) {
            c.back().second = std::max(c.back().second, t1);
        } else {
            auto it = std::lower_bound(c.begin(), c.end(), std::make_pair(t0, t1));
            c.insert(it, {t0, t1});
        }
        latest = std::max(latest, t1);
    }

    void trim(double before) {
        for (auto &d : devices) d.row.trim(before);
        for (auto &n : nets) n.unknown.trim(before);
        for (auto &c : chans) c.second.unattributed.trim(before);
    }

private:
    std::unordered_map<std::string, int> by_mac_, by_key_;
};

}  // namespace wf
