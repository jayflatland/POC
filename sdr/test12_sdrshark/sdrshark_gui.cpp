/*
SDR Shark: a Wireshark-style packet viewer for the B200.

Captures WiFi (802.11 a/b/g), Bluetooth LE advertising, and ADS-B (Mode S),
lists packets, and shows a protocol tree plus a hex/ASCII view where the
selected field's bytes are highlighted. Captures save as pcapng for
Wireshark.

Filter box: space-separated terms that must all match (case-insensitive
substring of protocol / source / destination / channel / info); prefix
a term with ! to exclude, e.g.   beacon !hidden    or   apple    or   ads-b
*/

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTextBlock>
#include <QStatusBar>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <memory>
#include <mutex>

#include "capture.hpp"
#include "pcapng.hpp"

using PacketPtr = std::shared_ptr<shark::Packet>;

// -------------------- packet list model --------------------

class PacketModel : public QAbstractTableModel {
public:
    enum Col { No, Time, Channel, Rssi, Proto, Src, Dst, Len, Info, NCOLS };

    int rowCount(const QModelIndex & = QModelIndex()) const override { return (int)view_.size(); }
    int columnCount(const QModelIndex & = QModelIndex()) const override { return NCOLS; }

    QVariant headerData(int section, Qt::Orientation o, int role) const override {
        static const char *names[NCOLS] = {"No.", "Time", "Channel", "RSSI", "Protocol", "Source", "Destination", "Length", "Info"};
        if (o == Qt::Horizontal && role == Qt::DisplayRole) return names[section];
        return {};
    }

    QVariant data(const QModelIndex &idx, int role) const override {
        const shark::Packet &p = *view_[idx.row()];
        if (role == Qt::DisplayRole) {
            switch (idx.column()) {
            case No: return (qlonglong)p.number;
            case Time: return QString::number(p.time, 'f', 6);
            case Channel: return QString::fromStdString(p.channel);
            case Rssi: return QString::number(p.rssi, 'f', 1);
            case Proto: return QString::fromStdString(p.protocol);
            case Src: return QString::fromStdString(p.source);
            case Dst: return QString::fromStdString(p.destination);
            case Len: return (int)p.data.size();
            case Info: return QString::fromStdString(p.info);
            }
        } else if (role == Qt::BackgroundRole) {
            return row_color(p);
        } else if (role == Qt::TextAlignmentRole && (idx.column() == No || idx.column() == Len || idx.column() == Rssi)) {
            return int(Qt::AlignRight | Qt::AlignVCenter);
        }
        return {};
    }

    void add(const std::vector<PacketPtr> &ps) {
        std::vector<PacketPtr> shown;
        for (auto &p : ps) {
            all_.push_back(p);
            if (matches(*p)) shown.push_back(p);
        }
        if (shown.empty()) return;
        beginInsertRows(QModelIndex(), (int)view_.size(), (int)(view_.size() + shown.size() - 1));
        view_.insert(view_.end(), shown.begin(), shown.end());
        endInsertRows();
    }

    void set_filter(const QString &f) {
        terms_ = f.toLower().split(' ', Qt::SkipEmptyParts);
        beginResetModel();
        view_.clear();
        for (auto &p : all_) if (matches(*p)) view_.push_back(p);
        endResetModel();
    }

    void clear() {
        beginResetModel();
        all_.clear();
        view_.clear();
        endResetModel();
    }

    PacketPtr at(int row) const { return row >= 0 && row < (int)view_.size() ? view_[row] : nullptr; }
    const std::vector<PacketPtr> &all() const { return all_; }
    size_t total() const { return all_.size(); }

private:
    bool matches(const shark::Packet &p) const {
        if (terms_.isEmpty()) return true;
        QString hay = QString::fromStdString(p.protocol + " " + p.source + " " + p.destination + " " + p.channel + " " + p.info).toLower();
        if (p.link == shark::Link::Wifi) hay += " wifi wlan";
        if (p.link == shark::Link::Ble) hay += " ble bluetooth btle";
        if (p.link == shark::Link::ModeS) hay += " adsb modes";
        for (const QString &t : terms_) {
            bool neg = t.startsWith('!');
            QString s = neg ? t.mid(1) : t;
            if (s.isEmpty()) continue;
            if (hay.contains(s) == neg) return false;
        }
        return true;
    }

    static QColor row_color(const shark::Packet &p) {
        if (p.bad_crc) return QColor(255, 180, 180);
        if (p.link == shark::Link::Ble) return QColor(210, 245, 210);
        if (p.link == shark::Link::ModeS) return QColor(255, 248, 200);
        if (p.data.empty()) return QColor(255, 255, 255);
        int type = (p.data[0] >> 2) & 3;
        if (type == 0) return QColor(232, 222, 255);  // management
        if (type == 1) return QColor(236, 236, 236);  // control
        if (p.protocol != "802.11") return QColor(218, 238, 255);
        return QColor(205, 230, 255);                  // data
    }

    std::vector<PacketPtr> all_, view_;
    QStringList terms_;
};

// -------------------- main window --------------------

class MainWindow : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle("SDR Shark");
        resize(1400, 900);
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

        auto *tb = addToolBar("Capture");
        tb->setMovable(false);
        mode_ = new QComboBox;
        mode_->addItems({"WiFi 2.4 GHz (ch 1-11)", "WiFi 2.4 GHz (ch 1, 6, 11)", "WiFi 5 GHz", "WiFi 2.4 + 5 GHz",
                         "Bluetooth LE advertising", "ADS-B / Mode S (1090 MHz)", "LTE cell scan"});
        tb->addWidget(new QLabel(" Mode: "));
        tb->addWidget(mode_);
        gain_ = new QDoubleSpinBox;
        gain_->setRange(0, 76);
        gain_->setValue(40);
        gain_->setSuffix(" dB");
        tb->addWidget(new QLabel("  Gain: "));
        tb->addWidget(gain_);
        ant_ = new QComboBox;
        ant_->addItems({"TX/RX", "RX2"});
        tb->addWidget(new QLabel("  Antenna: "));
        tb->addWidget(ant_);
        tb->addSeparator();
        start_ = new QPushButton("Start");
        tb->addWidget(start_);
        auto *clear = new QPushButton("Clear");
        tb->addWidget(clear);
        auto *save = new QPushButton("Save pcapng...");
        tb->addWidget(save);
        tb->addSeparator();
        autoscroll_ = new QCheckBox("Auto-scroll");
        autoscroll_->setChecked(true);
        tb->addWidget(autoscroll_);

        filter_ = new QLineEdit;
        filter_->setPlaceholderText("Filter: e.g.  beacon   !ack   apple   ads-b   (space-separated, ! to exclude)  — Enter to apply");
        filter_->setClearButtonEnabled(true);

        table_ = new QTableView;
        table_->setModel(&model_);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->verticalHeader()->setVisible(false);
        table_->verticalHeader()->setDefaultSectionSize(20);
        table_->horizontalHeader()->setStretchLastSection(true);
        table_->setFont(mono);
        int widths[] = {60, 90, 70, 55, 70, 130, 130, 55};
        for (int i = 0; i < 8; i++) table_->setColumnWidth(i, widths[i]);

        tree_ = new QTreeWidget;
        tree_->setHeaderHidden(true);
        tree_->setFont(mono);

        hex_ = new QPlainTextEdit;
        hex_->setReadOnly(true);
        hex_->setFont(mono);
        hex_->setLineWrapMode(QPlainTextEdit::NoWrap);

        auto *split = new QSplitter(Qt::Vertical);
        split->addWidget(table_);
        split->addWidget(tree_);
        split->addWidget(hex_);
        split->setSizes({450, 270, 180});

        auto *central = new QWidget;
        auto *lay = new QVBoxLayout(central);
        lay->setContentsMargins(4, 4, 4, 4);
        lay->addWidget(filter_);
        lay->addWidget(split);
        setCentralWidget(central);

        status_ = new QLabel("Idle");
        statusBar()->addWidget(status_, 1);
        count_ = new QLabel;
        statusBar()->addPermanentWidget(count_);

        connect(start_, &QPushButton::clicked, this, [this] { toggle(); });
        connect(clear, &QPushButton::clicked, this, [this] {
            model_.clear();
            tree_->clear();
            hex_->clear();
            next_number_ = 1;
            t0_set_ = false;
        });
        connect(save, &QPushButton::clicked, this, [this] { save_pcap(); });
        connect(filter_, &QLineEdit::returnPressed, this, [this] { model_.set_filter(filter_->text()); });
        connect(filter_, &QLineEdit::textChanged, this, [this](const QString &t) { if (t.isEmpty()) model_.set_filter(""); });
        connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
                [this](const QModelIndex &cur, const QModelIndex &) { show_packet(model_.at(cur.row())); });
        connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it, QTreeWidgetItem *) {
            if (it) highlight(it->data(0, Qt::UserRole).toInt(), it->data(0, Qt::UserRole + 1).toInt());
        });

        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] { pump(); });
        timer->start(100);
    }

    ~MainWindow() override { capture_.stop(); }

    void set_mode(const QString &m) {
        static const QStringList names = {"wifi24", "wifi3", "wifi5", "wifiall", "ble", "adsb", "lte"};
        int i = names.indexOf(m.toLower());
        if (i >= 0) mode_->setCurrentIndex(i);
    }
    void set_gain(double g) { gain_->setValue(g); }
    void set_antenna(const QString &a) { ant_->setCurrentText(a); }
    void start_capture() {
        if (!capture_.running()) toggle();
    }

private:
    void toggle() {
        if (capture_.running()) {
            capture_.stop();
            start_->setText("Start");
            mode_->setEnabled(true);
            return;
        }
        shark::CaptureConfig cfg;
        int m = mode_->currentIndex();
        cfg.mode = m == 4 ? shark::Mode::Ble : m == 5 ? shark::Mode::Adsb : m == 6 ? shark::Mode::Lte : shark::Mode::Wifi;
        cfg.wifi_channels = m == 0 ? shark::wifi_channels_for("2.4")
                            : m == 1 ? std::vector<int>{1, 6, 11}
                            : m == 2 ? shark::wifi_channels_for("5")
                                     : shark::wifi_channels_for("all");
        cfg.gain = gain_->value();
        cfg.antenna = ant_->currentText().toStdString();
        capture_.start(cfg, [this](shark::Packet &&p) {
            std::lock_guard<std::mutex> l(pending_mutex_);
            pending_.push_back(std::make_shared<shark::Packet>(std::move(p)));
        });
        start_->setText("Stop");
        mode_->setEnabled(false);
    }

    // move packets from the capture thread into the model
    void pump() {
        std::vector<PacketPtr> batch;
        {
            std::lock_guard<std::mutex> l(pending_mutex_);
            batch.swap(pending_);
        }
        if (!batch.empty()) {
            std::sort(batch.begin(), batch.end(), [](auto &a, auto &b) { return a->wall < b->wall; });
            for (auto &p : batch) {
                if (!t0_set_) {
                    t0_ = p->wall;
                    t0_set_ = true;
                }
                p->number = next_number_++;
                p->time = p->wall - t0_;
            }
            model_.add(batch);
            if (autoscroll_->isChecked() && !table_->selectionModel()->hasSelection()) table_->scrollToBottom();
        }
        status_->setText(QString::fromStdString(capture_.running() ? capture_.status() : "Idle"));
        count_->setText(QString("Packets: %1   Displayed: %2").arg(model_.total()).arg(model_.rowCount()));
    }

    void show_packet(const PacketPtr &p) {
        tree_->clear();
        cur_ = p;
        if (!p) {
            hex_->clear();
            return;
        }
        auto *frame = new QTreeWidgetItem(tree_, {QString("Frame %1: %2 bytes on air, %3, %4 dBFS")
                                                      .arg(p->number)
                                                      .arg(p->data.size())
                                                      .arg(QString::fromStdString(p->channel))
                                                      .arg(p->rssi, 0, 'f', 1)});
        frame->setData(0, Qt::UserRole, -1);
        new QTreeWidgetItem(frame, {QString("Arrival time: %1 (%2 s since start)")
                                        .arg(QDateTime::fromMSecsSinceEpoch((qint64)(p->wall * 1000)).toString("yyyy-MM-dd hh:mm:ss.zzz"))
                                        .arg(p->time, 0, 'f', 6)});
        new QTreeWidgetItem(frame, {QString("Frequency: %1 MHz").arg(p->freq / 1e6, 0, 'f', 3)});
        for (auto &f : p->tree.children) add_field(nullptr, f);
        for (int i = 0; i < tree_->topLevelItemCount(); i++) tree_->topLevelItem(i)->setExpanded(i > 0);
        render_hex(*p);
    }

    void add_field(QTreeWidgetItem *parent, const shark::Field &f) {
        auto *it = parent ? new QTreeWidgetItem(parent, {QString::fromStdString(f.label)})
                          : new QTreeWidgetItem(tree_, {QString::fromStdString(f.label)});
        it->setData(0, Qt::UserRole, f.offset);
        it->setData(0, Qt::UserRole + 1, f.length);
        for (auto &c : f.children) add_field(it, c);
    }

    // "0000  xx xx xx xx xx xx xx xx  xx xx xx xx xx xx xx xx  ................"
    static int hex_col(int j) { return 6 + 3 * j + (j >= 8 ? 1 : 0); }
    static int ascii_col(int j) { return 6 + 49 + 1 + j; }

    void render_hex(const shark::Packet &p) {
        QString s;
        for (size_t off = 0; off < p.data.size(); off += 16) {
            QString line = QString("%1  ").arg(off, 4, 16, QChar('0'));
            QString ascii;
            for (int j = 0; j < 16; j++) {
                if (off + j < p.data.size()) {
                    uint8_t b = p.data[off + j];
                    line += QString("%1 ").arg(b, 2, 16, QChar('0'));
                    ascii += (b >= 32 && b < 127) ? QChar(b) : QChar('.');
                } else {
                    line += "   ";
                }
                if (j == 7) line += " ";
            }
            s += line + " " + ascii + "\n";
        }
        hex_->setPlainText(s);
    }

    void highlight(int offset, int length) {
        QList<QTextEdit::ExtraSelection> sels;
        if (cur_ && offset >= 0 && length > 0) {
            QTextCharFormat fmt;
            fmt.setBackground(QColor(120, 170, 255));
            QTextDocument *doc = hex_->document();
            for (int i = offset; i < offset + length && i < (int)cur_->data.size(); i++) {
                QTextBlock block = doc->findBlockByNumber(i / 16);
                int j = i % 16;
                for (auto [col, len] : {std::pair<int, int>{hex_col(j), 2}, {ascii_col(j), 1}}) {
                    QTextEdit::ExtraSelection sel;
                    sel.format = fmt;
                    sel.cursor = QTextCursor(block);
                    sel.cursor.setPosition(block.position() + col);
                    sel.cursor.setPosition(block.position() + col + len, QTextCursor::KeepAnchor);
                    sels.append(sel);
                }
            }
            hex_->setTextCursor(QTextCursor(doc->findBlockByNumber(offset / 16)));
            hex_->ensureCursorVisible();
        }
        hex_->setExtraSelections(sels);
    }

    void save_pcap() {
        QString path = QFileDialog::getSaveFileName(this, "Save capture", "capture.pcapng", "pcapng (*.pcapng)");
        if (path.isEmpty()) return;
        shark::PcapngWriter w;
        if (!w.open(path.toStdString())) {
            QMessageBox::warning(this, "Save", "Couldn't write " + path);
            return;
        }
        for (auto &p : model_.all()) w.write(*p);
        w.close();
        status_->setText(QString("Saved %1 packets to %2").arg(model_.total()).arg(path));
    }

    QComboBox *mode_, *ant_;
    QDoubleSpinBox *gain_;
    QPushButton *start_;
    QCheckBox *autoscroll_;
    QLineEdit *filter_;
    QTableView *table_;
    QTreeWidget *tree_;
    QPlainTextEdit *hex_;
    QLabel *status_, *count_;
    PacketModel model_;
    shark::Capture capture_;
    std::mutex pending_mutex_;
    std::vector<PacketPtr> pending_;
    PacketPtr cur_;
    long next_number_ = 1;
    double t0_ = 0;
    bool t0_set_ = false;
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    MainWindow w;
    // optional: sdrshark --mode wifi24|wifi3|wifi5|wifiall|ble|adsb [--gain N] [--antenna RX2] [--start]
    QStringList args = app.arguments();
    for (int i = 1; i < args.size(); i++) {
        if (args[i] == "--mode" && i + 1 < args.size()) w.set_mode(args[++i]);
        else if (args[i] == "--gain" && i + 1 < args.size()) w.set_gain(args[++i].toDouble());
        else if (args[i] == "--antenna" && i + 1 < args.size()) w.set_antenna(args[++i]);
        else if (args[i] == "--start") QTimer::singleShot(200, &w, [&w] { w.start_capture(); });
    }
    w.show();
    return app.exec();
}
