// Simulated pods for developing drills and UI without hardware.
// Pods "connect" one by one after start; a lit pod is "tapped" after a
// human-like 250-900 ms (sometimes not at all, to produce misses).
// No Arduino dependencies: also used by the native unit tests.
#pragma once

#include "PodDriver.h"

class MockPodDriver : public PodDriver {
 public:
  static constexpr uint8_t kMaxSlots = 6;

  explicit MockPodDriver(uint32_t seed = 12345) : rng_(seed ? seed : 1) {}

  void begin(uint8_t maxPods) override;
  uint8_t maxPods() const override { return maxPods_; }
  PodStatus status(uint8_t slot) const override;
  bool setColour(uint8_t slot, Rgb c, bool offOnTap = false) override;
  bool pollTap(PodTap& out) override;
  void rescan() override;
  const char* label(uint8_t slot) const override;
  void tick(uint32_t now) override;

  // --- simulation controls (tests and demo) ---
  // false: never tap by itself (tests inject taps directly into the engine).
  void setAutoTap(bool on) { autoTap_ = on; }
  void setReactionRange(uint32_t minMs, uint32_t maxMs) { minMs_ = minMs; maxMs_ = maxMs; }
  void setMissPercent(uint8_t p) { missPct_ = p; }
  void setConnected(uint8_t slot, bool on);
  void connectAllNow();
  // Last colour written to a pod (tests).
  Rgb colour(uint8_t slot) const { return slot < maxPods_ ? pods_[slot].colour : Rgb{0, 0, 0}; }
  bool lit(uint8_t slot) const { return slot < maxPods_ && pods_[slot].lit; }

 private:
  struct Pod {
    bool connected = false;
    uint32_t connectAt = 0;
    bool lit = false;
    bool offOnTap = false;
    Rgb colour{0, 0, 0};
    uint32_t litAt = 0;
    uint32_t tapAt = 0;  // 0 = no tap scheduled
  };
  uint32_t rand32();

  uint8_t maxPods_ = 0;
  Pod pods_[kMaxSlots];
  PodTap queue_[16];
  uint8_t qHead_ = 0, qLen_ = 0;
  uint32_t now_ = 0;
  uint32_t rng_;
  bool autoTap_ = true;
  uint32_t minMs_ = 250, maxMs_ = 900;
  uint8_t missPct_ = 10;
};
