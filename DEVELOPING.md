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
  main.cpp                 wiring + milestone 1 spike UI
  board/Board.h            M5Unified wrapper (display, press, buzzer)
  pods/PodDriver.h         hardware-agnostic pod interface (BLE now, ESP-NOW later)
  pods/BlazePodDriver.*    NimBLE central implementation
  pods/blazepod_protocol.* pure protocol functions (unit tested)
test/test_protocol/        Unity tests run natively
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

### Limits to keep in mind

- **Controller:** the precompiled ESP32-S3 BLE controller in arduino-esp32 2.x has `CONFIG_BT_CTRL_BLE_MAX_ACT=6`, which counts connections plus scanning. 4 pods is fine. With 6 pods, scanning has to stop once all 6 are connected (the driver already does this). A 7th activity would need a custom sdkconfig build.
- **NimBLE-Arduino** defaults to 3 connections. `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=8` in `platformio.ini` raises it.
- **No PSRAM** on the Dial's StampS3. Budget for internal RAM only.
