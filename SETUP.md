# Setup

Build notes for the C++ / SDR experiments in this repo. There is **no
top-level build** — each project has its own `CMakeLists.txt` and is built
independently. Most of the active work is under `sdr/` and targets an
**Ettus USRP B200/B210** over USB 3.

Tested on Ubuntu 24.04 ("noble") with UHD 4.9 and FFmpeg 6.1.

## 1. One-time system dependencies

Everything the SDR + graphics projects need, in one go:

```bash
sudo apt update
sudo apt install -y \
    build-essential cmake git pkg-config \
    libuhd-dev uhd-host python3-uhd \
    libboost-program-options-dev libboost-system-dev libboost-filesystem-dev \
    libfftw3-dev \
    freeglut3-dev libgl1-mesa-dev \
    libavcodec-dev libavutil-dev libswscale-dev libswresample-dev \
    qtbase5-dev \
    pulseaudio-utils alsa-utils \
    python3-numpy python3-scipy python3-matplotlib
```

Optional extras:

```bash
sudo apt install -y vlc              # test11 --vlc playback fallback
sudo apt install -y wireshark-common # capinfos, to validate test12 pcapng files
sudo apt install -y gnuradio         # only for the old sdr/test02, sdr/test03 scripts
```

### UHD device firmware/FPGA images (once)

```bash
sudo uhd_images_downloader        # fetches the B200 firmware + FPGA image
uhd_find_devices                  # should list the B200 serial when plugged in
```

### USB access without root

Install Ettus's udev rule so the B200 is usable as a normal user, then
replug it:

```bash
sudo cp /usr/lib/uhd/utils/uhd-usrp.rules /etc/udev/rules.d/ 2>/dev/null \
  || sudo cp /usr/share/uhd/utils/uhd-usrp.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

### Audio

Playback projects (`test05`, `test10`, `test11`) pipe audio to `pacat`
(from `pulseaudio-utils`; works under both PulseAudio and PipeWire). If
your system uses PipeWire, make sure `pipewire-pulse` is active so `pacat`
has a server to talk to.

### macOS (test13 so far)

`sdr/test13_wifi_flow` also builds on macOS with Homebrew:

```bash
brew install cmake pkg-config boost fftw qt@5 uhd
python3 $(brew --prefix uhd)/lib/uhd/utils/uhd_images_downloader.py -t b2xx   # needs requests
```

- `qt@5` is keg-only; test13's `CMakeLists.txt` finds it via `brew --prefix`.
- With an **Intel Homebrew** (`/usr/local`, including under Rosetta on Apple
  Silicon) the libraries are x86_64-only, so CMake builds x86_64 binaries
  automatically; they run under Rosetta. A native `/opt/homebrew` builds arm64.
- **Run `conda deactivate` first** if conda is active: it sets `CC`/`CXX`,
  `CMAKE_PREFIX_PATH` and puts its own `pkg-config` first on `PATH`, which
  pulls in conda's libraries and compiler and breaks the link.
- CMake uses Homebrew's `pkg-config` even if MacPorts' is first on `PATH`.
- No udev rules are needed on macOS, but on Apple Silicon laptops macOS
  blocks new USB accessories until you click **Allow** ("Allow accessory to
  connect?"; or System Settings → Privacy & Security). Until then the B200
  shows in System Information but `uhd_find_devices` finds nothing.
- Homebrew's UHD doesn't put `uhd_images_downloader` on `PATH`, hence the
  script above; run it with any Python that has `requests`.
- Quit Chrome if the B200 stops responding; it can hold the device via WebUSB.
- Tested on macOS 13 (Apple Silicon, Intel Homebrew under Rosetta) with
  UHD 4.11, Boost 1.92, Qt 5.15. There's no UHD bottle for that combo, so
  `brew install uhd` builds it from source (~10+ min). If it fails linking
  `boost::program_options`, look for stale hand-installed Boost / UHD files
  in `/usr/local` (`brew link --overwrite boost`, remove old `libuhd*`).

## 2. Building a project

Same pattern everywhere:

```bash
cd sdr/<project>
cmake -S . -B build
cmake --build build -j
./build/<binary>            # see each project's header comment for flags
```

`build/` is gitignored. Recordings (`*.cf32`, `*.cfile`, `*.sc16`) and
`*.pcapng` captures are gitignored too.

## 3. Projects and what they need

| Project | Binary | Needs beyond the base | What it does |
|---|---|---|---|
| `orbital_physics` | `orbital_physics` | GLUT, GL | N-body solar-system sim |
| `particle_stuff` | `particle_stuff` | GLUT, GL | 300-body gravity collapse |
| `reimann` | `reiman_simple` | just g++ (`./make.sh`) | Riemann zeta from scratch |
| `sdr/test05_fm_waterfall` | `fm_waterfall` | UHD, FFTW, GL/GLUT, pacat | live FM radio + waterfall |
| `sdr/test06_gps` | *(python)* | python3-uhd, numpy | GPS L1 recorder + acquisition |
| `sdr/test07_wifi_ssid` | `wifi_scan`, `phy_selftest` | UHD, FFTW | 802.11 beacon/SSID scanner |
| `sdr/test08_wifi_dump` | `wifi_dump` | UHD, FFTW (+ test07 headers) | 802.11 packet hex dumper |
| `sdr/test09_tx_scanner` | `tx_scanner` | UHD, FFTW | generic transmission scanner |
| `sdr/test10_airband` | `airband` | UHD, FFTW, pacat | VHF aviation (AM) scanner |
| `sdr/test11_atsc_tv` | `atsc_tv` | UHD, FFTW, FFmpeg, GL/GLUT, pacat | ATSC digital TV receiver |
| `sdr/test12_sdrshark` | `sdrshark`, `sdrshark_cli` | UHD, FFTW, Qt5 (GUI only) (+ test07 headers) | Wireshark-style WiFi/BLE/ADS-B/LTE viewer |
| `sdr/test13_wifi_flow` | `wifi_flow`, `wifi_flow_cli` | UHD, FFTW, Qt5 (GUI only) (+ test07 headers) | per-network / per-device WiFi activity timeline |

Notes:
- **test08, test12 and test13 `#include` headers from `sdr/test07_wifi_ssid`** (the
  802.11 PHY decoders), so keep the `sdr/` tree intact — they reference it
  by relative path in their `CMakeLists.txt`.
- **test12's and test13's GUIs** (`sdrshark`, `wifi_flow`) need `qtbase5-dev`;
  without it CMake still builds the CLI and prints a note.
- **test13** streams 20 MS/s, so the B200 must be on USB 3 (USB 2 tops out
  around 9.6 MS/s).
- **test11** needs the FFmpeg `-dev` packages; without them CMake fails.

## 4. Fresh-machine quickstart

```bash
git clone git@github.com:jayflatland/POC.git ~/Projects/POC
cd ~/Projects/POC
# run the apt install from section 1, then:
sudo uhd_images_downloader
cd sdr/test12_sdrshark && cmake -S . -B build && cmake --build build -j
./build/sdrshark            # pick a mode, press Start
```

## 5. Moving a Claude Code session to another machine

The code is all in git — cloning the repo is enough to keep working. To
carry a *conversation* over, the Claude Code state is under `~/.claude`:

- `~/.claude/projects/-home-jay-Projects-POC/<uuid>.jsonl` — the chat
  transcript (resume with `claude --resume`). The folder name encodes the
  project path, so the repo must sit at the **same absolute path**
  (`/home/jay/Projects/POC`) on the new machine for `--resume` to match it.
- `~/.claude/projects/-home-jay-Projects-POC/memory/` — saved preferences.
- `~/.claude/history.jsonl`, `~/.claude/settings.json` — history and config.

You log in again on the new machine (credentials are not portable), and
scratch recordings/prototypes under `/tmp/claude-*/.../scratchpad/` do **not**
travel with the transcript.
