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
  storage/Players.h        player names (NVS)
  settings/Settings.h      NVS settings
  web/PhoneServer.*        Wi-Fi AP "PodHub", captive DNS, AsyncWebServer, WebSocket /ws
data/index.html            phone page (single file); gzipped into src/web/index_html_gz.h at build
tools/page_test/           fake hub + Playwright screenshots of every phone screen
  pods/PodDriver.h         hardware-agnostic pod interface (BLE now, ESP-NOW later)
  pods/BlazePodDriver.*    NimBLE central implementation
  pods/MockPodDriver.*     simulated pods ("Demo pods" setting + native tests)
  pods/blazepod_protocol.* pure protocol functions (unit tested)
test/test_protocol/        Unity tests: checksum, colour, tap parsing
test/test_engine/          Unity tests: drill engine against MockPodDriver
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

### Limits to keep in mind

- **Controller:** the precompiled ESP32-S3 BLE controller in arduino-esp32 2.x has `CONFIG_BT_CTRL_BLE_MAX_ACT=6`, which counts connections plus scanning. 4 pods is fine. With 6 pods, scanning has to stop once all 6 are connected (the driver already does this). A 7th activity would need a custom sdkconfig build.
- **NimBLE-Arduino** defaults to 3 connections. `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=8` in `platformio.ini` raises it.
- **No PSRAM** on the Dial's StampS3. Budget for internal RAM only.
