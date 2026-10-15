/*
Scan WiFi channels with the B200 and hex-dump every decoded 802.11 frame.

Uses the OFDM / DSSS / MAC decoders from ../test07_wifi_ssid. Only frames
with a valid FCS are printed unless --bad is given.

    ./build/wifi_dump                        # all 2.4 + 5 GHz channels, forever
    ./build/wifi_dump --band 2.4 --passes 1  # one pass over 2.4 GHz
    ./build/wifi_dump --channels 6 --dwell 2
    ./build/wifi_dump --file caps/ch006.cfile --channels 6
*/

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
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

static const char *frame_type(const std::vector<uint8_t> &f) {
    if (f.size() < 2) {
        return "?";
    }
    int type = (f[0] >> 2) & 3;
    int sub = f[0] >> 4;
    static const char *mgmt[16] = {"AssocReq", "AssocResp", "ReassocReq", "ReassocResp", "ProbeReq", "ProbeResp",
                                   "TimingAdv", "Mgmt7", "Beacon", "ATIM", "Disassoc", "Auth", "Deauth", "Action",
                                   "ActionNoAck", "Mgmt15"};
    static const char *ctrl[16] = {"Ctrl0", "Ctrl1", "Trigger", "TACK", "BeamRpt", "NDPA", "CtrlExt", "CtrlWrapper",
                                   "BlockAckReq", "BlockAck", "PS-Poll", "RTS", "CTS", "ACK", "CF-End", "CF-End+Ack"};
    static const char *data[16] = {"Data", "Data+CF-Ack", "Data+CF-Poll", "Data+CF-Ack+Poll", "Null", "CF-Ack",
                                   "CF-Poll", "CF-Ack+Poll", "QoSData", "QoSData+CF-Ack", "QoSData+CF-Poll",
                                   "QoSData+CF-Ack+Poll", "QoSNull", "Data13", "QoSCF-Poll", "QoSCF-Ack+Poll"};
    if (type == 0) return mgmt[sub];
    if (type == 1) return ctrl[sub];
    if (type == 2) return data[sub];
    return "Extension";
}

static void hexdump(const std::vector<uint8_t> &d) {
    for (size_t off = 0; off < d.size(); off += 16) {
        printf("  %04zx  ", off);
        for (size_t i = 0; i < 16; i++) {
            if (off + i < d.size()) {
                printf("%02x ", d[off + i]);
            } else {
                printf("   ");
            }
            if (i == 7) {
                printf(" ");
            }
        }
        printf(" |");
        for (size_t i = 0; i < 16 && off + i < d.size(); i++) {
            uint8_t c = d[off + i];
            putchar(c >= 32 && c < 127 ? c : '.');
        }
        printf("|\n");
    }
}

static long total_frames = 0;

static void process_capture(const std::vector<cf> &x, int ch, bool show_bad) {
    static OfdmRx ofdm;
    static DsssRx dsss;
    RxStats stats;

    auto on_frame = [&](const RxFrame &f) {
        bool good = fcs_ok(f.psdu);
        if (!good && !show_bad) {
            return;
        }
        total_frames++;
        printf("ch %d  %s  %.1f dBFS  %zu bytes  %s%s\n", ch, f.phy.c_str(), f.power_db, f.psdu.size(),
               frame_type(f.psdu), good ? "" : "  BAD FCS");
        hexdump(f.psdu);
        printf("\n");
    };

    ofdm.process(x, FS, on_frame, stats);
    if (ch <= 14) {
        dsss.process(x, FS, on_frame, stats);
    }
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    std::string args, antenna, band, chan_list, file;
    double gain, dwell;
    int passes;

    po::options_description desc("Options");
    desc.add_options()
        ("help,h", "show help")
        ("band", po::value<std::string>(&band)->default_value("all"), "2.4, 5, or all")
        ("channels,c", po::value<std::string>(&chan_list)->default_value(""), "comma-separated channel list (overrides --band)")
        ("dwell,d", po::value<double>(&dwell)->default_value(0.3), "seconds per channel")
        ("passes,p", po::value<int>(&passes)->default_value(0), "passes over the channel list (0 = forever)")
        ("gain,g", po::value<double>(&gain)->default_value(40.0), "rx gain (dB)")
        ("antenna,a", po::value<std::string>(&antenna)->default_value("TX/RX"), "rx antenna (TX/RX or RX2)")
        ("args", po::value<std::string>(&args)->default_value("type=b200"), "UHD device args")
        ("bad", "also dump frames that fail the FCS check")
        ("file", po::value<std::string>(&file)->default_value(""), "decode a saved 20 MS/s fc32 capture instead (use with --channels)");
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
    bool show_bad = vm.count("bad") > 0;
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
        process_capture(x, channels[0], show_bad);
        printf("%ld frame(s)\n", total_frames);
        return 0;
    }

    auto usrp = uhd::usrp::multi_usrp::make(args);
    usrp->set_rx_rate(FS, 0);
    usrp->set_rx_gain(gain, 0);
    usrp->set_rx_antenna(antenna, 0);
    usrp->set_rx_bandwidth(FS, 0);
    fprintf(stderr, "B200: %.1f MS/s, gain %.0f dB, antenna %s, %.2f s per channel\n", usrp->get_rx_rate(0) / 1e6,
            usrp->get_rx_gain(0), usrp->get_rx_antenna(0).c_str(), dwell);

    uhd::stream_args_t stream_args("fc32", "sc16");
    stream_args.channels = {0};
    auto rx_stream = usrp->get_rx_stream(stream_args);

    const size_t settle = (size_t)(0.005 * FS);
    std::vector<cf> buf(settle + (size_t)(dwell * FS));
    uhd::rx_metadata_t md;

    for (int pass = 0; passes == 0 || pass < passes; pass++) {
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
            while (got < buf.size()) {
                size_t n = rx_stream->recv(&buf[got], buf.size() - got, md, 1.0);
                if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
                    break;
                }
                if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
                    continue;
                }
                got += n;
                if (md.end_of_burst) {
                    break;
                }
            }
            if (got <= settle) {
                fprintf(stderr, "ch %d: no samples received\n", ch);
                continue;
            }
            std::vector<cf> x(buf.begin() + settle, buf.begin() + got);
            process_capture(x, ch, show_bad);
        }
    }
    fprintf(stderr, "%ld frame(s)\n", total_frames);
    return 0;
}
