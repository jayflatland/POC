#pragma once

// 802.11 MAC: FCS check and beacon / probe response parsing.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

inline uint32_t crc32_ieee(const uint8_t *d, size_t n) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (0u - (crc & 1)));
        }
    }
    return ~crc;
}

inline bool fcs_ok(const std::vector<uint8_t> &f) {
    if (f.size() < 14) {
        return false;
    }
    size_t n = f.size() - 4;
    uint32_t rx = f[n] | (f[n + 1] << 8) | (f[n + 2] << 16) | ((uint32_t)f[n + 3] << 24);
    return crc32_ieee(f.data(), n) == rx;
}

struct BeaconInfo {
    std::string bssid;
    std::string ssid;
    bool hidden = false;
    int channel = -1;  // from the DS Parameter Set / HT Operation element
    std::string security;
    bool probe_response = false;
};

inline std::string mac_to_string(const uint8_t *m) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    return buf;
}

// AKM suite types from the RSN element
inline std::string rsn_security(const uint8_t *ie, int len) {
    // version(2) group cipher(4) pairwise count(2) + 4n, AKM count(2) + 4m
    if (len < 8) {
        return "WPA2";
    }
    int off = 6;
    int npair = ie[off] | (ie[off + 1] << 8);
    off += 2 + 4 * npair;
    if (off + 2 > len) {
        return "WPA2";
    }
    int nakm = ie[off] | (ie[off + 1] << 8);
    off += 2;
    bool psk = false, sae = false, ent = false, owe = false;
    for (int i = 0; i < nakm && off + 4 <= len; i++, off += 4) {
        int type = ie[off + 3];
        if (type == 2 || type == 6) psk = true;
        else if (type == 8 || type == 24) sae = true;
        else if (type == 1 || type == 5 || type == 11 || type == 12) ent = true;
        else if (type == 18) owe = true;
    }
    if (sae && psk) return "WPA2/WPA3";
    if (sae) return "WPA3";
    if (owe) return "OWE";
    if (ent) return "WPA2-Ent";
    return "WPA2";
}

// returns true for a beacon or probe response with a good FCS
inline bool parse_beacon(const std::vector<uint8_t> &f, BeaconInfo &out) {
    if (f.size() < 24 + 12 + 4) {
        return false;
    }
    int type = (f[0] >> 2) & 3;
    int subtype = f[0] >> 4;
    if (type != 0 || (subtype != 8 && subtype != 5)) {
        return false;
    }
    out.probe_response = subtype == 5;
    out.bssid = mac_to_string(&f[16]);
    uint16_t cap = f[34] | (f[35] << 8);

    bool rsn = false, wpa = false;
    std::string rsn_sec;
    size_t off = 36;
    size_t end = f.size() - 4;
    bool have_ssid = false;
    while (off + 2 <= end) {
        int id = f[off];
        int len = f[off + 1];
        if (off + 2 + len > end) {
            break;
        }
        const uint8_t *ie = &f[off + 2];
        if (id == 0 && !have_ssid) {
            have_ssid = true;
            bool all_zero = true;
            for (int i = 0; i < len; i++) {
                all_zero &= ie[i] == 0;
                char c = (char)ie[i];
                if (c >= 32 && c < 127) {
                    out.ssid += c;
                } else {
                    char esc[5];
                    snprintf(esc, sizeof(esc), "\\x%02x", ie[i]);
                    out.ssid += esc;
                }
            }
            out.hidden = len == 0 || all_zero;
        } else if (id == 3 && len >= 1) {
            out.channel = ie[0];
        } else if (id == 61 && len >= 1 && out.channel < 0) {
            out.channel = ie[0];
        } else if (id == 48) {
            rsn = true;
            rsn_sec = rsn_security(ie, len);
        } else if (id == 221 && len >= 4 && ie[0] == 0x00 && ie[1] == 0x50 && ie[2] == 0xF2 && ie[3] == 1) {
            wpa = true;
        }
        off += 2 + len;
    }
    if (rsn) {
        out.security = wpa ? "WPA/" + rsn_sec : rsn_sec;
    } else if (wpa) {
        out.security = "WPA";
    } else if (cap & 0x10) {
        out.security = "WEP";
    } else {
        out.security = "open";
    }
    return true;
}
