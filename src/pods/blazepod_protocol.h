// BlazePod BLE wire protocol (unofficial, reverse-engineered).
//
// Sources, checked line by line:
//   https://github.com/sasodoma/blazepod-hacking  (main.c: unlock checksum, readme: UUIDs/payloads)
//   https://github.com/Snerls/blazepod            (protocol.py / protocol.js + tests)
//
// Pure functions only: no Arduino or NimBLE headers, so this compiles and is
// unit tested natively (`pio test -e native`).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace blazepod {

// Nordic-UART-style service. Unlock payload is written to RX.
constexpr const char* kUartServiceUuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* kUartRxCharUuid  = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
// Colour: write [G, B, R] (+ optional 0x01 = turn off when tapped).
constexpr const char* kColourServiceUuid = "50c97bfa-4cb8-4c84-b745-0e58a0280cd6";
constexpr const char* kColourCharUuid    = "50c912a2-4cb8-4c84-b745-0e58a0280cd6";
// Tap notifications: 8 bytes [state, ms LE u32, ?, ?, ?].
constexpr const char* kTapServiceUuid = "50c928bd-4cb8-4c84-b745-0e58a0280cd6";
constexpr const char* kTapCharUuid    = "50c9727e-4cb8-4c84-b745-0e58a0280cd6";

// Pods advertise with this local-name prefix (both reference ports filter on it).
constexpr const char* kNamePrefix = "BlazePod";

constexpr size_t kAuthPayloadLen = 7;
constexpr size_t kMinMfrDataLen = 5;

// Computes the 7-byte unlock payload: "sea" + 4-byte checksum (little endian).
// `mfr` is the manufacturer-specific advertising data. Only the LAST 5 bytes
// are used, so it may or may not include the 2-byte company ID (NimBLE
// includes it, Bleak strips it; the result is the same).
// Returns false if `mfr` is shorter than 5 bytes.
bool buildAuthPayload(const uint8_t* mfr, size_t len, uint8_t out[kAuthPayloadLen]);

// Encodes a colour for the colour characteristic. Writes 3 or 4 bytes to
// `out` (wire order G, B, R) and returns the number of bytes written.
size_t encodeColour(uint8_t r, uint8_t g, uint8_t b, bool offOnTap, uint8_t out[4]);

enum class TapState : uint8_t {
  WhileOff = 0x00,        // tapped a pod that was off
  LitStayedOn = 0x21,     // tapped a lit pod, it stayed lit
  LitTurnedOff = 0x25,    // tapped a lit pod, it turned off
};

struct Tap {
  TapState state;
  uint32_t elapsedMs;  // ms since the pod was lit, measured by the pod itself
  bool hitLitPod() const { return state != TapState::WhileOff; }
};

enum class TapParse : uint8_t { Ok, TooShort, UnknownState };

// Parses a tap notification. Bytes 5..7 are unknown and ignored.
TapParse parseTap(const uint8_t* data, size_t len, Tap& out);

// True if an advertisement looks like a BlazePod: it has usable
// manufacturer data (needed to unlock) AND either the name starts with
// "BlazePod" (any case) or it advertises one of the pod's own services.
bool looksLikePod(const char* name, size_t mfrLen, bool advertisesPodService = false);

}  // namespace blazepod
