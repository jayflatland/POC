/*
Scan WiFi channels with the B200 and list access points (SSIDs) from their beacons.

Hops across 2.4 GHz and 5 GHz channels, captures a short burst of 20 MS/s IQ
on each, and decodes 802.11 frames in software:
    - OFDM 802.11a/g, 6-54 Mbps (all 5 GHz beacons, most 2.4 GHz ones)
    - DSSS 802.11b, 1-2 Mbps (older 2.4 GHz beacons)
Only frames with a valid FCS (CRC-32) are used.

    ./build/wifi_scan                       # 2.4 + 5 GHz, one pass
    ./build/wifi_scan --band 2.4 --passes 3
    ./build/wifi_scan --channels 1,6,11 --dwell 0.5 -v
    ./build/wifi_scan --dump caps/          # also save each channel's IQ
    ./build/wifi_scan --file caps/ch006.cfile --channels 6
*/

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <boost/program_options.hpp>
#include <uhd/usrp/multi_usrp.hpp>

#include "dsss_rx.hpp"
#include "mac80211.hpp"
#include "ofdm_rx.hpp"

namespace po = boost::program_options;

static const double FS = 20e6;

struct Network {
    BeaconInfo info;
    int seen_channel = 0;
    std::string phy;
    float best_power = -200.0f;
    int beacons = 0;
};

static std::map<std::string, Network> networks;

static double channel_freq(int ch) {
    if (ch >= 1 && ch <= 13) {
        return (2407.0 + 5.0 * ch) * 1e6;
    }
    if (ch == 14) {
        return 2484e6;
    }
    return (5000.0 + 5.0 * ch) * 1e6;
}

static std::vector<int> parse_channels(const std::string &band, const std::string &list) {
    std::vector<int> chans;
    if (!list.empty()) {
        std::stringstream ss(list);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            chans.push_back(std::stoi(tok));
        }
        return chans;
    }
    if (band == "2.4" || band == "all") {
        for (int c = 1; c <= 11; c++) {
            chans.push_back(c);
        }
    }
    if (band == "5" || band == "all") {
        for (int c : {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
                      149, 153, 157, 161, 165}) {
            chans.push_back(c);
        }
    }
    return chans;
}

static int channel_of(const Network &n) {
    return n.info.channel > 0 ? n.info.channel : n.seen_channel;
}

static void print_network_line(const Network &n) {
    std::string ssid = n.info.hidden ? "<hidden>" : n.info.ssid;
    printf("  %3d  %6.1f dBFS  %-10s  %-12s  %s  %3d  %s\n", channel_of(n), n.best_power, n.phy.c_str(),
           n.info.security.c_str(), n.info.bssid.c_str(), n.beacons, ssid.c_str());
}

// decode one channel's capture; returns number of good frames
static int process_capture(const std::vector<cf> &x, int ch, bool is_24ghz, bool verbose) {
    static OfdmRx ofdm;
    static DsssRx dsss;
    RxStats stats;
    int frames = 0, beacons = 0, fcs_bad = 0;

    auto on_frame = [&](const RxFrame &f) {
        if (!fcs_ok(f.psdu)) {
            fcs_bad++;
            return;
        }
        frames++;
        BeaconInfo info;
        if (!parse_beacon(f.psdu, info)) {
            return;
        }
        beacons++;
        auto it = networks.find(info.bssid);
        bool is_new = it == networks.end();
        Network &n = networks[info.bssid];
        if (is_new || (n.info.hidden && !info.hidden)) {
            n.info = info;
        }
        if (f.power_db > n.best_power) {
            n.best_power = f.power_db;
            n.seen_channel = ch;
            n.phy = f.phy;
        }
        n.beacons++;
        if (is_new) {
            printf("  [new]");
            print_network_line(n);
        }
    };

    ofdm.process(x, FS, on_frame, stats);
    if (is_24ghz) {
        dsss.process(x, FS, on_frame, stats);
    }

    size_t clipped = 0;
    for (auto &v : x) {
        if (std::abs(v.real()) > 0.99f || std::abs(v.imag()) > 0.99f) {
            clipped++;
        }
    }
    printf("ch %3d  %7.1f MHz  %6.1f dBFS  frames %3d  beacons %3d", ch, channel_freq(ch) / 1e6,
           power_dbfs(x, 0, x.size()), frames, beacons);
    if (clipped > x.size() / 10000) {
        printf("  CLIPPING %.2f%% (lower --gain)", 100.0 * clipped / x.size());
    }
    if (verbose) {
        printf("\n        ofdm detect %ld signal_ok %ld | dsss lock %ld sfd %ld hdr_ok %ld hdr_bad %ld | fcs_bad %d",
               stats.ofdm_detect, stats.ofdm_signal_ok, stats.dsss_lock, stats.dsss_sfd, stats.dsss_header_ok,
               stats.dsss_header_bad, fcs_bad);
    }
    printf("\n");
    fflush(stdout);
    return frames;
}

int main(int argc, char *argv[]) {
    std::string args, antenna, band, chan_list, dump_dir, file;
    double gain, dwell;
    int passes;

    po::options_description desc("Options");
    desc.add_options()
        ("help,h", "show help")
        ("band", po::value<std::string>(&band)->default_value("all"), "2.4, 5, or all")
        ("channels,c", po::value<std::string>(&chan_list)->default_value(""), "comma-separated channel list (overrides --band)")
        ("dwell,d", po::value<double>(&dwell)->default_value(0.3), "seconds per channel (beacons are ~every 0.1 s)")
        ("passes,p", po::value<int>(&passes)->default_value(1), "number of passes over the channel list")
        ("gain,g", po::value<double>(&gain)->default_value(40.0), "rx gain (dB)")
        ("antenna,a", po::value<std::string>(&antenna)->default_value("TX/RX"), "rx antenna (TX/RX or RX2)")
        ("args", po::value<std::string>(&args)->default_value("type=b200"), "UHD device args")
        ("dump", po::value<std::string>(&dump_dir)->default_value(""), "save each channel's IQ (fc32) into this directory")
        ("file", po::value<std::string>(&file)->default_value(""), "decode a saved 20 MS/s fc32 capture instead (use with --channels)")
        ("verbose,v", "print decoder statistics per channel");
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
    bool verbose = vm.count("verbose") > 0;
    auto channels = parse_channels(band, chan_list);
    if (channels.empty()) {
        std::cerr << "No channels to scan" << std::endl;
        return 1;
    }

    if (!file.empty()) {
        std::ifstream in(file, std::ios::binary | std::ios::ate);
        if (!in) {
            std::cerr << "Couldn't open " << file << std::endl;
            return 1;
        }
        std::vector<cf> x(in.tellg() / sizeof(cf));
        in.seekg(0);
        in.read((char *)x.data(), x.size() * sizeof(cf));
        process_capture(x, channels[0], channels[0] <= 14, verbose);
    } else {
        auto usrp = uhd::usrp::multi_usrp::make(args);
        usrp->set_rx_rate(FS, 0);
        usrp->set_rx_gain(gain, 0);
        usrp->set_rx_antenna(antenna, 0);
        usrp->set_rx_bandwidth(FS, 0);
        printf("B200: %.1f MS/s, gain %.0f dB, antenna %s, %.2f s per channel\n\n", usrp->get_rx_rate(0) / 1e6,
               usrp->get_rx_gain(0), usrp->get_rx_antenna(0).c_str(), dwell);

        uhd::stream_args_t stream_args("fc32", "sc16");
        stream_args.channels = {0};
        auto rx_stream = usrp->get_rx_stream(stream_args);

        const size_t settle = (size_t)(0.005 * FS);
        const size_t want = (size_t)(dwell * FS);
        std::vector<cf> buf(settle + want);
        uhd::rx_metadata_t md;

        for (int pass = 0; pass < passes; pass++) {
            if (passes > 1) {
                printf("--- pass %d of %d ---\n", pass + 1, passes);
            }
            for (int ch : channels) {
                usrp->set_rx_freq(uhd::tune_request_t(channel_freq(ch)), 0);
                for (int i = 0; i < 100; i++) {
                    if (usrp->get_rx_sensor("lo_locked", 0).to_bool()) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }

                uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
                cmd.num_samps = buf.size();
                cmd.stream_now = true;
                rx_stream->issue_stream_cmd(cmd);

                size_t got = 0;
                int overflows = 0;
                while (got < buf.size()) {
                    size_t n = rx_stream->recv(&buf[got], buf.size() - got, md, 1.0);
                    if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
                        break;
                    }
                    if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
                        overflows++;
                        continue;
                    }
                    got += n;
                    if (md.end_of_burst) {
                        break;
                    }
                }
                if (got <= settle) {
                    printf("ch %3d  no samples received\n", ch);
                    continue;
                }
                std::vector<cf> x(buf.begin() + settle, buf.begin() + got);

                if (!dump_dir.empty()) {
                    char name[64];
                    snprintf(name, sizeof(name), "/ch%03d.cfile", ch);
                    std::ofstream out(dump_dir + name, std::ios::binary);
                    out.write((const char *)x.data(), x.size() * sizeof(cf));
                }
                if (overflows) {
                    printf("        (%d overflows)\n", overflows);
                }
                process_capture(x, ch, ch <= 14, verbose);
            }
        }
    }

    printf("\n%zu network(s) found\n", networks.size());
    if (networks.empty()) {
        return 0;
    }
    printf("\n   CH     POWER      PHY         SECURITY      BSSID              BCN  SSID\n");
    std::vector<const Network *> sorted;
    for (auto &kv : networks) {
        sorted.push_back(&kv.second);
    }
    std::sort(sorted.begin(), sorted.end(), [](const Network *a, const Network *b) {
        if (channel_of(*a) != channel_of(*b)) {
            return channel_of(*a) < channel_of(*b);
        }
        return a->best_power > b->best_power;
    });
    for (auto *n : sorted) {
        print_network_line(*n);
    }
    return 0;
}
