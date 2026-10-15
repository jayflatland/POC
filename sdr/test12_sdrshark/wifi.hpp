#pragma once

// IEEE 802.11 dissector. Frames come from test07's OFDM / DSSS decoders.

#include <string>

#include "packet.hpp"

namespace shark {

inline const char *wifi_subtype_name(int type, int sub) {
    static const char *mgmt[16] = {"Association Request", "Association Response", "Reassociation Request",
                                   "Reassociation Response", "Probe Request", "Probe Response", "Timing Advertisement",
                                   "Reserved", "Beacon frame", "ATIM", "Disassociate", "Authentication",
                                   "Deauthentication", "Action", "Action No Ack", "Reserved"};
    static const char *ctrl[16] = {"Reserved", "Reserved", "Trigger", "TACK", "Beamforming Report Poll", "VHT/HE NDP Announcement",
                                   "Control Frame Extension", "Control Wrapper", "802.11 Block Ack Req", "802.11 Block Ack",
                                   "Power-Save poll", "Request-to-send", "Clear-to-send", "Acknowledgement", "CF-End",
                                   "CF-End + CF-Ack"};
    static const char *data[16] = {"Data", "Data + CF-Ack", "Data + CF-Poll", "Data + CF-Ack + CF-Poll", "Null function (No data)",
                                   "CF-Ack (No data)", "CF-Poll (No data)", "CF-Ack + CF-Poll (No data)", "QoS Data",
                                   "QoS Data + CF-Ack", "QoS Data + CF-Poll", "QoS Data + CF-Ack + CF-Poll", "QoS Null function (No data)",
                                   "Reserved", "QoS CF-Poll (No data)", "QoS CF-Ack + CF-Poll (No data)"};
    if (type == 0) return mgmt[sub];
    if (type == 1) return ctrl[sub];
    if (type == 2) return data[sub];
    return "Extension";
}

inline const char *ie_name(int id) {
    switch (id) {
    case 0: return "SSID";
    case 1: return "Supported Rates";
    case 3: return "DS Parameter set";
    case 5: return "Traffic Indication Map (TIM)";
    case 7: return "Country Information";
    case 11: return "QBSS Load Element";
    case 32: return "Power Constraint";
    case 35: return "TPC Report";
    case 42: return "ERP Information";
    case 45: return "HT Capabilities";
    case 48: return "RSN Information";
    case 50: return "Extended Supported Rates";
    case 54: return "Mobility Domain";
    case 59: return "Supported Operating Classes";
    case 61: return "HT Information";
    case 70: return "RM Enabled Capabilities";
    case 74: return "Overlapping BSS Scan Parameters";
    case 107: return "Interworking";
    case 127: return "Extended Capabilities";
    case 191: return "VHT Capabilities";
    case 192: return "VHT Operation";
    case 195: return "VHT Tx Power Envelope";
    case 221: return "Vendor Specific";
    case 255: return "Extension";
    default: return nullptr;
    }
}

inline const char *vendor_name(const uint8_t *oui) {
    uint32_t o = (oui[0] << 16) | (oui[1] << 8) | oui[2];
    switch (o) {
    case 0x0050F2: return "Microsoft Corp.";
    case 0x506F9A: return "Wi-Fi Alliance";
    case 0x001018: return "Broadcom";
    case 0x0017F2: return "Apple, Inc.";
    case 0x00037F: return "Atheros Communications";
    case 0x8CFDF0: return "Qualcomm";
    case 0x00904C: return "Epigram";
    case 0x000C43: return "Ralink Technology";
    default: return nullptr;
    }
}

inline std::string rsn_akms(const uint8_t *ie, int len) {
    if (len < 8) return "";
    int off = 6;
    int npair = ie[off] | (ie[off + 1] << 8);
    off += 2 + 4 * npair;
    if (off + 2 > len) return "";
    int nakm = ie[off] | (ie[off + 1] << 8);
    off += 2;
    std::string s;
    for (int i = 0; i < nakm && off + 4 <= len; i++, off += 4) {
        int t = ie[off + 3];
        const char *n = t == 1 ? "802.1X" : t == 2 ? "PSK" : t == 8 ? "SAE" : t == 18 ? "OWE" : t == 6 ? "PSK-SHA256" : nullptr;
        s += (s.empty() ? "" : ", ") + (n ? std::string(n) : fmt("type %d", t));
    }
    return s;
}

// information elements from `start` to `end`
inline void dissect_ies(Packet &p, Field &parent, int start, int end, std::string *ssid_out) {
    const uint8_t *d = p.data.data();
    Field &tagged = parent.add("Tagged parameters", start, end - start);
    int i = start;
    while (i + 2 <= end) {
        int id = d[i], len = d[i + 1];
        if (i + 2 + len > end) break;
        const uint8_t *v = d + i + 2;
        const char *name = ie_name(id);
        Field &f = tagged.add(fmt("Tag: %s", name ? name : fmt("%d", id).c_str()), i, len + 2);
        f.add(fmt("Tag Number: %d", id), i, 1);
        f.add(fmt("Tag length: %d", len), i + 1, 1);
        if (id == 0) {
            bool hidden = len == 0;
            for (int k = 0; k < len; k++) hidden &= v[k] == 0;
            std::string ssid = hidden ? "<hidden>" : printable(v, len);
            f.label = "Tag: SSID parameter set: " + ssid;
            f.add("SSID: " + ssid, i + 2, len);
            if (ssid_out) *ssid_out = ssid;
        } else if (id == 1 || id == 50) {
            std::string r;
            for (int k = 0; k < len; k++) {
                int rate = v[k] & 0x7F;
                r += (r.empty() ? "" : ", ") + (rate % 2 ? fmt("%.1f", rate / 2.0) : fmt("%d", rate / 2)) + (v[k] & 0x80 ? "(B)" : "");
            }
            f.label += ": " + r + " [Mbit/sec]";
        } else if (id == 3 && len >= 1) {
            f.label += fmt(": Current Channel: %d", v[0]);
        } else if (id == 5 && len >= 4) {
            f.label += fmt(": DTIM %d of %d bitmap", v[0], v[1]);
        } else if (id == 7 && len >= 2) {
            f.label += ": " + printable(v, 2);
        } else if (id == 48) {
            std::string akm = rsn_akms(v, len);
            if (!akm.empty()) f.add("Auth Key Management (AKM) Suites: " + akm, i + 2, len);
        } else if (id == 221 && len >= 3) {
            const char *vn = vendor_name(v);
            f.label += vn ? fmt(": %s", vn) : fmt(": %02x:%02x:%02x", v[0], v[1], v[2]);
            if (len >= 4 && v[0] == 0x00 && v[1] == 0x50 && v[2] == 0xF2) {
                f.label += v[3] == 1 ? ": WPA Information Element" : v[3] == 2 ? ": WMM/WME" : v[3] == 4 ? ": WPS" : "";
            }
        } else if (id == 61 && len >= 1) {
            f.label += fmt(": Primary Channel: %d", v[0]);
        }
        i += 2 + len;
    }
}

inline void dissect_80211(Packet &p) {
    const uint8_t *d = p.data.data();
    int n = (int)p.data.size();
    p.protocol = "802.11";
    if (n < 10) {
        p.info = "Malformed";
        return;
    }
    int type = (d[0] >> 2) & 3, sub = d[0] >> 4;
    int flags = d[1];
    bool to_ds = flags & 1, from_ds = flags & 2, retry = flags & 8, prot = flags & 0x40, order = flags & 0x80;
    const char *name = wifi_subtype_name(type, sub);
    Field &w = p.tree.add(std::string("IEEE 802.11 ") + name, 0, n - 4);
    Field &fc = w.add(fmt("Frame Control Field: 0x%02x%02x", d[0], d[1]), 0, 2);
    fc.add(fmt("Type/Subtype: %s (0x%04x)", name, (type << 4) | sub), 0, 1);
    Field &fl = fc.add(fmt("Flags: 0x%02x", flags), 1, 1);
    fl.add(fmt("DS status: %s", to_ds && from_ds ? "WDS (To DS and From DS)" : to_ds ? "To DS" : from_ds ? "From DS" : "Not leaving DS or network is operating in AD-HOC mode"), 1, 1);
    if (flags & 4) fl.add("More Fragments: set", 1, 1);
    if (retry) fl.add("Retry: Frame is being retransmitted", 1, 1);
    if (flags & 0x10) fl.add("PWR MGT: STA will go to sleep", 1, 1);
    if (flags & 0x20) fl.add("More Data: set", 1, 1);
    if (prot) fl.add("Protected flag: Data is protected", 1, 1);
    if (order) fl.add("+HTC/Order flag: set", 1, 1);
    w.add(fmt("Duration/ID: %d", d[2] | (d[3] << 8)), 2, 2);

    std::string ra = n >= 10 ? mac(d + 4) : "", a2 = n >= 16 ? mac(d + 10) : "", a3 = n >= 22 ? mac(d + 16) : "";
    int hdr = 4;
    p.destination = ra;
    if (type == 1) {
        w.add("Receiver address: " + ra, 4, 6);
        hdr = 10;
        bool has_ta = sub == 8 || sub == 9 || sub == 10 || sub == 11 || sub == 4 || sub == 5 || sub == 2;
        if (has_ta && n >= 20) {
            w.add("Transmitter address: " + a2, 10, 6);
            p.source = a2;
            hdr = 16;
        }
        p.info = name;
        if (sub == 9 && n >= hdr + 4) p.info += fmt(", Flags=........%s", retry ? "R" : ".");
    } else {
        if (n < 24) {
            p.info = std::string(name) + " (truncated)";
            return;
        }
        if (type == 0) {
            w.add("Receiver address / Destination address: " + ra, 4, 6);
            w.add("Transmitter address / Source address: " + a2, 10, 6);
            w.add("BSS Id: " + a3, 16, 6);
        } else if (!to_ds && !from_ds) {
            w.add("Destination address: " + ra, 4, 6);
            w.add("Source address: " + a2, 10, 6);
            w.add("BSS Id: " + a3, 16, 6);
        } else if (to_ds && !from_ds) {
            w.add("BSS Id / Receiver address: " + ra, 4, 6);
            w.add("Source address / Transmitter address: " + a2, 10, 6);
            w.add("Destination address: " + a3, 16, 6);
        } else if (!to_ds && from_ds) {
            w.add("Destination address / Receiver address: " + ra, 4, 6);
            w.add("BSS Id / Transmitter address: " + a2, 10, 6);
            w.add("Source address: " + a3, 16, 6);
        } else {
            w.add("Receiver address: " + ra, 4, 6);
            w.add("Transmitter address: " + a2, 10, 6);
            w.add("Destination address: " + a3, 16, 6);
        }
        p.source = a2;
        int seq = (d[22] | (d[23] << 8));
        w.add(fmt("Fragment number: %d", seq & 0xF), 22, 2);
        w.add(fmt("Sequence number: %d", seq >> 4), 22, 2);
        hdr = 24;
        if (type == 2 && to_ds && from_ds && n >= 30) {
            w.add("Source address: " + mac(d + 24), 24, 6);
            hdr = 30;
        }
        if (type == 2 && (sub & 8) && n >= hdr + 2) {
            w.add(fmt("QoS Control: TID %d", d[hdr] & 0xF), hdr, 2);
            hdr += 2;
        }
        if (order && n >= hdr + 4) {
            w.add("HT Control", hdr, 4);
            hdr += 4;
        }
        p.info = fmt("%s, SN=%d, FN=%d, Flags=%s%s%s%s", name, seq >> 4, seq & 0xF, prot ? ".p" : "..", retry ? "..R" : "...",
                     from_ds ? ".F" : "..", to_ds ? "T" : ".");
    }
    int body_end = n - 4;

    if (type == 0 && hdr < body_end) {
        Field &mgt = p.tree.add("IEEE 802.11 Wireless Management", hdr, body_end - hdr);
        int ies = -1;
        if ((sub == 8 || sub == 5) && body_end - hdr >= 12) {
            Field &fixed = mgt.add("Fixed parameters (12 bytes)", hdr, 12);
            uint64_t ts = 0;
            for (int k = 7; k >= 0; k--) ts = (ts << 8) | d[hdr + k];
            fixed.add(fmt("Timestamp: %llu", (unsigned long long)ts), hdr, 8);
            int bi = d[hdr + 8] | (d[hdr + 9] << 8);
            fixed.add(fmt("Beacon Interval: %.6f [Seconds]", bi * 1024e-6), hdr + 8, 2);
            int cap = d[hdr + 10] | (d[hdr + 11] << 8);
            Field &c = fixed.add(fmt("Capabilities Information: 0x%04x", cap), hdr + 10, 2);
            if (cap & 1) c.add("ESS capabilities: Transmitter is an AP", hdr + 10, 2);
            if (cap & 0x10) c.add("Privacy: Data confidentiality required", hdr + 10, 2);
            if (cap & 0x20) c.add("Short Preamble: Allowed", hdr + 10, 2);
            ies = hdr + 12;
            p.info += fmt(", BI=%d", bi);
        } else if (sub == 4) {
            ies = hdr;
        } else if ((sub == 0 || sub == 2) && body_end - hdr >= 4) {
            mgt.add(fmt("Capabilities Information: 0x%04x", d[hdr] | (d[hdr + 1] << 8)), hdr, 2);
            mgt.add(fmt("Listen Interval: %d", d[hdr + 2] | (d[hdr + 3] << 8)), hdr + 2, 2);
            ies = hdr + 4 + (sub == 2 ? 6 : 0);
        } else if ((sub == 1 || sub == 3) && body_end - hdr >= 6) {
            mgt.add(fmt("Status code: %d", d[hdr + 2] | (d[hdr + 3] << 8)), hdr + 2, 2);
            mgt.add(fmt("Association ID: %d", (d[hdr + 4] | (d[hdr + 5] << 8)) & 0x3FFF), hdr + 4, 2);
            ies = hdr + 6;
        } else if (sub == 11 && body_end - hdr >= 6) {
            int alg = d[hdr] | (d[hdr + 1] << 8), sq = d[hdr + 2] | (d[hdr + 3] << 8), st = d[hdr + 4] | (d[hdr + 5] << 8);
            mgt.add(fmt("Authentication Algorithm: %s (%d)", alg == 0 ? "Open System" : alg == 3 ? "SAE" : "other", alg), hdr, 2);
            mgt.add(fmt("Authentication SEQ: %d", sq), hdr + 2, 2);
            mgt.add(fmt("Status code: %d", st), hdr + 4, 2);
        } else if ((sub == 10 || sub == 12) && body_end - hdr >= 2) {
            int r = d[hdr] | (d[hdr + 1] << 8);
            mgt.add(fmt("Reason code: %d", r), hdr, 2);
            p.info += fmt(", Reason %d", r);
        } else if ((sub == 13 || sub == 14) && body_end - hdr >= 2) {
            mgt.add(fmt("Category code: %d", d[hdr]), hdr, 1);
            mgt.add(fmt("Action code: %d", d[hdr + 1]), hdr + 1, 1);
        }
        if (ies >= 0 && ies < body_end) {
            std::string ssid;
            dissect_ies(p, mgt, ies, body_end, &ssid);
            if (!ssid.empty() || sub == 4) p.info += ", SSID=\"" + (ssid.empty() ? std::string("Wildcard") : ssid) + "\"";
        }
    } else if (type == 2 && hdr < body_end) {
        if (prot) {
            if (body_end - hdr >= 8) {
                const uint8_t *c = d + hdr;
                uint64_t pn = c[0] | (c[1] << 8) | ((uint64_t)c[4] << 16) | ((uint64_t)c[5] << 24) | ((uint64_t)c[6] << 32) | ((uint64_t)c[7] << 40);
                Field &cc = p.tree.add("CCMP parameters", hdr, 8);
                cc.add(fmt("Key ID: %d", c[3] >> 6), hdr + 3, 1);
                cc.add(fmt("PN: %llu", (unsigned long long)pn), hdr, 8);
                p.tree.add(fmt("Data (%d bytes, encrypted)", body_end - hdr - 8), hdr + 8, body_end - hdr - 8);
            }
        } else if (body_end - hdr >= 8 && d[hdr] == 0xAA && d[hdr + 1] == 0xAA && d[hdr + 2] == 0x03) {
            int et = (d[hdr + 6] << 8) | d[hdr + 7];
            const char *etn = et == 0x0800 ? "IPv4" : et == 0x86DD ? "IPv6" : et == 0x0806 ? "ARP" : et == 0x888E ? "802.1X Authentication" : nullptr;
            Field &llc = p.tree.add("Logical-Link Control", hdr, 8);
            llc.add(etn ? fmt("Type: %s (0x%04x)", etn, et) : fmt("Type: 0x%04x", et), hdr + 6, 2);
            int pl = hdr + 8;
            if (et == 0x0800 && body_end - pl >= 20) {
                const uint8_t *ip = d + pl;
                std::string src = fmt("%d.%d.%d.%d", ip[12], ip[13], ip[14], ip[15]);
                std::string dst = fmt("%d.%d.%d.%d", ip[16], ip[17], ip[18], ip[19]);
                Field &f = p.tree.add("Internet Protocol Version 4, Src: " + src + ", Dst: " + dst, pl, body_end - pl);
                f.add(fmt("Protocol: %d", ip[9]), pl + 9, 1);
                f.add("Source Address: " + src, pl + 12, 4);
                f.add("Destination Address: " + dst, pl + 16, 4);
                p.protocol = ip[9] == 6 ? "TCP" : ip[9] == 17 ? "UDP" : ip[9] == 1 ? "ICMP" : "IPv4";
                p.source = src;
                p.destination = dst;
            } else if (et == 0x888E) {
                p.protocol = "EAPOL";
                p.info = "Key (4-way handshake)";
            } else if (et == 0x0806) {
                p.protocol = "ARP";
            }
            if (pl < body_end && p.protocol == "802.11") p.tree.add(fmt("Data (%d bytes)", body_end - pl), pl, body_end - pl);
        } else {
            p.tree.add(fmt("Data (%d bytes)", body_end - hdr), hdr, body_end - hdr);
        }
    }
    uint32_t fcs = d[n - 4] | (d[n - 3] << 8) | (d[n - 2] << 16) | ((uint32_t)d[n - 1] << 24);
    w.add(fmt("Frame check sequence: 0x%08x [%s]", fcs, p.bad_crc ? "incorrect" : "correct"), n - 4, 4);
}

}  // namespace shark
