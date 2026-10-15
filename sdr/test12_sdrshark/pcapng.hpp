#pragma once

// pcapng writer: one interface per link type, so a mixed capture opens in Wireshark.
//   802.11 frames  -> LINKTYPE_IEEE802_11_RADIOTAP (127), FCS included
//   BLE packets    -> LINKTYPE_BLUETOOTH_LE_LL_WITH_PHDR (256)
//   Mode S / ADS-B -> LINKTYPE_USER0 (147), raw message bytes

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "packet.hpp"

namespace shark {

class PcapngWriter {
public:
    bool open(const std::string &path) {
        f_ = fopen(path.c_str(), "wb");
        if (!f_) return false;
        // Section Header Block
        std::vector<uint8_t> b;
        put32(b, 0x1A2B3C4D);
        put16(b, 1);
        put16(b, 0);
        put64(b, 0xFFFFFFFFFFFFFFFFull);
        block(0x0A0D0D0A, b);
        // Interface Description Blocks: 0 = radiotap, 1 = BLE, 2 = user0
        for (int lt : {127, 256, 147}) {
            std::vector<uint8_t> i;
            put16(i, lt);
            put16(i, 0);
            put32(i, 65535);
            // if_tsresol = 6 (microseconds) is the default
            block(0x00000001, i);
        }
        return true;
    }

    void write(const Packet &p) {
        if (!f_) return;
        std::vector<uint8_t> pkt;
        int iface = 0;
        if (p.link == Link::Wifi) {
            // radiotap: flags (FCS at end), channel, dBm antenna signal
            uint16_t freq_mhz = (uint16_t)(p.freq / 1e6 + 0.5);
            uint16_t chflags = (p.freq < 3e9 ? 0x0080 : 0x0100) | 0x0040;
            uint8_t rt[15] = {0, 0, 15, 0, 0x2A, 0, 0, 0, 0x10, 0, 0, 0, 0, 0, 0};
            std::memcpy(rt + 10, &freq_mhz, 2);
            std::memcpy(rt + 12, &chflags, 2);
            rt[14] = (uint8_t)(int8_t)std::max(-127.0f, std::min(0.0f, p.rssi));
            pkt.assign(rt, rt + 15);
        } else if (p.link == Link::Ble) {
            iface = 1;
            int rf = p.ble_channel == 37 ? 0 : p.ble_channel == 38 ? 12 : p.ble_channel == 39 ? 39 : 0;
            uint8_t ph[10] = {(uint8_t)rf, (uint8_t)(int8_t)std::max(-127.0f, std::min(0.0f, p.rssi)), 0x80, 0, 0xD6, 0xBE, 0x89, 0x8E};
            uint16_t flags = 0x0001 | 0x0002 | 0x0010 | 0x0400 | 0x0800;  // dewhitened, signal, ref AA, CRC checked + valid
            std::memcpy(ph + 8, &flags, 2);
            pkt.assign(ph, ph + 10);
        } else {
            iface = 2;
        }
        pkt.insert(pkt.end(), p.data.begin(), p.data.end());

        std::vector<uint8_t> b;
        uint64_t us = (uint64_t)(p.wall * 1e6);
        put32(b, iface);
        put32(b, (uint32_t)(us >> 32));
        put32(b, (uint32_t)us);
        put32(b, (uint32_t)pkt.size());
        put32(b, (uint32_t)pkt.size());
        b.insert(b.end(), pkt.begin(), pkt.end());
        while (b.size() % 4) b.push_back(0);
        block(0x00000006, b);
    }

    void close() {
        if (f_) fclose(f_);
        f_ = nullptr;
    }
    ~PcapngWriter() { close(); }

private:
    static void put16(std::vector<uint8_t> &b, uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); }
    static void put32(std::vector<uint8_t> &b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 0xFF); }
    static void put64(std::vector<uint8_t> &b, uint64_t v) { for (int i = 0; i < 8; i++) b.push_back((v >> (8 * i)) & 0xFF); }
    void block(uint32_t type, const std::vector<uint8_t> &body) {
        uint32_t len = 12 + (uint32_t)body.size();
        fwrite(&type, 4, 1, f_);
        fwrite(&len, 4, 1, f_);
        fwrite(body.data(), 1, body.size(), f_);
        fwrite(&len, 4, 1, f_);
    }
    FILE *f_ = nullptr;
};

}  // namespace shark
