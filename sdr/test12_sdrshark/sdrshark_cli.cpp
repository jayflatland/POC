// Command-line front end for testing the capture engine: one line per packet
// (like tshark), optional full dissection, optional pcapng output.

#include <csignal>
#include <iostream>
#include <mutex>

#include <boost/program_options.hpp>

#include "capture.hpp"
#include "pcapng.hpp"

namespace po = boost::program_options;
static std::atomic<bool> g_stop{false};

static void print_tree(const shark::Field &f, int depth) {
    for (auto &c : f.children) {
        printf("%*s%s\n", depth * 4, "", c.label.c_str());
        print_tree(c, depth + 1);
    }
}

int main(int argc, char **argv) {
    std::string mode, band, file, pcap, antenna;
    double rate, freq, gain, duration;
    bool verbose = false;
    po::options_description desc("Options");
    desc.add_options()
        ("help,h", "show help")
        ("mode,m", po::value<std::string>(&mode)->default_value("wifi"), "wifi, ble, or adsb")
        ("band", po::value<std::string>(&band)->default_value("2.4"), "wifi: 2.4, 5, all, or a channel list like 1,6,11")
        ("gain,g", po::value<double>(&gain)->default_value(40.0), "rx gain (dB)")
        ("antenna,a", po::value<std::string>(&antenna)->default_value("TX/RX"), "antenna")
        ("duration,d", po::value<double>(&duration)->default_value(0.0), "seconds to run (0 = until Ctrl-C)")
        ("pcap,w", po::value<std::string>(&pcap)->default_value(""), "write a pcapng file")
        ("verbose,V", po::bool_switch(&verbose), "print each packet's protocol tree")
        ("file", po::value<std::string>(&file)->default_value(""), "replay a cf32 recording")
        ("rate", po::value<double>(&rate)->default_value(0.0), "sample rate of --file")
        ("freq", po::value<double>(&freq)->default_value(0.0), "center frequency of --file");
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 0;
    }
    signal(SIGINT, [](int) { g_stop = true; });

    shark::CaptureConfig cfg;
    cfg.mode = mode == "ble" ? shark::Mode::Ble : mode == "adsb" ? shark::Mode::Adsb : shark::Mode::Wifi;
    cfg.gain = gain;
    cfg.antenna = antenna;
    if (band.find(',') != std::string::npos || (band != "2.4" && band != "5" && band != "all")) {
        std::stringstream ss(band);
        std::string tok;
        while (std::getline(ss, tok, ',')) cfg.wifi_channels.push_back(std::stoi(tok));
        cfg.wifi_channels.erase(cfg.wifi_channels.begin(), cfg.wifi_channels.begin() + 3);  // drop the defaults
    } else {
        cfg.wifi_channels = shark::wifi_channels_for(band);
    }
    cfg.file = file;
    cfg.file_rate = rate;
    cfg.file_freq = freq;

    shark::PcapngWriter writer;
    if (!pcap.empty()) writer.open(pcap);
    std::mutex m;
    long n = 0;
    shark::Capture cap;
    cap.start(cfg, [&](shark::Packet &&p) {
        std::lock_guard<std::mutex> l(m);
        p.number = ++n;
        printf("%5ld %9.4f %-8s %6.1f %-7s %-17s %-17s %4zu  %s\n", p.number, p.time, p.channel.c_str(), p.rssi,
               p.protocol.c_str(), p.source.c_str(), p.destination.c_str(), p.data.size(), p.info.c_str());
        if (verbose) print_tree(p.tree, 1);
        fflush(stdout);
        writer.write(p);
    });
    auto t0 = std::chrono::steady_clock::now();
    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (duration > 0 && el >= duration) break;
        if (!file.empty() && !cap.running()) break;
    }
    cap.stop();
    fprintf(stderr, "%s\n", cap.status().c_str());
    return 0;
}
