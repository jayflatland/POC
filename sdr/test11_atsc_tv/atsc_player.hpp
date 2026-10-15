#pragma once

// Built-in player: MPEG transport stream in, picture in an OpenGL window and
// sound through pacat out.
//
//   TS demux (PAT / PMT / ATSC PSIP TVCT, PES assembly) is done here;
//   libavcodec only decodes the MPEG-2 (or H.264) video and AC-3 audio.
//   Video and audio are scheduled against one presentation clock by PTS.

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <spawn.h>
#include <unistd.h>

extern char **environ;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace atsc {

static double now_sec() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// -------------------- PSI section assembly --------------------

class SectionAssembler {
public:
    // returns complete sections via cb(table_id, data, len)
    template <typename Cb>
    void push(const uint8_t *ts, Cb cb) {
        bool pusi = ts[1] & 0x40;
        int afc = (ts[3] >> 4) & 3;
        if (!(afc & 1)) {
            return;
        }
        int off = 4 + ((afc & 2) ? 1 + ts[4] : 0);
        if (off >= 188) {
            return;
        }
        const uint8_t *p = ts + off;
        int n = 188 - off;
        if (pusi) {
            int ptr = p[0];
            p++;
            n--;
            if (ptr < n && !buf_.empty()) {
                buf_.insert(buf_.end(), p, p + ptr);
                flush(cb);
            }
            buf_.assign(p + std::min(ptr, n), p + n);
            active_ = true;
        } else if (active_) {
            buf_.insert(buf_.end(), p, p + n);
        }
        flush(cb);
    }

private:
    template <typename Cb>
    void flush(Cb cb) {
        while (buf_.size() >= 3) {
            if (buf_[0] == 0xFF) {  // stuffing
                buf_.clear();
                return;
            }
            size_t len = 3 + (((buf_[1] & 0x0F) << 8) | buf_[2]);
            if (buf_.size() < len) {
                return;
            }
            cb(buf_[0], buf_.data(), len);
            buf_.erase(buf_.begin(), buf_.begin() + len);
        }
    }
    std::vector<uint8_t> buf_;
    bool active_ = false;
};

struct ProgramInfo {
    int number = 0;
    int pmt_pid = -1;
    int video_pid = -1, video_type = 0;
    int audio_pid = -1, audio_type = 0;
    std::string name;  // from PSIP: "30.1 KXYZ-HD"
};

struct VideoFrame {
    double pts;
    AVFrame *frame;
};

struct AudioChunk {
    double pts;
    std::vector<int16_t> samples;  // stereo, 48 kHz
};

// -------------------- audio output --------------------

// pacat as a child process on a small pipe, so we know (and can kill) exactly
// what's still waiting to be played. Run verbose: it reports the stream's real
// output latency (server buffer + device) a few times a second on stderr.
class AudioSink {
public:
    bool start() {
        int in[2], err[2];
        if (pipe(in) != 0) return false;
        if (pipe(err) != 0) {
            ::close(in[0]);
            ::close(in[1]);
            return false;
        }
        fcntl(in[1], F_SETPIPE_SZ, 4096);
        fcntl(in[1], F_SETFD, FD_CLOEXEC);
        fcntl(err[0], F_SETFD, FD_CLOEXEC);
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_adddup2(&fa, in[0], 0);
        posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_adddup2(&fa, err[1], 2);
        posix_spawn_file_actions_addclose(&fa, in[0]);
        posix_spawn_file_actions_addclose(&fa, err[1]);
        const char *argv[] = {"pacat", "-v", "--raw", "--format=s16le", "--rate=48000", "--channels=2",
                              "--latency-msec=60", "--client-name=atsc_tv", nullptr};
        pid_t pid;
        int rc = posix_spawnp(&pid, "pacat", &fa, nullptr, (char *const *)argv, environ);
        posix_spawn_file_actions_destroy(&fa);
        ::close(in[0]);
        ::close(err[1]);
        if (rc != 0) {
            ::close(in[1]);
            ::close(err[0]);
            return false;
        }
        pid_ = pid;
        fd_ = in[1];
        latency_ = 0.06;
        known_ = false;
        int efd = err[0];
        reader_ = std::thread([this, efd] { read_latency(efd); });
        return true;
    }

    void stop() {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
        if (pid_ > 0) {
            kill(pid_, SIGKILL);  // drop whatever it still had buffered
            waitpid(pid_, nullptr, 0);
        }
        pid_ = -1;
        if (reader_.joinable()) reader_.join();  // stderr hits EOF once pacat is gone
    }

    bool write(const int16_t *s, size_t n) {
        const char *p = (const char *)s;
        size_t left = n * 2;
        while (left > 0) {
            ssize_t w = ::write(fd_, p, left);
            if (w < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            p += w;
            left -= w;
        }
        return true;
    }

    // seconds of audio written but not yet heard
    double pending_seconds() const {
        int n = 0;
        if (fd_ >= 0) ioctl(fd_, FIONREAD, &n);
        return n / (4.0 * 48000.0) + latency_.load();
    }
    double pipe_seconds() const {
        int n = 0;
        if (fd_ >= 0) ioctl(fd_, FIONREAD, &n);
        return n / (4.0 * 48000.0);
    }
    double latency() const { return latency_; }
    pid_t pid() const { return pid_; }

private:
    void read_latency(int fd) {
        char buf[512];
        std::string line;
        ssize_t n;
        while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
            for (ssize_t i = 0; i < n; i++) {
                char c = buf[i];
                if (c != '\r' && c != '\n') {
                    line += c;
                    continue;
                }
                const char *l = strstr(line.c_str(), "Latency: ");
                if (l) {
                    double v = atol(l + 9) / 1e6;  // "Latency: 50252 usec."
                    latency_ = known_ ? 0.8 * latency_.load() + 0.2 * v : v;
                    known_ = true;
                }
                line.clear();
            }
        }
        ::close(fd);
    }

    int fd_ = -1;
    pid_t pid_ = -1;
    std::thread reader_;
    std::atomic<double> latency_{0.06};
    std::atomic<bool> known_{false};
};

// -------------------- player --------------------

class Player {
public:
    ~Player() { stop(); }

    void start(int program_index) {
        want_index_ = program_index;
        av_log_set_level(AV_LOG_QUIET);
        demux_thread_ = std::thread([this] { demux_loop(); });
        audio_thread_ = std::thread([this] { audio_loop(); });

    }

    void stop() {
        if (stopping_.exchange(true)) {
            return;
        }
        ts_cv_.notify_all();
        if (demux_thread_.joinable()) demux_thread_.join();
        if (audio_thread_.joinable()) audio_thread_.join();

        close_decoders();
        std::lock_guard<std::mutex> lock(video_mutex_);
        for (auto &v : video_q_) av_frame_free(&v.frame);
        video_q_.clear();
        sink_.stop();
    }

    // called by the receiver for every TS packet
    void push_ts(const uint8_t *ts, bool ok) {
        if (!ok) return;  // RS failed: drop, the decoders conceal
        std::lock_guard<std::mutex> lock(ts_mutex_);
        if (ts_q_.size() > 200000) return;
        ts_q_.emplace_back();
        std::memcpy(ts_q_.back().data(), ts, 188);
        ts_cv_.notify_one();
    }

    void change_program(int delta) { switch_req_ += delta; }
    void toggle_mute() { muted_ = !muted_; }
    void volume(float d) { volume_ = std::max(0.0f, std::min(2.0f, volume_ + d)); }
    float get_volume() const { return volume_; }
    bool muted() const { return muted_; }

    // GL thread: newest frame that's due; caller owns it
    AVFrame *take_due_frame(double *pts_out) {
        double clk;
        if (!master_clock(&clk)) return nullptr;
        std::lock_guard<std::mutex> lock(video_mutex_);
        AVFrame *due = nullptr;
        // a frame is shown on the first display tick at or after its time; taking it up to
        // 10 ms early centers the error instead of always being late
        while (!video_q_.empty() && video_q_.front().pts <= clk + 0.010) {
            if (due) av_frame_free(&due);
            due = video_q_.front().frame;
            *pts_out = video_q_.front().pts;
            video_q_.pop_front();
        }
        return due;
    }

    std::string osd() {
        std::lock_guard<std::mutex> lock(info_mutex_);
        return osd_text_;
    }
    double osd_age() const { return now_sec() - osd_time_; }
    // A/V debugging: downstream audio latency, and how far the last shown frame was from the audio clock
    std::string av_debug() {
        double a = 0.0;
        bool ok = audio_clock(&a);
        char buf[200];
        size_t q;
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            q = audio_q_.size();
        }
        snprintf(buf, sizeof(buf),
                 "pulse %.0f ms, pipe %.0f ms, audio clock %s %.3f, last frame offset %+.0f ms | written %ld, queue %zu, "
                 "resets %ld, pacat pid %d",
                 sink_.latency() * 1e3, sink_.pipe_seconds() * 1e3, ok ? "ok" : "--", a, last_offset_ * 1e3,
                 (long)written_, q, (long)resets_, (int)sink_.pid());
        return buf;
    }
    void note_shown(double pts) {
        double clk;
        if (master_clock(&clk)) last_offset_ = pts - clk;
    }

    std::string status() {
        std::lock_guard<std::mutex> lock(info_mutex_);
        return status_text_;
    }

private:
    // ---- clocks ----
    // The audio clock is the PTS of the sample coming out of the speaker right now:
    // everything written, minus what's still in the pipe and in PulseAudio.
    bool audio_clock(double *out) {
        std::lock_guard<std::mutex> lock(amap_mutex_);
        if (marks_.empty()) return false;
        double behind = sink_.pending_seconds();
        double heard = (double)written_ - behind * 48000.0;
        if (heard < (double)marks_.front().start) return false;  // nothing audible yet
        const Mark *m = &marks_.front();
        for (auto &k : marks_) {
            if ((double)k.start <= heard) m = &k;
            else break;
        }
        *out = m->pts + (heard - (double)m->start) / 48000.0;
        while (marks_.size() > 2 && (double)marks_[1].start < heard - 48000.0) marks_.pop_front();
        return true;
    }

    // audio when there is any; otherwise wall time continuing from the last audio
    // reading (or, for silent programs, from the first video frame)
    bool master_clock(double *out) {
        double a;
        double now = now_sec();
        if (audio_clock(&a)) {
            clock_p0_ = a;
            clock_w0_ = now;
            clock_started_ = true;
            *out = a;
            return true;
        }
        if (!clock_started_) return false;
        *out = clock_p0_ + (now - clock_w0_);
        return true;
    }

    // ---- demux + decode thread ----
    void demux_loop() {
        std::array<uint8_t, 188> pkt;
        while (!stopping_) {
            {
                std::unique_lock<std::mutex> lock(ts_mutex_);
                ts_cv_.wait_for(lock, std::chrono::milliseconds(100), [this] { return !ts_q_.empty() || stopping_; });
                if (ts_q_.empty()) continue;
                pkt = ts_q_.front();
                ts_q_.pop_front();
            }
            int delta = switch_req_.exchange(0);
            if (delta != 0 && !programs_.empty()) {
                cur_index_ = ((cur_index_ + delta) % (int)programs_.size() + (int)programs_.size()) % (int)programs_.size();
                select_program(cur_index_);
            }
            handle_ts(pkt.data());
        }
    }

    void handle_ts(const uint8_t *ts) {
        int pid = ((ts[1] & 0x1F) << 8) | ts[2];
        if (pid == 0) {
            pat_.push(ts, [&](int tid, const uint8_t *s, size_t len) { if (tid == 0x00) parse_pat(s, len); });
        } else if (pid == 0x1FFB) {
            psip_.push(ts, [&](int tid, const uint8_t *s, size_t len) { if (tid == 0xC8) parse_tvct(s, len); });
        } else if (pmt_asm_.count(pid)) {
            pmt_asm_[pid].push(ts, [&](int tid, const uint8_t *s, size_t len) { if (tid == 0x02) parse_pmt(s, len); });
        } else if (cur_index_ >= 0 && cur_index_ < (int)programs_.size()) {
            const ProgramInfo &p = programs_[cur_index_];
            if (pid == p.video_pid) pes_in(ts, video_pes_, true);
            else if (pid == p.audio_pid) pes_in(ts, audio_pes_, false);
        }
    }

    void parse_pat(const uint8_t *s, size_t len) {
        if (!programs_.empty()) return;  // keep the first PAT we see
        std::vector<ProgramInfo> progs;
        for (size_t i = 8; i + 4 <= len - 4; i += 4) {
            int num = (s[i] << 8) | s[i + 1];
            int pid = ((s[i + 2] & 0x1F) << 8) | s[i + 3];
            if (num == 0) continue;  // network PID
            ProgramInfo p;
            p.number = num;
            p.pmt_pid = pid;
            progs.push_back(p);
            pmt_asm_[pid];
        }
        programs_ = progs;
        update_status();
    }

    void parse_pmt(const uint8_t *s, size_t len) {
        int num = (s[3] << 8) | s[4];
        int pi_len = ((s[10] & 0x0F) << 8) | s[11];
        size_t i = 12 + pi_len;
        int vpid = -1, vtype = 0, apid = -1, atype = 0;
        while (i + 5 <= len - 4) {
            int type = s[i];
            int epid = ((s[i + 1] & 0x1F) << 8) | s[i + 2];
            int es_len = ((s[i + 3] & 0x0F) << 8) | s[i + 4];
            if (vpid < 0 && (type == 0x02 || type == 0x1B || type == 0x80)) { vpid = epid; vtype = type; }
            if (apid < 0 && (type == 0x81 || type == 0x87)) { apid = epid; atype = type; }
            i += 5 + es_len;
        }
        for (size_t k = 0; k < programs_.size(); k++) {
            ProgramInfo &p = programs_[k];
            if (p.number != num) continue;
            bool changed = p.video_pid != vpid || p.audio_pid != apid;
            p.video_pid = vpid; p.video_type = vtype; p.audio_pid = apid; p.audio_type = atype;
            if (cur_index_ < 0 && (int)k == std::min(want_index_, (int)programs_.size() - 1) && vpid >= 0) {
                cur_index_ = (int)k;
                select_program(cur_index_);
            } else if (changed && (int)k == cur_index_) {
                select_program(cur_index_);
            }
        }
        update_status();
    }

    // ATSC Terrestrial Virtual Channel Table: channel names and major.minor numbers
    void parse_tvct(const uint8_t *s, size_t len) {
        int n = s[9];
        size_t i = 10;
        for (int c = 0; c < n && i + 32 <= len - 4; c++) {
            std::string name;
            for (int k = 0; k < 7; k++) {
                int ch = (s[i + 2 * k] << 8) | s[i + 2 * k + 1];
                if (ch >= 32 && ch < 127) name += (char)ch;
            }
            int major = ((s[i + 14] & 0x0F) << 6) | (s[i + 15] >> 2);
            int minor = ((s[i + 15] & 0x03) << 8) | s[i + 16];
            int prog = (s[i + 24] << 8) | s[i + 25];
            int dlen = ((s[i + 30] & 0x03) << 8) | s[i + 31];
            char label[64];
            snprintf(label, sizeof(label), "%d.%d %s", major, minor, name.c_str());
            for (auto &p : programs_) {
                if (p.number == prog && p.name != label) {
                    p.name = label;
                    if (&p - &programs_[0] == cur_index_) set_osd();
                }
            }
            i += 32 + dlen;
        }
        update_status();
    }

    // ---- program switching ----
    void select_program(int index) {
        close_decoders();
        {
            std::lock_guard<std::mutex> lock(video_mutex_);
            for (auto &v : video_q_) av_frame_free(&v.frame);
            video_q_.clear();
            clock_started_ = false;
        }
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            audio_q_.clear();
        }
        audio_reset_ = true;
        video_pes_.clear();
        audio_pes_.clear();
        const ProgramInfo &p = programs_[index];
        no_audio_program_ = p.audio_pid < 0;
        AVCodecID vid = p.video_type == 0x1B ? AV_CODEC_ID_H264 : AV_CODEC_ID_MPEG2VIDEO;
        AVCodecID aid = p.audio_type == 0x87 ? AV_CODEC_ID_EAC3 : AV_CODEC_ID_AC3;
        open_decoder(vid, &vctx_, &vparser_);
        vctx_->thread_count = 0;  // let FFmpeg pick
        avcodec_open2(vctx_, avcodec_find_decoder(vid), nullptr);
        open_decoder(aid, &actx_, &aparser_);
        avcodec_open2(actx_, avcodec_find_decoder(aid), nullptr);
        set_osd();
    }

    void open_decoder(AVCodecID id, AVCodecContext **ctx, AVCodecParserContext **parser) {
        *ctx = avcodec_alloc_context3(avcodec_find_decoder(id));
        *parser = av_parser_init(id);
    }

    void close_decoders() {
        if (vparser_) { av_parser_close(vparser_); vparser_ = nullptr; }
        if (aparser_) { av_parser_close(aparser_); aparser_ = nullptr; }
        if (vctx_) avcodec_free_context(&vctx_);
        if (actx_) avcodec_free_context(&actx_);
        if (swr_) swr_free(&swr_);
    }

    // ---- PES assembly ----
    struct Pes {
        std::vector<uint8_t> data;
        int64_t pts = AV_NOPTS_VALUE;
        bool active = false;
        void clear() { data.clear(); active = false; pts = AV_NOPTS_VALUE; }
    };
    Pes video_pes_, audio_pes_;

    void pes_in(const uint8_t *ts, Pes &pes, bool video) {
        bool pusi = ts[1] & 0x40;
        int afc = (ts[3] >> 4) & 3;
        if (!(afc & 1)) return;
        int off = 4 + ((afc & 2) ? 1 + ts[4] : 0);
        if (off >= 188) return;
        if (pusi) {
            if (pes.active && !pes.data.empty()) {
                decode(pes, video);
            }
            const uint8_t *h = ts + off;
            if (188 - off < 9 || h[0] != 0 || h[1] != 0 || h[2] != 1) {
                pes.clear();
                return;
            }
            int hdr_len = h[8];
            pes.pts = AV_NOPTS_VALUE;
            if ((h[7] & 0x80) && 188 - off >= 14) {
                pes.pts = ((int64_t)(h[9] & 0x0E) << 29) | (h[10] << 22) | ((h[11] & 0xFE) << 14) | (h[12] << 7) | (h[13] >> 1);
            }
            int start = off + 9 + hdr_len;
            pes.data.assign(ts + std::min(start, 188), ts + 188);
            pes.active = true;
        } else if (pes.active) {
            pes.data.insert(pes.data.end(), ts + off, ts + 188);
        }
    }

    // ---- decoding ----
    void decode(Pes &pes, bool video) {
        AVCodecContext *ctx = video ? vctx_ : actx_;
        AVCodecParserContext *parser = video ? vparser_ : aparser_;
        if (!ctx || !parser) return;
        AVPacket *pkt = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        const uint8_t *data = pes.data.data();
        int size = (int)pes.data.size();
        int64_t pts = pes.pts;
        while (size > 0) {
            int used = av_parser_parse2(parser, ctx, &pkt->data, &pkt->size, data, size, pts, AV_NOPTS_VALUE, 0);
            pts = AV_NOPTS_VALUE;
            data += used;
            size -= used;
            if (pkt->size == 0) continue;
            pkt->pts = parser->pts;
            if (avcodec_send_packet(ctx, pkt) < 0) continue;
            while (avcodec_receive_frame(ctx, frame) == 0) {
                int64_t ts90 = frame->best_effort_timestamp;
                if (ts90 == AV_NOPTS_VALUE) ts90 = frame->pts;
                double t = ts90 == AV_NOPTS_VALUE ? -1.0 : ts90 / 90000.0;
                if (video) on_video(frame, t);
                else on_audio(frame, t);
            }
        }
        av_frame_free(&frame);
        av_packet_free(&pkt);
    }

    void on_video(AVFrame *frame, double t) {
        if (t < 0) t = last_vpts_ + 1.0 / 30.0;
        last_vpts_ = t;
        AVFrame *copy = av_frame_clone(frame);
        std::unique_lock<std::mutex> lock(video_mutex_);
        if (!clock_started_ && no_audio_program_) {
            // silent program: show this frame BUFFER seconds from now
            clock_p0_ = t;
            clock_w0_ = now_sec() + BUFFER;
            clock_started_ = true;
        }
        if (video_q_.size() > 90) {  // way behind: drop the oldest
            av_frame_free(&video_q_.front().frame);
            video_q_.pop_front();
        }
        video_q_.push_back({t, copy});
        // keep decode order sorted by PTS (decoder already reorders; this is cheap insurance)
        for (size_t i = video_q_.size() - 1; i > 0 && video_q_[i].pts < video_q_[i - 1].pts; i--) {
            std::swap(video_q_[i], video_q_[i - 1]);
        }
        if (frame->width != vw_ || frame->height != vh_ || (frame->flags & AV_FRAME_FLAG_INTERLACED) != interlaced_) {
            vw_ = frame->width;
            vh_ = frame->height;
            interlaced_ = frame->flags & AV_FRAME_FLAG_INTERLACED;
            lock.unlock();
            update_status();
        }
    }

    void on_audio(AVFrame *frame, double t) {
        if (!swr_) {
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            if (swr_alloc_set_opts2(&swr_, &stereo, AV_SAMPLE_FMT_S16, 48000, &frame->ch_layout,
                                    (AVSampleFormat)frame->format, frame->sample_rate, 0, nullptr) < 0 ||
                swr_init(swr_) < 0) {
                swr_free(&swr_);
                return;
            }
            std::lock_guard<std::mutex> lock(info_mutex_);
            char buf[64];
            snprintf(buf, sizeof(buf), "%d ch %d Hz", frame->ch_layout.nb_channels, frame->sample_rate);
            audio_desc_ = buf;
        }
        AudioChunk c;
        c.pts = t;
        int max_out = swr_get_out_samples(swr_, frame->nb_samples);
        c.samples.resize(2 * max_out);
        uint8_t *out = (uint8_t *)c.samples.data();
        int n = swr_convert(swr_, &out, max_out, (const uint8_t **)frame->extended_data, frame->nb_samples);
        if (n <= 0) return;
        c.samples.resize(2 * n);
        std::lock_guard<std::mutex> lock(audio_mutex_);
        if (audio_q_.size() < 400) audio_q_.push_back(std::move(c));
    }

    // ---- audio output: one continuous stream, paced by the sound card ----
    void audio_loop() {
        const int SILENCE = 480;  // 10 ms
        std::vector<int16_t> silence(2 * SILENCE, 0);
        double next_pts = -1.0;
        sink_.start();
        while (!stopping_) {
            if (audio_reset_.exchange(false)) {
                resets_++;
                // new program: throw away everything queued downstream
                sink_.stop();
                sink_.start();
                std::lock_guard<std::mutex> lock(amap_mutex_);
                marks_.clear();
                written_ = 0;
                next_pts = -1.0;
            }
            AudioChunk c;
            bool have = false;
            {
                std::lock_guard<std::mutex> lock(audio_mutex_);
                if (!audio_q_.empty()) {
                    c = std::move(audio_q_.front());
                    audio_q_.pop_front();
                    have = true;
                }
            }
            double buffered = sink_.pending_seconds();
            if (have) {
                if (buffered > 0.5) continue;  // fell behind (e.g. after a stall): skip to catch up
                if (c.pts < 0) c.pts = next_pts;
                size_t frames = c.samples.size() / 2;
                float g = muted_ ? 0.0f : volume_.load();
                for (auto &v : c.samples) v = (int16_t)std::max(-32767.0f, std::min(32767.0f, v * g));
                {
                    std::lock_guard<std::mutex> lock(amap_mutex_);
                    marks_.push_back({written_, c.pts});
                }
                if (!sink_.write(c.samples.data(), c.samples.size())) {
                    audio_reset_ = true;
                    continue;
                }
                {
                    std::lock_guard<std::mutex> lock(amap_mutex_);
                    written_ += (long)frames;
                }
                next_pts = c.pts + frames / 48000.0;
            } else if (next_pts >= 0 && buffered < 0.03) {
                // about to run dry: keep the device going with a little silence, so it
                // never stalls and comes back later than it should
                {
                    std::lock_guard<std::mutex> lock(amap_mutex_);
                    marks_.push_back({written_, next_pts});
                }
                sink_.write(silence.data(), silence.size());
                {
                    std::lock_guard<std::mutex> lock(amap_mutex_);
                    written_ += SILENCE;
                }
                next_pts += (double)SILENCE / 48000.0;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        sink_.stop();
    }

    void set_osd() {
        std::lock_guard<std::mutex> lock(info_mutex_);
        if (cur_index_ < 0 || cur_index_ >= (int)programs_.size()) return;
        const ProgramInfo &p = programs_[cur_index_];
        char buf[128];
        snprintf(buf, sizeof(buf), "%s  (program %d, %d of %zu)", p.name.empty() ? "?" : p.name.c_str(), p.number,
                 cur_index_ + 1, programs_.size());
        osd_text_ = buf;
        osd_time_ = now_sec();
    }

    void update_status() {
        std::lock_guard<std::mutex> lock(info_mutex_);
        char buf[256];
        std::string name = "?";
        if (cur_index_ >= 0 && cur_index_ < (int)programs_.size() && !programs_[cur_index_].name.empty()) {
            name = programs_[cur_index_].name;
        }
        snprintf(buf, sizeof(buf), "%s  %dx%d%s  %s", name.c_str(), vw_, vh_, interlaced_ ? "i" : "p",
                 audio_desc_.c_str());
        status_text_ = buf;
    }

    static constexpr double BUFFER = 0.6;  // seconds between decoding and showing

    std::atomic<bool> stopping_{false};
    std::thread demux_thread_, audio_thread_;
    std::mutex ts_mutex_;
    std::condition_variable ts_cv_;
    std::deque<std::array<uint8_t, 188>> ts_q_;

    SectionAssembler pat_, psip_;
    std::map<int, SectionAssembler> pmt_asm_;
    std::vector<ProgramInfo> programs_;
    int cur_index_ = -1, want_index_ = 0;
    std::atomic<int> switch_req_{0};

    AVCodecContext *vctx_ = nullptr, *actx_ = nullptr;
    AVCodecParserContext *vparser_ = nullptr, *aparser_ = nullptr;
    SwrContext *swr_ = nullptr;
    double last_vpts_ = 0.0;

    std::mutex video_mutex_;
    std::deque<VideoFrame> video_q_;
    std::atomic<bool> clock_started_{false};
    std::atomic<double> clock_p0_{0.0}, clock_w0_{0.0};
    int vw_ = 0, vh_ = 0;
    bool interlaced_ = false;

    std::mutex audio_mutex_;
    std::deque<AudioChunk> audio_q_;
    AudioSink sink_;
    std::atomic<bool> audio_reset_{false}, no_audio_program_{false};
    std::atomic<double> last_offset_{0.0};
    struct Mark {
        long start;
        double pts;
    };
    std::mutex amap_mutex_;
    std::deque<Mark> marks_;
    long written_ = 0;
    std::atomic<long> resets_{0};
    std::atomic<float> volume_{1.0f};
    std::atomic<bool> muted_{false};

    std::mutex info_mutex_;
    std::string osd_text_, status_text_, audio_desc_;
    std::atomic<double> osd_time_{0.0};
};

}  // namespace atsc
