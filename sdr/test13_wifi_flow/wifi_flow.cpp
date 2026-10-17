/*
wifi_flow: WiFi activity timeline for the B200.

Scans 2.4 + 5 GHz, locks onto the busiest channels (spending --survey-share
of the time sweeping all the others), and draws every network
and device as a swimlane: time runs left to right, each packet is a mark
whose width is its airtime. Zoomed out, a pixel's brightness is the share
of that pixel's time the device spent transmitting.

    blue    sent by an AP          orange  sent by a client to its AP
    grey    other / unknown role   hatched: that channel wasn't being listened to

    ./build/wifi_flow                         # all bands
    ./build/wifi_flow --band 2.4 --focus 2
    ./build/wifi_flow --file ch36.cf32 --channel 36   # replay a 20 MS/s recording

Mouse: wheel over the lanes zooms, drag pans (pauses live view), wheel over
the names scrolls, click a network to expand/collapse, right-click to lock
onto its channel, hover a packet for details. Click a channel chip in the
strip at the top to lock/unlock it.
Keys: space pause/live, +/- zoom, left/right pan, End back to live.
*/

#include <cmath>
#include <map>

#include <QtWidgets>
#include <boost/program_options.hpp>

#include "engine.hpp"

namespace po = boost::program_options;

static const QColor C_BG(13, 15, 19), C_LANE(19, 22, 28), C_NOLISTEN(33, 36, 44), C_HDR(26, 30, 38), C_GRID(44, 48, 58);
static const QColor C_TEXT(214, 218, 226), C_DIM(128, 135, 148), C_SEL(255, 255, 255);
static const QColor C_DIR[3] = {QColor(150, 157, 170), QColor(64, 158, 255), QColor(255, 152, 56)};

static QString us_str(double s) {
    if (s < 1e-3) return QString("%1 µs").arg(s * 1e6, 0, 'f', 0);
    if (s < 1.0) return QString("%1 ms").arg(s * 1e3, 0, 'f', s < 0.01 ? 2 : 1);
    if (s < 120) return QString("%1 s").arg(s, 0, 'f', s < 10 ? 2 : 0);
    return QString("%1 min").arg(s / 60, 0, 'f', 1);
}

// ------------------------------------------------------------------ rows

struct ViewRow {
    enum Kind { NetHeader, DeviceRow, NetUnknown, GroupHeader, ChanRow } kind = DeviceRow;
    QString key;              // for expand/collapse
    QString label, sub, right;
    int indent = 0;
    bool expanded = false;
    int net = -1, dev = -1, ch = 0;
    std::vector<int> devs;    // devices drawn in this lane (headers aggregate their members)
    std::vector<int> unknown_nets, chan_rows;  // network "unidentified" rows / channel rows included
    std::vector<int> chans;   // channels whose listening coverage applies
    bool header() const { return kind == NetHeader || kind == GroupHeader; }
};

// ------------------------------------------------------------------ channel strip

class ChannelStrip : public QWidget {
public:
    ChannelStrip(wf::Engine &e, QWidget *parent) : QWidget(parent), eng_(e) {
        setFixedHeight(44);
        setMouseTracking(true);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), C_BG);
        place_chips();
        std::map<int, float> act;
        {
            std::lock_guard<std::mutex> l(eng_.model.mu);
            for (auto &c : eng_.model.chans) act[c.first] = c.second.activity;
        }
        auto focus = eng_.focus();
        auto pinned = eng_.pinned();
        int tuned = eng_.tuned();
        QFont f = font();
        f.setPointSizeF(8.5);
        p.setFont(f);
        p.setPen(C_DIM);
        p.drawText(QRect(6, 0, 60, height()), Qt::AlignVCenter, "Channels");
        for (auto &c : chips_) {
            float a = act.count(c.ch) ? act[c.ch] : -1.0f;
            QColor fill = C_LANE;
            if (a >= 0) {
                double lvl = a <= 0 ? 0.0 : std::clamp(1.0 + std::log10(a) / 2.5, 0.08, 1.0);
                fill = QColor::fromRgbF(0.075 + 0.17 * lvl, 0.086 + 0.53 * lvl, 0.11 + 0.89 * lvl);
            }
            QRectF r = c.r.adjusted(1, 1, -1, -1);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(r, 3, 3);
            if (c.ch == tuned) {
                p.setPen(QPen(C_SEL, 2));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r, 3, 3);
            }
            QFont cf = f;
            cf.setBold(focus.count(c.ch));
            p.setFont(cf);
            p.setPen(a > 0.25f ? QColor(10, 12, 16) : (focus.count(c.ch) ? C_TEXT : C_DIM));
            p.drawText(r, Qt::AlignCenter, QString::number(c.ch));
            if (focus.count(c.ch)) p.fillRect(QRectF(r.left() + 3, r.bottom() + 2, r.width() - 6, 2), QColor(255, 210, 90));
            if (pinned.count(c.ch)) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 210, 90));
                p.drawEllipse(QPointF(r.right() - 4, r.top() + 4), 2.5, 2.5);
            }
        }
        p.setFont(f);
        p.setPen(C_DIM);
        double right = chips_.empty() ? 70 : chips_.back().r.right() + 20;
        p.drawText(QRectF(right, 0, width() - right - 8, height()), Qt::AlignVCenter | Qt::AlignLeft,
                   "outline = tuned now   gold bar = locked on   dot = pinned (click a channel to pin)");
    }

    void mousePressEvent(QMouseEvent *e) override {
        for (auto &c : chips_) {
            if (c.r.contains(e->pos())) {
                bool on = !eng_.pinned().count(c.ch);
                eng_.set_pinned(c.ch, on);
                update();
            }
        }
    }

    bool event(QEvent *e) override {
        if (e->type() == QEvent::ToolTip) {
            auto *he = static_cast<QHelpEvent *>(e);
            for (auto &c : chips_) {
                if (!c.r.contains(he->pos())) continue;
                QString tip = QString("<b>Channel %1</b> (%2 MHz)<br>").arg(c.ch).arg(wf::chan_freq(c.ch) / 1e6, 0, 'f', 0);
                std::lock_guard<std::mutex> l(eng_.model.mu);
                auto &m = eng_.model;
                auto it = m.chans.find(c.ch);
                if (it == m.chans.end()) {
                    tip += "not visited yet";
                } else {
                    tip += QString("airtime %1%, last visit %2 ago<br>")
                               .arg(100.0 * it->second.activity, 0, 'f', 1)
                               .arg(us_str(m.latest - it->second.last_visit));
                    for (auto &d : m.devices) {
                        if (d.is_ap && d.primary == c.ch && d.net >= 0) {
                            const auto &n = m.nets[d.net];
                            tip += (n.hidden ? QString("&lt;hidden&gt;") : QString::fromStdString(n.ssid).toHtmlEscaped()) +
                                   " · " + QString::fromStdString(d.mac) + "<br>";
                        }
                    }
                }
                tip += "<i>click to pin / unpin</i>";
                QToolTip::showText(he->globalPos(), tip, this);
                return true;
            }
            QToolTip::hideText();
            return true;
        }
        return QWidget::event(e);
    }

private:
    struct Chip {
        int ch;
        QRectF r;
    };
    void place_chips() {
        chips_.clear();
        double x = 70, w = 27;
        int prev = 0;
        for (int ch : eng_.channels()) {
            if (prev && ((prev <= 14) != (ch <= 14) || ch - prev > 8)) x += 12;
            chips_.push_back({ch, QRectF(x, 8, w, 24)});
            x += w;
            prev = ch;
        }
    }
    wf::Engine &eng_;
    std::vector<Chip> chips_;
};

// ------------------------------------------------------------------ the timeline

class FlowView : public QAbstractScrollArea {
public:
    static constexpr int LABEL_W = 340, AXIS_H = 26, ROW_H = 20, HDR_H = 24;
    static constexpr double RAW_SPAN = 30.0;  // draw individual packets up to this view span

    std::function<void()> on_view_changed;

    FlowView(wf::Engine &e, QWidget *parent) : QAbstractScrollArea(parent), eng_(e) {
        viewport()->setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setFrameShape(QFrame::NoFrame);
        verticalScrollBar()->setSingleStep(ROW_H);
    }

    bool live() const { return live_; }
    double span() const { return span_; }
    void set_span(double s) { span_ = std::clamp(s, 50e-6, 3600.0); }
    void set_live(bool on) {
        live_ = on;
        changed();
    }
    void zoom(double factor, double anchor_frac = 1.0) {
        double ns = std::clamp(span_ * factor, 50e-6, 3600.0);
        double t_anchor = view_end_ - span_ * (1.0 - anchor_frac);
        if (!live_) view_end_ = t_anchor + ns * (1.0 - anchor_frac);
        span_ = ns;
        changed();
    }
    void set_all_expanded(bool on) {
        for (auto &r : rows_) {
            if (r.header()) user_expand_[r.key] = on;
        }
        rebuild();
    }

    // rows from the model (called a few times a second)
    void rebuild() {
        std::vector<ViewRow> rows;
        wf::Model &m = eng_.model;
        std::lock_guard<std::mutex> l(m.mu);
        double now = m.latest;
        auto recent = [&](const wf::Row &r) { return r.airtime_between(now - 60, now + 1); };

        std::vector<std::vector<int>> members(m.nets.size());
        std::vector<int> loose;
        for (size_t d = 0; d < m.devices.size(); d++) {
            int n = m.net_of((int)d);
            if (n >= 0) members[n].push_back((int)d);
            else if (m.devices[d].row.packets) loose.push_back((int)d);
        }
        // re-sort networks by recent airtime every few seconds (not every frame, so rows don't jump)
        QElapsedTimer &tm = sort_timer_;
        if (!tm.isValid() || tm.elapsed() > 5000 || net_order_.size() != m.nets.size()) {
            tm.restart();
            std::vector<std::pair<double, int>> s;
            for (size_t n = 0; n < m.nets.size(); n++) {
                double a = recent(m.nets[n].unknown);
                for (int d : members[n]) a += recent(m.devices[d].row);
                s.push_back({a, (int)n});
            }
            std::stable_sort(s.begin(), s.end(), [](auto &a, auto &b) { return a.first > b.first; });
            net_order_.clear();
            for (auto &x : s) net_order_.push_back(x.second);
        }
        auto expanded = [&](const QString &key, bool dflt) {
            auto it = user_expand_.find(key);
            return it == user_expand_.end() ? dflt : it->second;
        };
        auto dev_label = [&](int d, ViewRow &r) {
            const wf::Device &dv = m.devices[d];
            r.label = QString(dv.is_ap ? "AP   " : "STA  ") + QString::fromStdString(dv.mac);
            QString v = dv.random ? "random MAC" : QString::fromStdString(dv.vendor).left(22);
            r.sub = v;
            r.right = dv.rssi_init ? QString("%1 dB").arg(dv.rssi, 0, 'f', 0) : "";
            r.chans = dv.heard_on;
            if (dv.is_ap && dv.primary && !std::count(r.chans.begin(), r.chans.end(), dv.primary)) r.chans.push_back(dv.primary);
        };

        for (int n : net_order_) {
            auto &mem = members[n];
            const wf::Network &net = m.nets[n];
            if (mem.empty() && !net.unknown.packets) continue;
            std::sort(mem.begin(), mem.end(), [&](int a, int b) {
                if (m.devices[a].is_ap != m.devices[b].is_ap) return m.devices[a].is_ap;
                return m.devices[a].row.airtime > m.devices[b].row.airtime;
            });
            ViewRow h;
            h.kind = ViewRow::NetHeader;
            h.net = n;
            h.key = "net:" + QString::fromStdString(net.key);
            int clients = 0, ch = 0;
            for (int d : mem) {
                clients += !m.devices[d].is_ap;
                if (m.devices[d].is_ap && !ch) ch = m.devices[d].primary ? m.devices[d].primary : m.devices[d].ch;
                for (int c : m.devices[d].heard_on) {
                    if (!std::count(h.chans.begin(), h.chans.end(), c)) h.chans.push_back(c);
                }
            }
            h.expanded = expanded(h.key, clients > 0);
            h.label = net.placeholder ? "(no beacon yet) " + QString::fromStdString(net.key.substr(6))
                    : net.hidden ? "<hidden> " + QString::fromStdString(net.key.substr(7))
                                 : QString::fromStdString(net.ssid);
            int aps = (int)mem.size() - clients;
            h.sub = QString("ch %1 · %2 · %3 AP%4, %5 client%6")
                        .arg(ch).arg(net.security.empty() ? "?" : QString::fromStdString(net.security))
                        .arg(aps).arg(aps == 1 ? "" : "s").arg(clients).arg(clients == 1 ? "" : "s");
            h.devs = mem;
            if (net.unknown.packets) h.unknown_nets.push_back(n);
            rows.push_back(h);
            if (!h.expanded) continue;
            for (int d : mem) {
                ViewRow r;
                r.kind = ViewRow::DeviceRow;
                r.dev = d;
                r.indent = 1;
                r.devs = {d};
                dev_label(d, r);
                if (r.chans.empty()) r.chans = h.chans;  // only ever seen as a receiver
                rows.push_back(r);
            }
            if (net.unknown.packets) {
                ViewRow r;
                r.kind = ViewRow::NetUnknown;
                r.indent = 1;
                r.label = "STA  (unidentified)";
                r.sub = "ACKs from a client we couldn't pin down";
                r.unknown_nets = {n};
                r.chans = h.chans;
                rows.push_back(r);
            }
        }

        if (!loose.empty()) {
            ViewRow g;
            g.kind = ViewRow::GroupHeader;
            g.key = "group:loose";
            g.expanded = expanded(g.key, false);
            g.label = "Not associated";
            g.sub = QString("%1 devices (probing, or role unknown)").arg(loose.size());
            g.devs = loose;
            for (int d : loose) {
                for (int c : m.devices[d].heard_on) {
                    if (!std::count(g.chans.begin(), g.chans.end(), c)) g.chans.push_back(c);
                }
            }
            rows.push_back(g);
            if (g.expanded) {
                std::sort(loose.begin(), loose.end(), [&](int a, int b) { return m.devices[a].last > m.devices[b].last; });
                for (size_t i = 0; i < loose.size() && i < 300; i++) {
                    ViewRow r;
                    r.kind = ViewRow::DeviceRow;
                    r.dev = loose[i];
                    r.indent = 1;
                    r.devs = {loose[i]};
                    dev_label(loose[i], r);
                    rows.push_back(r);
                }
            }
        }

        std::vector<int> chs;
        for (auto &c : m.chans) {
            if (c.second.unattributed.packets) chs.push_back(c.first);
        }
        if (!chs.empty()) {
            ViewRow g;
            g.kind = ViewRow::GroupHeader;
            g.key = "group:unattr";
            g.expanded = expanded(g.key, true);
            g.label = "Unattributed";
            g.sub = "packets whose sender couldn't be determined";
            g.chan_rows = chs;
            g.chans = chs;
            rows.push_back(g);
            if (g.expanded) {
                for (int c : chs) {
                    ViewRow r;
                    r.kind = ViewRow::ChanRow;
                    r.indent = 1;
                    r.ch = c;
                    r.label = QString("ch %1").arg(c);
                    r.sub = QString("%1 MHz").arg(wf::chan_freq(c) / 1e6, 0, 'f', 0);
                    r.chan_rows = {c};
                    r.chans = {c};
                    rows.push_back(r);
                }
            }
        }
        rows_ = std::move(rows);
        int total = 0;
        for (auto &r : rows_) total += r.header() ? HDR_H : ROW_H;
        content_h_ = total;
        verticalScrollBar()->setRange(0, std::max(0, total - lanes_h()));
        verticalScrollBar()->setPageStep(lanes_h());
    }

protected:
    int lanes_h() const { return viewport()->height() - AXIS_H; }
    int lane_w() const { return std::max(10, viewport()->width() - LABEL_W); }

    void changed() {
        viewport()->update();
        if (on_view_changed) on_view_changed();
    }

    // per-pixel airtime accumulators for one lane
    struct Acc {
        std::vector<float> air[3];
        std::vector<int> count;
        void reset(int w) {
            for (auto &a : air) a.assign(w, 0.0f);
            count.assign(w, 0);
        }
    };

    void accumulate(const wf::Row &row, double t0, double px, int w, bool raw, Acc &acc) {
        double t1 = t0 + px * w;
        auto spread = [&](double s, double e, int dir, int cnt) {
            double ps = (s - t0) / px, pe = (e - t0) / px;
            if (pe < 0 || ps >= w) return;
            int a = std::max(0, (int)std::floor(ps)), b = std::min(w - 1, (int)std::floor(pe));
            int first = a;
            for (int i = a; i <= b; i++) {
                double ov = std::min(pe, (double)i + 1) - std::max(ps, (double)i);
                if (ov > 0) acc.air[dir][i] += (float)(ov * px);
            }
            acc.count[std::clamp(first, 0, w - 1)] += cnt;
        };
        if (raw && (row.raw.empty() || row.raw.front().t <= t0 || row.packets == (long)row.raw.size())) {
            auto it = std::lower_bound(row.raw.begin(), row.raw.end(), t0 - 0.006,
                                       [](const wf::Burst &b, double t) { return b.t < t; });
            for (; it != row.raw.end() && it->t < t1; ++it) {
                spread(it->t, it->t + it->dur, it->dir, 1);
            }
            return;
        }
        auto it = std::lower_bound(row.bins.begin(), row.bins.end(), (int64_t)(t0 / wf::BIN_SEC) - 1,
                                   [](const wf::Bin &b, int64_t i) { return b.idx < i; });
        for (; it != row.bins.end() && it->idx * wf::BIN_SEC < t1; ++it) {
            double s = it->idx * wf::BIN_SEC, e = s + wf::BIN_SEC;
            double tot = it->air[0] + it->air[1] + it->air[2];
            if (tot <= 0) continue;
            // spread the bin's airtime over the bin, preserving its direction split
            for (int dir = 0; dir < 3; dir++) {
                if (it->air[dir] <= 0) continue;
                double ps = (s - t0) / px, pe = (e - t0) / px;
                if (pe < 0 || ps >= w) continue;
                int a = std::max(0, (int)std::floor(ps)), b = std::min(w - 1, (int)std::floor(pe));
                for (int i = a; i <= b; i++) {
                    double ov = std::min(pe, (double)i + 1) - std::max(ps, (double)i);
                    if (ov > 0) acc.air[dir][i] += (float)(it->air[dir] * ov / (pe - ps));
                }
            }
            int pi = (int)std::floor((s - t0) / px);
            if (pi >= 0 && pi < w) acc.count[pi] += it->count;
        }
    }

    // fraction of each pixel during which channel `ch` was being listened to
    const std::vector<float> &coverage(int ch, double t0, double px, int w) {
        auto &v = cov_cache_[ch];
        if ((int)v.size() == w) return v;
        v.assign(w, 0.0f);
        auto it = eng_.model.chans.find(ch);
        if (it == eng_.model.chans.end()) return v;
        double t1 = t0 + px * w;
        const auto &iv = it->second.covered;
        auto j = std::lower_bound(iv.begin(), iv.end(), std::make_pair(t0 - 3600.0, 0.0));
        for (; j != iv.end() && j->first < t1; ++j) {
            if (j->second < t0) continue;
            double ps = (j->first - t0) / px, pe = (j->second - t0) / px;
            int a = std::max(0, (int)std::floor(ps)), b = std::min(w - 1, (int)std::floor(pe));
            for (int i = a; i <= b; i++) {
                double ov = std::min(pe, (double)i + 1) - std::max(ps, (double)i);
                if (ov > 0) v[i] = std::min(1.0f, v[i] + (float)ov);
            }
        }
        return v;
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(viewport());
        p.fillRect(viewport()->rect(), C_BG);
        int w = lane_w(), lh = lanes_h();
        wf::Model &m = eng_.model;
        std::lock_guard<std::mutex> l(m.mu);
        if (live_) view_end_ = m.latest;
        double t0 = view_end_ - span_, px = span_ / w;
        bool raw = span_ <= RAW_SPAN;
        cov_cache_.clear();
        QImage img(w, 1, QImage::Format_RGB32);
        Acc acc;

        p.save();
        p.setClipRect(0, 0, viewport()->width(), lh);
        int y = -verticalScrollBar()->value();
        QFont base = font(), bold = font(), small = font();
        bold.setBold(true);
        small.setPointSizeF(font().pointSizeF() * 0.85);
        for (size_t ri = 0; ri < rows_.size(); ri++) {
            const ViewRow &r = rows_[ri];
            int h = r.header() ? HDR_H : ROW_H;
            if (y + h < 0) {
                y += h;
                continue;
            }
            if (y > lh) break;
            row_y_[ri] = y;
            // lane
            acc.reset(w);
            for (int d : r.devs) {
                if (d < (int)m.devices.size()) accumulate(m.devices[d].row, t0, px, w, raw, acc);
            }
            for (int n : r.unknown_nets) accumulate(m.nets[n].unknown, t0, px, w, raw, acc);
            for (int c : r.chan_rows) accumulate(m.chans[c].unattributed, t0, px, w, raw, acc);
            std::vector<float> cov(w, 0.0f);
            for (int c : r.chans) {
                const auto &cv = coverage(c, t0, px, w);
                for (int i = 0; i < w; i++) cov[i] = std::max(cov[i], cv[i]);
            }
            QRgb *line = (QRgb *)img.scanLine(0);
            for (int i = 0; i < w; i++) {
                QColor bg = cov[i] >= 0.5f ? (r.header() ? C_HDR : C_LANE) : C_NOLISTEN;
                double tot = acc.air[0][i] + acc.air[1][i] + acc.air[2][i];
                if (tot <= 0) {
                    line[i] = bg.rgb();
                    continue;
                }
                double frac = std::min(1.0, tot / px);
                double lvl = frac >= 1.0 ? 1.0 : std::max(0.22, 1.0 + std::log10(frac) / 3.5);
                double cr = 0, cg = 0, cb = 0;
                for (int d = 0; d < 3; d++) {
                    double wgt = acc.air[d][i] / tot;
                    cr += wgt * C_DIR[d].redF();
                    cg += wgt * C_DIR[d].greenF();
                    cb += wgt * C_DIR[d].blueF();
                }
                line[i] = qRgb((int)(255 * (bg.redF() * (1 - lvl) + cr * lvl)), (int)(255 * (bg.greenF() * (1 - lvl) + cg * lvl)),
                               (int)(255 * (bg.blueF() * (1 - lvl) + cb * lvl)));
            }
            QRect lane(LABEL_W, y + 1, w, h - 2);
            p.drawImage(lane, img);
            // hatch where not listening
            p.setPen(Qt::NoPen);
            p.setBrush(QBrush(QColor(255, 255, 255, 22), Qt::BDiagPattern));
            for (int i = 0; i < w;) {
                if (cov[i] >= 0.5f) {
                    i++;
                    continue;
                }
                int j = i;
                while (j < w && cov[j] < 0.5f) j++;
                p.drawRect(LABEL_W + i, y + 1, j - i, h - 2);
                i = j;
            }
            // label
            QRect lr(0, y, LABEL_W - 4, h);
            p.fillRect(lr, r.header() ? C_HDR : C_BG);
            int x = 8 + r.indent * 16;
            if (r.header()) {
                p.setPen(C_DIM);
                p.setFont(base);
                p.drawText(QRect(x, y, 14, h), Qt::AlignVCenter, r.expanded ? "▾" : "▸");
                x += 16;
                p.setFont(bold);
                p.setPen(r.kind == ViewRow::GroupHeader ? C_DIM : C_TEXT);
                QString name = QFontMetrics(bold).elidedText(r.label, Qt::ElideRight, 150);
                p.drawText(QRect(x, y, 160, h), Qt::AlignVCenter, name);
                int nx = x + QFontMetrics(bold).horizontalAdvance(name) + 8;
                p.setFont(small);
                p.setPen(C_DIM);
                p.drawText(QRect(nx, y, LABEL_W - nx - 8, h), Qt::AlignVCenter,
                           QFontMetrics(small).elidedText(r.sub, Qt::ElideRight, LABEL_W - nx - 8));
            } else {
                p.setFont(base);
                p.setPen(r.kind == ViewRow::DeviceRow ? C_TEXT : C_DIM);
                p.drawText(QRect(x, y, 200, h), Qt::AlignVCenter, r.label);
                int nx = x + QFontMetrics(base).horizontalAdvance(r.label) + 8;
                p.setFont(small);
                p.setPen(C_DIM);
                int rw = QFontMetrics(small).horizontalAdvance(r.right) + 8;
                p.drawText(QRect(nx, y, LABEL_W - nx - rw - 8, h), Qt::AlignVCenter,
                           QFontMetrics(small).elidedText(r.sub, Qt::ElideRight, LABEL_W - nx - rw - 8));
                p.drawText(QRect(LABEL_W - rw - 10, y, rw, h), Qt::AlignVCenter | Qt::AlignRight, r.right);
            }
            p.fillRect(0, y + h - 1, viewport()->width(), 1, C_BG);
            y += h;
        }
        p.restore();

        // vertical grid + time axis
        draw_axis(p, t0, px, w, lh);
        if (hover_x_ >= LABEL_W && hover_x_ < LABEL_W + w) {
            p.setPen(QPen(QColor(255, 255, 255, 60), 1));
            p.drawLine(hover_x_, 0, hover_x_, lh);
        }
        if (!live_) {
            p.setPen(QColor(255, 210, 90));
            p.drawText(QRect(LABEL_W + 6, lh - 18, 200, 16), Qt::AlignLeft | Qt::AlignVCenter, "PAUSED  (End = live)");
        }
    }

    void draw_axis(QPainter &p, double t0, double px, int w, int lh) {
        p.fillRect(0, lh, viewport()->width(), AXIS_H, C_BG);
        p.fillRect(LABEL_W, lh, w, 1, C_GRID);
        static const double steps[] = {1e-5, 2e-5, 5e-5, 1e-4, 2e-4, 5e-4, 1e-3, 2e-3, 5e-3, 0.01, 0.02, 0.05, 0.1, 0.2,
                                       0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600};
        double step = 3600;
        for (double s : steps) {
            if (s / px >= 110) {
                step = s;
                break;
            }
        }
        double wall0 = eng_.session_wall();
        p.setFont(font());
        for (double t = std::ceil(t0 / step) * step; t < t0 + px * w; t += step) {
            int x = LABEL_W + (int)((t - t0) / px);
            p.fillRect(x, 0, 1, lh, QColor(255, 255, 255, 14));
            p.fillRect(x, lh, 1, 5, C_DIM);
            double wall = wall0 + t;
            QDateTime dt = QDateTime::fromMSecsSinceEpoch((qint64)std::floor(wall * 1000));
            QString s;
            if (step >= 1) s = dt.toString("hh:mm:ss");
            else if (step >= 1e-3) s = dt.toString("mm:ss.zzz");
            else s = dt.toString("ss.zzz") + QString("%1").arg((int)std::fmod(std::floor(wall * 1e6), 1000), 3, 10, QChar('0'));
            p.setPen(C_DIM);
            p.drawText(QRect(x - 60, lh + 5, 120, AXIS_H - 6), Qt::AlignHCenter | Qt::AlignTop, s);
        }
        p.setPen(C_DIM);
        p.drawText(QRect(8, lh + 4, LABEL_W - 16, AXIS_H - 6), Qt::AlignLeft | Qt::AlignTop,
                   QString("view %1 · %2/px").arg(us_str(px * w)).arg(us_str(px)));
    }

    int row_at(int y) const {
        int yy = -verticalScrollBar()->value();
        for (size_t i = 0; i < rows_.size(); i++) {
            int h = rows_[i].header() ? HDR_H : ROW_H;
            if (y >= yy && y < yy + h) return (int)i;
            yy += h;
        }
        return -1;
    }

    void wheelEvent(QWheelEvent *e) override {
        int x = (int)e->position().x();
        if (x < LABEL_W || (e->modifiers() & Qt::ShiftModifier)) {
            QAbstractScrollArea::wheelEvent(e);
            return;
        }
        double frac = std::clamp((x - LABEL_W) / (double)lane_w(), 0.0, 1.0);
        double f = e->angleDelta().y() > 0 ? 0.8 : 1.25;
        if (live_) frac = 1.0;
        zoom(f, frac);
    }

    void mousePressEvent(QMouseEvent *e) override {
        int ri = row_at(e->pos().y());
        if (e->button() == Qt::LeftButton && e->pos().x() < LABEL_W && ri >= 0 && rows_[ri].header() &&
            e->pos().y() < lanes_h()) {
            user_expand_[rows_[ri].key] = !rows_[ri].expanded;
            rebuild();
            viewport()->update();
            return;
        }
        if (e->button() == Qt::RightButton && ri >= 0) {
            context_menu(ri, e->globalPos());
            return;
        }
        if (e->button() == Qt::LeftButton && e->pos().x() >= LABEL_W) {
            drag_x_ = e->pos().x();
            drag_end_ = view_end_;
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        hover_x_ = e->pos().x();
        if (drag_x_ >= 0 && (e->buttons() & Qt::LeftButton)) {
            double px = span_ / lane_w();
            live_ = false;
            view_end_ = drag_end_ - (e->pos().x() - drag_x_) * px;
            changed();
        }
        viewport()->update();
    }

    void mouseReleaseEvent(QMouseEvent *) override { drag_x_ = -1; }

    void mouseDoubleClickEvent(QMouseEvent *e) override {
        if (e->pos().x() < LABEL_W) return;
        live_ = false;
        zoom(0.25, (e->pos().x() - LABEL_W) / (double)lane_w());
    }

    void leaveEvent(QEvent *) override {
        hover_x_ = -1;
        viewport()->update();
    }

    void keyPressEvent(QKeyEvent *e) override {
        switch (e->key()) {
        case Qt::Key_Space: set_live(!live_); break;
        case Qt::Key_End: set_live(true); break;
        case Qt::Key_Plus:
        case Qt::Key_Equal: zoom(0.5, live_ ? 1.0 : 0.5); break;
        case Qt::Key_Minus: zoom(2.0, live_ ? 1.0 : 0.5); break;
        case Qt::Key_Left: live_ = false; view_end_ -= span_ * 0.25; changed(); break;
        case Qt::Key_Right: view_end_ += span_ * 0.25; changed(); break;
        default: QAbstractScrollArea::keyPressEvent(e);
        }
    }

    void resizeEvent(QResizeEvent *e) override {
        QAbstractScrollArea::resizeEvent(e);
        verticalScrollBar()->setRange(0, std::max(0, content_h_ - lanes_h()));
        verticalScrollBar()->setPageStep(lanes_h());
    }

    void scrollContentsBy(int, int) override { viewport()->update(); }

    bool viewportEvent(QEvent *e) override {
        if (e->type() == QEvent::ToolTip) {
            auto *he = static_cast<QHelpEvent *>(e);
            QString tip = tooltip_at(he->pos());
            if (tip.isEmpty()) QToolTip::hideText();
            else QToolTip::showText(he->globalPos(), tip, viewport());
            return true;
        }
        return QAbstractScrollArea::viewportEvent(e);
    }

    QString tooltip_at(QPoint pos) {
        int ri = row_at(pos.y());
        if (ri < 0 || pos.y() >= lanes_h()) return "";
        const ViewRow &r = rows_[ri];
        wf::Model &m = eng_.model;
        std::lock_guard<std::mutex> l(m.mu);
        if (pos.x() < LABEL_W) {
            if (r.dev < 0) return "";
            const wf::Device &d = m.devices[r.dev];
            QString s = QString("<b>%1</b> %2<br>").arg(QString::fromStdString(d.mac)).arg(d.is_ap ? "(access point)" : "");
            if (!d.vendor.empty()) s += QString::fromStdString(d.vendor).toHtmlEscaped() + "<br>";
            if (d.random) s += "locally administered (randomized) address<br>";
            if (d.ap >= 0) s += "associated with " + QString::fromStdString(m.devices[d.ap].mac) + "<br>";
            s += QString("%1 packets, %2 airtime<br>").arg(d.row.packets).arg(us_str(d.row.airtime));
            if (d.rssi_init) s += QString("signal %1 dBFS<br>").arg(d.rssi, 0, 'f', 1);
            QStringList chs;
            for (int c : d.heard_on) chs << QString::number(c);
            s += "heard on ch " + chs.join(", ");
            return s;
        }
        int w = lane_w();
        double px = span_ / w, t0 = view_end_ - span_;
        double t = t0 + (pos.x() - LABEL_W + 0.5) * px;
        double wall = eng_.session_wall() + t;
        auto clock = [&](double tt) {
            double wl = eng_.session_wall() + tt;
            return QDateTime::fromMSecsSinceEpoch((qint64)std::floor(wl * 1000)).toString("hh:mm:ss.zzz") +
                   QString("%1").arg((int)std::fmod(std::floor(wl * 1e6), 1000), 3, 10, QChar('0'));
        };
        // nearest packet within a few pixels
        std::vector<const wf::Row *> src;
        for (int d : r.devs) src.push_back(&m.devices[d].row);
        for (int n : r.unknown_nets) src.push_back(&m.nets[n].unknown);
        for (int c : r.chan_rows) src.push_back(&m.chans[c].unattributed);
        const wf::Burst *best = nullptr;
        int best_dev = -1;
        double best_d = 3 * px;
        for (size_t k = 0; k < src.size(); k++) {
            const auto &raw = src[k]->raw;
            auto it = std::lower_bound(raw.begin(), raw.end(), t - 0.006, [](const wf::Burst &b, double x) { return b.t < x; });
            for (; it != raw.end() && it->t < t + best_d; ++it) {
                double d = t < it->t ? it->t - t : (t > it->t + it->dur ? t - it->t - it->dur : 0.0);
                if (d < best_d || (d == 0 && best_d == 0)) {
                    best_d = d;
                    best = &*it;
                    best_dev = k < r.devs.size() ? r.devs[k] : -1;
                }
            }
        }
        if (best && px < 0.05) {
            const wf::Burst &b = *best;
            QString s = QString("<b>%1</b><br>").arg(clock(b.t));
            s += QString("airtime %1").arg(us_str(b.dur));
            if (b.bytes >= 0) s += QString(", %1 bytes").arg(b.bytes);
            s += QString("<br>%1%2, %3 dBFS, ch %4<br>")
                     .arg(b.phy).arg(b.what[0] ? QString(" · ") + b.what : QString()).arg(b.rssi, 0, 'f', 1).arg(b.ch);
            QString from = best_dev >= 0 ? QString::fromStdString(m.devices[best_dev].mac) : "?";
            QString to = b.peer >= 0 ? QString::fromStdString(m.devices[b.peer].mac) : "?";
            s += QString("%1 → %2").arg(from, to);
            static const char *dirn[3] = {"", " (from AP)", " (to AP)"};
            s += dirn[b.dir];
            if (b.guess) s += "<br><i>sender guessed from signal level</i>";
            if (b.format != 0) s += "<br><i>payload not decodable (802.11n/ac/ax or CCK): timing only</i>";
            return s;
        }
        // summary of this pixel
        Acc acc;
        int i = pos.x() - LABEL_W;
        acc.reset(w);
        for (auto *rw : src) accumulate(*rw, t0, px, w, px * w <= RAW_SPAN, acc);
        double tot = acc.air[0][i] + acc.air[1][i] + acc.air[2][i];
        QString s = QString("<b>%1</b> (+%2)<br>").arg(clock(t - 0.5 * px)).arg(us_str(px));
        if (tot <= 0) return s + "no packets";
        s += QString("%1 packets, %2 airtime (%3%)<br>").arg(acc.count[i]).arg(us_str(tot)).arg(100 * tot / px, 0, 'f', 1);
        s += QString("from AP %1% · to AP %2% · other %3%")
                 .arg(100 * acc.air[1][i] / tot, 0, 'f', 0).arg(100 * acc.air[2][i] / tot, 0, 'f', 0)
                 .arg(100 * acc.air[0][i] / tot, 0, 'f', 0);
        (void)wall;
        return s;
    }

    void context_menu(int ri, QPoint gpos) {
        const ViewRow &r = rows_[ri];
        int ch = r.ch;
        QString mac;
        {
            std::lock_guard<std::mutex> l(eng_.model.mu);
            auto &m = eng_.model;
            if (r.dev >= 0) {
                ch = m.devices[r.dev].primary ? m.devices[r.dev].primary : m.devices[r.dev].ch;
                mac = QString::fromStdString(m.devices[r.dev].mac);
            } else if (r.kind == ViewRow::NetHeader) {
                for (int d : r.devs) {
                    if (m.devices[d].is_ap) {
                        ch = m.devices[d].primary ? m.devices[d].primary : m.devices[d].ch;
                        break;
                    }
                }
            }
        }
        QMenu menu(this);
        QAction *pin = nullptr, *copy = nullptr;
        if (ch > 0) {
            bool pinned = eng_.pinned().count(ch);
            pin = menu.addAction(pinned ? QString("Unlock channel %1").arg(ch) : QString("Lock onto channel %1").arg(ch));
        }
        if (!mac.isEmpty()) copy = menu.addAction("Copy MAC address");
        QAction *a = menu.exec(gpos);
        if (a && a == pin) eng_.set_pinned(ch, !eng_.pinned().count(ch));
        if (a && a == copy) QGuiApplication::clipboard()->setText(mac);
    }

private:
    wf::Engine &eng_;
    std::vector<ViewRow> rows_;
    std::map<size_t, int> row_y_;
    std::map<QString, bool> user_expand_;
    std::vector<int> net_order_;
    QElapsedTimer sort_timer_;
    std::map<int, std::vector<float>> cov_cache_;
    int content_h_ = 0;
    bool live_ = true;
    double view_end_ = 0.0, span_ = 10.0;
    int hover_x_ = -1, drag_x_ = -1;
    double drag_end_ = 0.0;
};

// ------------------------------------------------------------------ window

class Legend : public QWidget {
public:
    explicit Legend(QWidget *p) : QWidget(p) {
        int w = 0;
        for (const char *t : {"from AP", "to AP", "other / unknown", "not listening"}) {
            w += 32 + QFontMetrics(font()).horizontalAdvance(t) + 14;
        }
        setFixedSize(w, 22);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        struct Item {
            QColor c;
            const char *t;
            bool hatch;
        } items[] = {{C_DIR[1], "from AP", false}, {C_DIR[2], "to AP", false}, {C_DIR[0], "other / unknown", false},
                     {C_NOLISTEN, "not listening", true}};
        int x = 0;
        for (auto &it : items) {
            QRect sw(x, 5, 22, 12);
            p.fillRect(sw, it.c);
            if (it.hatch) p.fillRect(sw, QBrush(QColor(255, 255, 255, 40), Qt::BDiagPattern));
            p.setPen(C_TEXT);
            p.drawText(QRect(x + 27, 0, 120, 22), Qt::AlignVCenter, it.t);
            x += 32 + QFontMetrics(font()).horizontalAdvance(it.t) + 14;
        }
    }
};

class MainWindow : public QMainWindow {
public:
    MainWindow(wf::Engine &e) : eng_(e) {
        setWindowTitle("wifi_flow");
        auto *central = new QWidget(this);
        auto *vb = new QVBoxLayout(central);
        vb->setContentsMargins(0, 0, 0, 0);
        vb->setSpacing(0);
        strip_ = new ChannelStrip(e, central);
        view_ = new FlowView(e, central);
        vb->addWidget(strip_);
        vb->addWidget(view_, 1);
        setCentralWidget(central);

        auto *tb = addToolBar("view");
        tb->setMovable(false);
        live_btn_ = tb->addAction("Pause");
        connect(live_btn_, &QAction::triggered, [this] { view_->set_live(!view_->live()); });
        tb->addSeparator();
        for (auto z : std::vector<std::pair<const char *, double>>{{"1 ms", 1e-3}, {"10 ms", 0.01}, {"100 ms", 0.1}, {"1 s", 1}, {"10 s", 10}, {"1 min", 60}, {"10 min", 600}}) {
            double s = z.second;
            connect(tb->addAction(z.first), &QAction::triggered, [this, s] { view_->zoom(s / view_->span(), view_->live() ? 1.0 : 0.5); });
        }
        tb->addSeparator();
        connect(tb->addAction("Expand all"), &QAction::triggered, [this] { view_->set_all_expanded(true); });
        connect(tb->addAction("Collapse all"), &QAction::triggered, [this] { view_->set_all_expanded(false); });
        tb->addSeparator();
        tb->addWidget(new QLabel(" Lock onto ", tb));
        auto *fn = new QSpinBox(tb);
        fn->setRange(0, 8);
        fn->setValue(e.focus_n());
        fn->setSuffix(" ch");
        fn->setToolTip("number of busy channels to lock onto (pinned channels count)");
        connect(fn, QOverload<int>::of(&QSpinBox::valueChanged), [this](int v) { eng_.set_focus_n(v); });
        tb->addWidget(fn);
        tb->addWidget(new QLabel("  survey ", tb));
        auto *sh = new QSpinBox(tb);
        sh->setRange(5, 95);
        sh->setSingleStep(5);
        sh->setValue((int)std::lround(e.survey_share() * 100));
        sh->setSuffix(" % of time");
        sh->setToolTip("share of time spent sweeping every other channel");
        connect(sh, QOverload<int>::of(&QSpinBox::valueChanged), [this](int v) { eng_.set_survey_share(v / 100.0); });
        tb->addWidget(sh);
        tb->addSeparator();
        tb->addWidget(new Legend(tb));
        view_->on_view_changed = [this] { live_btn_->setText(view_->live() ? "Pause" : "Live"); };

        status_ = new QLabel(this);
        statusBar()->addWidget(status_, 1);

        auto *paint = new QTimer(this);
        connect(paint, &QTimer::timeout, [this] {
            view_->viewport()->update();
            strip_->update();
        });
        paint->start(50);
        auto *slow = new QTimer(this);
        connect(slow, &QTimer::timeout, [this] {
            view_->rebuild();
            double pct = 0, guessed = 0;
            {
                std::lock_guard<std::mutex> l(eng_.model.mu);
                auto &m = eng_.model;
                pct = m.air_total > 0 ? 100 * m.air_attributed / m.air_total : 0;
                long n = m.frames_decoded + m.frames_timing_only;
                guessed = n ? 100.0 * m.frames_guessed / n : 0;
            }
            status_->setText(QString::fromStdString(eng_.status()) +
                             QString("  |  %1% of airtime attributed (%2% of packets by signal level)").arg(pct, 0, 'f', 0).arg(guessed, 0, 'f', 0));
        });
        slow->start(500);
        view_->rebuild();
        resize(1500, 900);
    }

private:
    wf::Engine &eng_;
    ChannelStrip *strip_;

public:
    FlowView *view_;

private:
    QAction *live_btn_;
    QLabel *status_;
};

int main(int argc, char *argv[]) {
    wf::Config cfg;
    std::string band, shot;
    double shot_after, span;
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
        ("file", po::value<std::string>(&cfg.file), "replay a 20 MS/s cf32 recording of one channel")
        ("channel", po::value<int>(&cfg.file_channel)->default_value(1), "channel of --file")
        ("span", po::value<double>(&span)->default_value(10.0), "initial time span shown (seconds)")
        ("screenshot", po::value<std::string>(&shot), "save a PNG of the window and quit")
        ("screenshot-after", po::value<double>(&shot_after)->default_value(15.0), "seconds before --screenshot");
    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv).options(desc).allow_unregistered().run(), vm);
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
    if (!cfg.file.empty()) {
        cfg.channels = {cfg.file_channel};
        cfg.realtime = true;
    }

    QApplication app(argc, argv);
    app.setStyle("Fusion");
    QPalette pal;
    pal.setColor(QPalette::Window, QColor(22, 25, 31));
    pal.setColor(QPalette::WindowText, C_TEXT);
    pal.setColor(QPalette::Base, C_BG);
    pal.setColor(QPalette::Text, C_TEXT);
    pal.setColor(QPalette::Button, QColor(34, 38, 46));
    pal.setColor(QPalette::ButtonText, C_TEXT);
    pal.setColor(QPalette::ToolTipBase, QColor(30, 34, 42));
    pal.setColor(QPalette::ToolTipText, C_TEXT);
    pal.setColor(QPalette::Highlight, QColor(64, 158, 255));
    app.setPalette(pal);

    wf::Engine eng;
    eng.start(cfg);
    MainWindow w(eng);
    w.view_->set_span(span);
    w.show();
    if (!shot.empty()) {
        QTimer::singleShot((int)(shot_after * 1000), [&] {
            w.grab().save(QString::fromStdString(shot));
            app.quit();
        });
    }
    int rc = app.exec();
    eng.stop();
    return rc;
}
