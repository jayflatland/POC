#pragma once

// A captured packet with Wireshark-style columns and a dissection tree whose
// nodes point at byte ranges of the payload.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <list>
#include <string>
#include <vector>

namespace shark {

struct Field {
    std::string label;
    int offset = -1;  // byte range in Packet::data, -1 = none
    int length = 0;
    std::list<Field> children;  // a list, so references from add() stay valid as siblings are added

    Field &add(const std::string &l, int off = -1, int len = 0) {
        children.push_back(Field{l, off, len, {}});
        return children.back();
    }
};

enum class Link { Wifi, Ble, ModeS };

struct Packet {
    long number = 0;
    double time = 0.0;       // seconds since capture start
    double wall = 0.0;       // unix time
    std::string channel;     // "ch 6", "BLE 37", "1090"
    double freq = 0.0;       // Hz
    float rssi = 0.0f;       // dBFS
    std::string protocol, source, destination, info;
    std::vector<uint8_t> data;
    Field tree;              // top-level layers are tree.children
    Link link = Link::Wifi;
    int ble_channel = 0;
    bool bad_crc = false;
};

inline std::string hex(uint64_t v, int digits) {
    char b[24];
    snprintf(b, sizeof(b), "%0*llx", digits, (unsigned long long)v);
    return b;
}

inline std::string mac(const uint8_t *m) {
    char b[18];
    snprintf(b, sizeof(b), "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    return b;
}

// BLE addresses are sent little-endian
inline std::string mac_rev(const uint8_t *m) {
    char b[18];
    snprintf(b, sizeof(b), "%02x:%02x:%02x:%02x:%02x:%02x", m[5], m[4], m[3], m[2], m[1], m[0]);
    return b;
}

inline std::string printable(const uint8_t *p, int n) {
    std::string s;
    for (int i = 0; i < n; i++) {
        s += (p[i] >= 32 && p[i] < 127) ? (char)p[i] : '.';
    }
    return s;
}

inline std::string fmt(const char *f, ...) __attribute__((format(printf, 1, 2)));
inline std::string fmt(const char *f, ...) {
    char b[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(b, sizeof(b), f, ap);
    va_end(ap);
    return b;
}

}  // namespace shark
