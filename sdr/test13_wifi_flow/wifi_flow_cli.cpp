/*
Headless WiFi activity survey: runs the wifi_flow engine for a while and
prints every network, its devices, and how much airtime each one used.

    ./build/wifi_flow_cli -d 60                     # all bands, focus on the 3 busiest channels
    ./build/wifi_flow_cli --band 2.4 --focus 2 -d 30
    ./build/wifi_flow_cli --file ch36.cf32 --channel 36   # replay a 20 MS/s cf32 recording
*/

#include <csignal>
#include <cstdio>
#include <iostream>

#include <boost/program_options.hpp>

#include "engine.hpp"

namespace po = boost::program_options;

static volatile sig_atomic_t g_stop = 0;

static void print_row(const char *indent, const std::string &label, const wf::Row &r, double span) {
    printf("%s%-44s %7ld pkts  %8.1f ms  %5.2f%%\n", indent, label.c_str(), r.packets, r.airtime * 1e3,
           span > 0 ? 100.0 * r.airtime / span : 0.0);
}

int main(int argc, char *argv[]) {
    wf::Config cfg;
    std::string band;
    double duration;
    po::options_description desc("Options");
    desc.add_options()
        ("help,h", "show help")
        ("band", po::value<std::string>(&band)->default_value("all"), "2.4, 5, all, or a channel list like 1,6,11,36")
        ("focus", po::value<int>(&cfg.focus_n)->default_value(3), "number of active channels to lock onto")
        ("dwell", po::value<double>(&cfg.focus_dwell)->default_value(0.25), "seconds per visit to a focus channel")
        ("survey-dwell", po::value<double>(&cfg.survey_dwell)->default_value(0.11), "seconds per visit to other channels")
        ("survey-share", po::value<double>(&cfg.survey_share)->default_value(0.35), "fraction of time spent sweeping non-focus channels (0-0.95)")
        ("gain,g", po::value<double>(&cfg.gain)->default_value(40.0), "rx gain (dB)")
        ("antenna,a", po::value<std::string>(&cfg.antenna)->default_value("TX/RX"), "rx antenna")
        ("args", po::value<std::string>(&cfg.args)->default_value("type=b200"), "UHD device args")
        ("workers", po::value<int>(&cfg.workers)->default_value(3), "decoder threads")
        ("duration,d", po::value<double>(&duration)->default_value(30.0), "seconds to run (0 = until Ctrl-C)")
        ("file", po::value<std::string>(&cfg.file), "replay a 20 MS/s cf32 recording of one channel")
        ("channel", po::value<int>(&cfg.file_channel)->default_value(1), "channel of --file");
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
    cfg.channels = wf::band_channels(band);

    signal(SIGINT, [](int) { g_stop = 1; });
    wf::Engine eng;
    auto t_start = std::chrono::steady_clock::now();
    eng.start(cfg);
    double last = -10;
    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
        if (el - last >= 2.0) {
            fprintf(stderr, "[%5.0f s] %s\n", el, eng.status().c_str());
            last = el;
        }
        if (eng.finished()) break;
        if (cfg.file.empty() && duration > 0 && el >= duration) break;
    }
    eng.stop();
    fprintf(stderr, "%s\n", eng.status().c_str());

    wf::Model &m = eng.model;
    std::lock_guard<std::mutex> l(m.mu);
    double listened = 0;
    for (auto &c : m.chans) {
        for (auto &iv : c.second.covered) listened += iv.second - iv.first;
    }
    printf("\nListened %.1f s total. %ld PPDUs: %ld decoded, %ld timing only (n/ac/ax, CCK, bad FCS)\n", listened,
           m.frames_decoded + m.frames_timing_only, m.frames_decoded, m.frames_timing_only);
    printf("Attributed to a device or network: %ld PPDUs, %.1f of %.1f ms airtime (%.0f%%)\n\n", m.frames_attributed,
           m.air_attributed * 1e3, m.air_total * 1e3, m.air_total > 0 ? 100 * m.air_attributed / m.air_total : 0.0);

    // per-channel listening time, for airtime percentages
    auto chan_time = [&](int ch) {
        double s = 0;
        auto it = m.chans.find(ch);
        if (it != m.chans.end()) {
            for (auto &iv : it->second.covered) s += iv.second - iv.first;
        }
        return s;
    };

    std::vector<int> order(m.nets.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
    std::vector<double> net_air(m.nets.size(), 0.0);
    for (size_t d = 0; d < m.devices.size(); d++) {
        int n = m.net_of((int)d);
        if (n >= 0) net_air[n] += m.devices[d].row.airtime;
    }
    for (size_t n = 0; n < m.nets.size(); n++) net_air[n] += m.nets[n].unknown.airtime;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return net_air[a] > net_air[b]; });

    printf("%-46s %12s %11s %7s   (%% of that channel's listening time)\n", "NETWORK / DEVICE", "PACKETS", "AIRTIME", "");
    for (int n : order) {
        const wf::Network &net = m.nets[n];
        std::vector<int> members;
        for (size_t d = 0; d < m.devices.size(); d++) {
            if (m.net_of((int)d) == n) members.push_back((int)d);
        }
        if (members.empty()) continue;
        std::sort(members.begin(), members.end(), [&](int a, int b) {
            if (m.devices[a].is_ap != m.devices[b].is_ap) return m.devices[a].is_ap;
            return m.devices[a].row.airtime > m.devices[b].row.airtime;
        });
        int ch = m.devices[members[0]].ch;
        std::string name = net.placeholder ? "(no beacon seen) " + net.key.substr(6)
                         : net.hidden ? "<hidden> " + net.key.substr(7) : net.ssid;
        printf("%s  [ch %d, %s]\n", name.c_str(), ch, net.security.c_str());
        double span = chan_time(ch);
        for (int d : members) {
            const wf::Device &dv = m.devices[d];
            std::string lab = std::string(dv.is_ap ? "AP     " : "client ") + dv.mac + (dv.random ? " (random)" : "") +
                              (dv.vendor.empty() ? "" : " " + dv.vendor.substr(0, 14));
            print_row("    ", lab, dv.row, span);
        }
        if (net.unknown.packets) print_row("    ", "unidentified client", net.unknown, span);
    }
    printf("\nNot associated (probing / unknown role):\n");
    int shown = 0;
    for (size_t d = 0; d < m.devices.size(); d++) {
        const wf::Device &dv = m.devices[d];
        if (m.net_of((int)d) >= 0 || dv.row.packets == 0) continue;
        if (++shown > 25) continue;
        print_row("    ", dv.mac + (dv.random ? " (random)" : "") + " ch " + std::to_string(dv.ch), dv.row, chan_time(dv.ch));
    }
    if (shown > 25) printf("    ... and %d more\n", shown - 25);
    printf("\nUnattributed (sender unknown), by channel:\n");
    for (auto &c : m.chans) {
        if (c.second.unattributed.packets) print_row("    ", "ch " + std::to_string(c.first), c.second.unattributed, chan_time(c.first));
    }
    return 0;
}
