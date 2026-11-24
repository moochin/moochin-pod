// Hardware-agnostic pod interface. The drill engine and UI only see this.
// Implementations: BlazePodDriver (BLE). Planned: MockPodDriver,
// EspNowPodDriver (DIY pods).
#pragma once

#include <stdint.h>

struct Rgb {
  uint8_t r, g, b;
};

enum class PodStatus : uint8_t {
  Empty,        // slot has no pod assigned yet (searching)
  Connecting,   // pod found, connecting/unlocking
  Connected,    // unlocked and receiving taps
  Lost,         // was connected, dropped; reconnecting automatically
  NotResponding // connects but keeps dropping after unlock: likely new firmware
};

enum class TapKind : uint8_t { WhileOff, LitStayedOn, LitTurnedOff };

struct PodTap {
  uint8_t slot;        // 0-based pod index (shown to users as slot + 1)
  TapKind kind;
  uint32_t podMs;      // reaction time measured by the pod (ms since lit)
  uint32_t hubMillis;  // hub millis() when the notification arrived
};

class PodDriver {
 public:
  virtual ~PodDriver() = default;

  // Starts discovery/connection. `maxPods` slots are filled automatically.
  virtual void begin(uint8_t maxPods) = 0;

  virtual uint8_t maxPods() const = 0;
  virtual PodStatus status(uint8_t slot) const = 0;
  uint8_t connectedCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < maxPods(); i++) n += status(i) == PodStatus::Connected;
    return n;
  }

  // Sets a pod's colour. {0,0,0} = off. `offOnTap` makes the pod turn
  // itself off when tapped. Returns false if the pod isn't connected.
  virtual bool setColour(uint8_t slot, Rgb c, bool offOnTap = false) = 0;

  // Pops the next tap event, if any. Safe to call from loop().
  virtual bool pollTap(PodTap& out) = 0;

  // Battery percentage, or -1 if unknown/unsupported.
  virtual int8_t battery(uint8_t slot) const { (void)slot; return -1; }

  // Forget all pods and search again.
  virtual void rescan() = 0;

  // Short human-readable id for logs (e.g. BLE address).
  virtual const char* label(uint8_t slot) const = 0;
};
