// Runs one drill as a non-blocking state machine.
//
// Call tick(now) every loop and feed every tap through onTap(). The engine
// lights pods through the PodDriver and reports what happened as events
// (for beeps and screen updates). No Arduino dependencies: unit tested
// natively against MockPodDriver.
//
// Reaction times are the pod's own measurement (PodTap::podMs), so BLE
// latency never affects scores.
#pragma once

#include <stdint.h>

#include "../pods/PodDriver.h"
#include "DrillConfig.h"

enum class DrillPhase : uint8_t { Idle, Countdown, Running, Finished };

enum class DrillEventType : uint8_t {
  CountdownBeat,  // n = 3, 2, 1
  Go,
  Lit,       // slot lit
  Hit,       // slot hit, ms = pod reaction time
  Miss,      // slot timed out
  WrongTap,  // an unlit pod was tapped
  Finished,
};

struct DrillEvent {
  DrillEventType type;
  uint8_t slot;
  uint8_t n;
  uint32_t ms;
};

struct DrillResult {
  static constexpr uint8_t kMaxPods = 6;
  uint16_t hits = 0, misses = 0, wrongTaps = 0;
  uint32_t sumMs = 0, bestMs = 0, worstMs = 0;
  uint16_t podHits[kMaxPods] = {0};
  uint16_t podMisses[kMaxPods] = {0};
  uint32_t podSumMs[kMaxPods] = {0};
  uint32_t podBestMs[kMaxPods] = {0};
  uint32_t durationMs = 0;
  bool aborted = false;

  uint32_t averageMs() const { return hits ? sumMs / hits : 0; }
  uint32_t podAverageMs(uint8_t p) const { return p < kMaxPods && podHits[p] ? podSumMs[p] / podHits[p] : 0; }
};

class DrillEngine {
 public:
  static constexpr uint32_t kCountdownStepMs = 1000;

  explicit DrillEngine(PodDriver& pods, uint32_t seed = 0x5eed) : pods_(pods), rng_(seed ? seed : 1) {}

  // Starts the 3-2-1 countdown. Returns false if no pod is connected.
  bool start(const DrillConfig& cfg, uint32_t now);
  // Stops early: lights off, result marked aborted, phase -> Finished.
  void stop(uint32_t now);
  // Back to Idle after the results have been shown.
  void reset() { phase_ = DrillPhase::Idle; }

  void tick(uint32_t now);
  void onTap(const PodTap& tap, uint32_t now);
  bool pollEvent(DrillEvent& out);

  DrillPhase phase() const { return phase_; }
  const DrillConfig& config() const { return cfg_; }
  const DrillResult& result() const { return result_; }
  int8_t litPod() const { return lit_; }
  uint8_t countdown() const { return countdownN_; }  // 3, 2, 1 during Countdown
  uint32_t elapsedMs(uint32_t now) const;
  uint32_t remainingMs(uint32_t now) const;  // time-based drills
  uint32_t lastHitMs() const { return lastHitMs_; }

 private:
  void beginRunning(uint32_t now);
  void finish(uint32_t now, bool aborted);
  void lightNext(uint32_t now);
  void scheduleNext(uint32_t now);
  void turnOffLit();
  int8_t pickPod();
  bool repsDone() const;
  void emit(DrillEventType t, uint8_t slot = 0, uint32_t ms = 0, uint8_t n = 0);
  uint32_t rand32();

  PodDriver& pods_;
  DrillConfig cfg_{};
  DrillPhase phase_ = DrillPhase::Idle;
  DrillResult result_;
  uint32_t rng_;

  uint32_t countdownAt_ = 0;  // when the current countdown step started
  uint8_t countdownN_ = 0;
  uint32_t startedAt_ = 0;    // start of Running
  uint32_t endsAt_ = 0;
  uint32_t nextLightAt_ = 0;
  uint32_t litAt_ = 0;
  int8_t lit_ = -1;
  int8_t lastPod_ = -1;
  uint32_t lastHitMs_ = 0;

  DrillEvent events_[16];
  uint8_t evHead_ = 0, evLen_ = 0;
};
