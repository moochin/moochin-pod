// Runs one drill as a non-blocking state machine.
//
// Call tick(now) every loop and feed every tap through onTap(). The engine
// lights pods through the PodDriver and reports what happened as events
// (for beeps and screen updates). No Arduino dependencies: unit tested
// natively against MockPodDriver.
//
// Each "round" lights a target pod and, for some drills, decoys. Hitting the
// target scores the pod's own reaction time (PodTap::podMs), so BLE latency
// never affects scores. Hitting a decoy is a mistake and ends the round.
#pragma once

#include <stdint.h>

#include "../pods/PodDriver.h"
#include "DrillConfig.h"

enum class DrillPhase : uint8_t { Idle, Countdown, Running, Finished };

enum class DrillEventType : uint8_t {
  CountdownBeat,  // n = 3, 2, 1
  Go,
  Lit,       // slot = target lit
  Hit,       // slot hit, ms = pod reaction time
  Miss,      // slot (target) timed out
  Mistake,   // slot = decoy that was hit
  WrongTap,  // an unlit pod was tapped
  LevelUp,   // beep test: n = new level
  Finished,
};

struct DrillEvent {
  DrillEventType type;
  uint8_t slot;
  uint8_t n;
  uint32_t ms;
};

// One entry per round outcome, for the session log.
struct Shot {
  enum Kind : uint8_t { Hit = 'h', Miss = 'm', Mistake = 'd' };
  uint8_t pod;
  Kind kind;
  uint16_t ms;  // reaction time for hits (pod-measured), else 0
};

struct DrillResult {
  static constexpr uint8_t kMaxPods = 6;
  static constexpr uint16_t kMaxShots = 400;
  uint16_t hits = 0, misses = 0, mistakes = 0, wrongTaps = 0;
  uint32_t sumMs = 0, bestMs = 0, worstMs = 0;
  uint16_t podHits[kMaxPods] = {0};
  uint16_t podMisses[kMaxPods] = {0};
  uint16_t podMistakes[kMaxPods] = {0};
  uint32_t podSumMs[kMaxPods] = {0};
  uint32_t podBestMs[kMaxPods] = {0};
  uint32_t durationMs = 0;
  bool aborted = false;
  Shot shots[kMaxShots];
  uint16_t shotCount = 0;

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
  int8_t litPod() const { return target_; }     // the target, -1 if none
  uint8_t litMask() const { return litMask_; }  // target + decoys
  uint8_t countdown() const { return countdownN_; }  // 3, 2, 1 during Countdown
  uint32_t elapsedMs(uint32_t now) const;
  uint32_t remainingMs(uint32_t now) const;  // time-based drills
  uint32_t lastHitMs() const { return lastHitMs_; }

  // Beep test progress (RoundMode::BeepTest).
  bool isBeepTest() const { return cfg_.mode == RoundMode::BeepTest; }
  uint8_t beepLevel() const { return beepLevel_; }
  uint8_t beepShuttle() const { return beepShuttle_; }    // 1-based, the shuttle being run
  uint16_t beepRun() const { return beepRun_; }           // shuttles run (completed or not)
  uint32_t shuttleRemainingMs(uint32_t now) const;
  uint32_t shuttleDurationMs() const;
  bool shuttleReached() const { return beepTapped_; }

 private:
  void beginRunning(uint32_t now);
  void finish(uint32_t now, bool aborted);
  void startRound(uint32_t now);
  void endRound();  // all lit pods off
  void scheduleNext(uint32_t now);
  bool roundSingle(Rgb colour);
  bool roundColourCall();
  bool roundLeftRight();
  int8_t pickPod(uint8_t allowedMask, int8_t avoid);
  void beepStart(uint32_t at);
  void beepShuttleStart(uint32_t at);
  void beepTick(uint32_t now);
  void beepTap(const PodTap& tap);
  uint8_t connectedMask() const;
  bool repsDone() const;
  void logShot(uint8_t pod, Shot::Kind kind, uint32_t ms);
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
  int8_t target_ = -1;
  uint8_t litMask_ = 0;
  int8_t lastPod_ = -1;
  uint32_t lastHitMs_ = 0;

  // Beep test state.
  int8_t beepEnds_[2] = {-1, -1};  // the two pods
  uint8_t beepDest_ = 0;           // index into beepEnds_: the pod being run to
  uint8_t beepLevel_ = 1;
  uint8_t beepShuttle_ = 1;
  uint16_t beepRun_ = 0;
  uint32_t shuttleStart_ = 0, shuttleEnd_ = 0;
  bool beepTapped_ = false;
  uint8_t beepWarnings_ = 0;  // in a row

  DrillEvent events_[16];
  uint8_t evHead_ = 0, evLen_ = 0;
};
