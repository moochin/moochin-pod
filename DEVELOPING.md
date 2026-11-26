# Developing Pod Hub

## Build and test

```bash
pip install platformio
pio test -e native      # protocol unit tests, no hardware needed
pio run -e dial         # firmware for the M5Stack Dial
pio run -e dial -t upload && pio device monitor
```

`pio run -e dial` also writes `.pio/build/dial/podhub-factory.bin`: bootloader, partitions and app merged into one image for offset `0x0` (see `scripts/merge_bin.py`). The browser installers flash this file. CI (`.github/workflows/build.yml`) runs the tests and uploads that image on every push.

## Layout

```
src/
  main.cpp                 wiring only (driver choice, loop, status JSON)
  board/Board.*            M5Unified wrapper: knob (quadrature ISR), press, beep queue, power off
  ui/DialUi.*              on-Dial screens: home, countdown, running, results, menu, off
  engine/DrillEngine.*     drill state machine (no Arduino deps, unit tested)
  engine/DrillConfig.h     drill = data; engine/Drills.h = catalogue
  storage/PersonalBests.h  PBs per player/drill/length (NVS)
  storage/Results.*        session history on LittleFS + RAM summaries + CSV export
  storage/ShotFormat.h     compact per-round log format (unit tested)
  storage/Players.h        player names (NVS)
  settings/Settings.h      NVS settings
  web/PhoneServer.*        Wi-Fi AP "PodHub", captive DNS, AsyncWebServer, WebSocket /ws
data/index.html            phone page (single file); gzipped into src/web/index_html_gz.h at build
installer/index.html       one-click installer page (ESP Web Tools), built into a site by scripts/make_site.py
docs/images/               screenshots for the README
tools/page_test/           fake hub + Playwright screenshots of every phone screen
  pods/PodDriver.h         hardware-agnostic pod interface (BLE now, ESP-NOW later)
  pods/BlazePodDriver.*    NimBLE central implementation
  pods/MockPodDriver.*     simulated pods ("Demo pods" setting + native tests)
  pods/blazepod_protocol.* pure protocol functions (unit tested)
test/test_protocol/        Unity tests: checksum, colour, tap parsing
test/test_engine/          Unity tests: drill engine against MockPodDriver
test/test_storage/         Unity tests: session log format round trip
```

## BlazePod protocol notes

Ported from [sasodoma/blazepod-hacking](https://github.com/sasodoma/blazepod-hacking) `main.c`, cross-checked against [Snerls/blazepod](https://github.com/Snerls/blazepod) `protocol.py`.

- **Discovery:** the local name starts with `BlazePod` and there are at least 5 bytes of manufacturer data. If there are several manufacturer data entries, the longest one is used.
- **Unlock:** write `"sea"` + checksum (7 bytes) to `6e400002-…` with a response. The checksum uses only the *last 5 bytes* of the manufacturer data, so whether or not the 2-byte company ID is included (NimBLE includes it) doesn't matter.
- **Colour:** `50c912a2-…`, written without a response, in `[G, B, R]` order with an optional `0x01` = turn off on tap.
- **Taps:** notifications on `50c9727e-…`: `[state, ms LE u32, ?, ?, ?]`. Reaction time comes from the pod itself, so BLE latency doesn't affect it.
- **Test vectors** in `test/test_protocol` were produced by compiling the reference `main.c` with gcc. The Python port gives the same output.

### Failure detection

A pod that rejects the unlock is expected to drop the link soon after (Snerls has a probe, `probe_xpod_auth.py`, built around a ~10 s kick; not yet confirmed). The driver counts disconnects within 15 s of unlocking. After 3 in a row, or if the expected services are missing, the pod is marked `NotResponding` and the UI says it may have newer firmware. It is still retried every 10 s.

### Concurrency

- The `pods` task (core 0, next to the NimBLE host) does all scanning, connecting and reconnecting, and blocking GATT calls.
- The Arduino `loop()` (core 1) calls only `setColour()`, a write without response that doesn't block, and `pollTap()`.
- Tap notifications go from the NimBLE host into a FreeRTOS queue of 32 entries.
- Known pods reconnect straight to their address without scanning, backing off 0.5 / 1 / 2 s.

### Pod numbering and persistence

When a pod is first assigned a number, its address and manufacturer data are saved to NVS (namespace `pods`, keys `p0`..`p5`). At boot, remembered pods reconnect straight to their address with no scan, so the 20-second power-on target doesn't depend on discovery. The driver scans when a number is free, or when a remembered pod has been missing for more than 10 s, because its advertising data might have changed. Data from a known address is updated and saved again if it changes. Holding the button for 1.5 s (rescan) clears the store.

### Phone mode

`PhoneServer` starts the open AP `PodHub` (192.168.4.1, at most 4 clients) and a DNS server that answers every name with the hub's IP. Every unknown URL, including iOS `/hotspot-detect.html` and Android `/generate_204`, redirects to `/`, which makes phones show the captive-portal sheet. AsyncTCP runs on core 1 with a 6 KB stack. `/api/status` returns JSON built with `snprintf`.

### Drill engine

`DrillEngine` is a non-blocking state machine (Idle, Countdown, Running, Finished). It's driven by `tick(now)` and `onTap(tap, now)` and reports what happened through `pollEvent()`. It lights pods with "off on tap" and scores the pod-measured ms. A lit pod that disconnects doesn't count as a miss. Random picks never repeat the previous pod. Time is passed in, so the tests drive it deterministically.

### Rounds, decoys and the shot log

Each round has one target and, for Colour call and Left/right, decoys (`RoundMode`). Hitting the target scores the pod-measured ms. Hitting a decoy is a mistake and ends the round. Every outcome is appended to `DrillResult::shots` (up to 400). Left/right reads `DrillConfig::leftMask`, which comes from `Settings::leftMask` (0 means pods 1 to n/2 are left). The setup screen lights the pods in the cue colours and toggles a pod's side when it's tapped (`DialUi::onTap`).

### History storage

- **File:** `/sessions.jsonl` on the LittleFS `spiffs` partition (1.5 MB), one line per session, written by hand to avoid a large buffer. Fields: id, time (from the RTC, empty until a phone has set it), player and name at the time, drill id and name, seconds, demo, aborted, pb, hits, misses, mistakes, avg, best, worst, durMs, and shots: `"1h321 3m 2d 4h287"`, which is pod (1-based), `h`it with ms, `m`iss, or `d`ecoy.
- **In RAM:** one summary per line for `/api/history`. Once the file reaches 220 lines it's rewritten with the newest 200.
- **Locking:** one mutex covers every file access. Saves happen in `loop()`; history and CSV are served from the web task.
- **`/api/export.csv?player=N`** writes `/export.csv` (one row per session, with hit times separated by `;` and the raw shots) and serves it as a download.

### Knob

The ISR decodes full quadrature on GPIO41/40 (same pins as M5Stack's M5Dial library). `Board::kCountsPerClick = 4`. If one detent moves two menu items or needs two clicks, change it to 2.

### Power

The Dial has no PMIC or battery ADC (M5Unified configures neither for the Dial), so no battery level. `Board::powerOff()` → `M5.Power.powerOff()` drives GPIO46 (power hold) low, which cuts the power on battery. On USB it falls through to deep sleep.

### Phone page

- `data/index.html` is the whole UI, with no external resources. `scripts/embed_web.py` (a PlatformIO pre-script) gzips it into the generated `src/web/index_html_gz.h`, which is gitignored. It's served from flash with `Content-Encoding: gzip`, so an OTA firmware update also updates the page.
- **Protocol:** the hub pushes `{"t":"state", ...}` on every change, about once a second during a drill and every 2 s otherwise. The page sends `{"cmd": "hello"|"start"|"stop"|"repeat"|"done"|"drill"|"seconds"|"player"|"rename"|"flash", ...}`.
- **Threading:** commands are parsed in the AsyncTCP task and queued (`WebCmd`). `loop()` applies them through `DialUi`, so the Dial and the phone always agree.
- **Clock:** `hello` carries the phone's local time, which sets the Dial's RTC (session timestamps for milestone 5).
- **Captive portal:** a connectivity check from a phone that hasn't opened the page gets a redirect, which opens the portal. Once a phone has opened the page (its IP sent `hello`), its checks get the normal "online" answer: Apple `Success`, Android `204`, Windows `Connect Test`. iOS then shows "Done" instead of "Cancel" and stays on the network.
- **Test without hardware:** `(cd tools/page_test && npm install) && node tools/page_test/run.js data/index.html /tmp/shots`

### Versions, releases and the installer

- **Version:** `scripts/version.py` stamps `PODHUB_VERSION` from the `PODHUB_VERSION` environment variable (CI tag builds) or `git describe`. It's shown in the Dial menu title and on the phone's Settings screen.
- **Release:** push a tag such as `v1.0.0` and `.github/workflows/release.yml` will:
  - run the tests and build;
  - attach `podhub-install.bin` (merged image at 0x0) and `podhub-update.bin` (app only, for updates over Wi-Fi) to a GitHub Release;
  - deploy the installer site to GitHub Pages.

  The site can also be redeployed with **Actions → release → Run workflow**. Pages must be set to **Source: GitHub Actions** (Settings → Pages).
- **Installer site:** `scripts/make_site.py <build> <out> <version>` builds it from `installer/index.html`. The ESP Web Tools manifest flashes the bootloader (0x0), partitions (0x8000), boot_app0 (0xE000) and app (0x10000) separately, not the merged image, so a USB reinstall without "Erase device" keeps NVS (pods, players, settings). LittleFS history is outside the written range either way. ESP Web Tools is pinned to `esp-web-tools@10.4.0` on unpkg.
- **Update over Wi-Fi:** `POST /api/update` (multipart), handled in `PhoneServer`. Before anything is written, the first 64 bytes must show an ESP image (0xE9), chip id 0x0009 (ESP32-S3), and the `esp_app_desc_t` magic 0xABCD5432 at offset 32. That magic is only present in app images, which rejects the merged installer image and the bootloader; both cases were checked against real build outputs. Updates are refused while a drill runs. The Dial shows progress, and `loop()` restarts 1.5 s after success so the phone gets its reply first. Writes use the second OTA slot (`default_8MB.csv` has two 3.2 MB app slots).

### Settings over Wi-Fi

`{"cmd":"set","key":...,"value":...}` with keys `sound`, `bright` (0-3), `autoOff` (minutes, 0 = never), `pods` (1-6, restarts), `demo` (0/1, restarts). `eraseHistory` deletes the sessions file and PBs. `factoryReset` clears the NVS namespaces `settings`, `pods`, `players` and `pb`, formats LittleFS, and restarts.

### Limits to keep in mind

- **Controller:** the precompiled ESP32-S3 BLE controller in arduino-esp32 2.x has `CONFIG_BT_CTRL_BLE_MAX_ACT=6`, which counts connections plus scanning. 4 pods is fine. With 6 pods, scanning has to stop once all 6 are connected (the driver already does this). A 7th activity would need a custom sdkconfig build.
- **NimBLE-Arduino** defaults to 3 connections. `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=8` in `platformio.ini` raises it.
- **No PSRAM** on the Dial's StampS3. Budget for internal RAM only.
