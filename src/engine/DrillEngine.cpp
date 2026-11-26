#include "DrillEngine.h"

#include <new>

namespace {
bool due(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }
constexpr Rgb kOff{0, 0, 0};
constexpr uint32_t kRetryMs = 250;  // no pod available: look again soon
uint8_t bit(uint8_t p) { return (uint8_t)(1u << p); }
}  // namespace

bool DrillEngine::start(const DrillConfig& cfg, uint32_t now) {
  if (pods_.connectedCount() == 0) return false;
  cfg_ = cfg;
  result_.~DrillResult();  // reset in place (it's big; avoid a stack temporary)
  new (&result_) DrillResult();
  target_ = -1;
  litMask_ = 0;
  lastPod_ = -1;
  lastHitMs_ = 0;
  evLen_ = 0;
  phase_ = DrillPhase::Countdown;
  countdownN_ = 3;
  countdownAt_ = now;
  emit(DrillEventType::CountdownBeat, 0, 0, countdownN_);
  return true;
}

void DrillEngine::stop(uint32_t now) {
  if (phase_ == DrillPhase::Countdown || phase_ == DrillPhase::Running) finish(now, true);
}

void DrillEngine::tick(uint32_t now) {
  switch (phase_) {
    case DrillPhase::Countdown:
      if (due(now, countdownAt_ + kCountdownStepMs)) {
        countdownAt_ += kCountdownStepMs;
        if (--countdownN_ == 0) {
          beginRunning(now);
        } else {
          emit(DrillEventType::CountdownBeat, 0, 0, countdownN_);
        }
      }
      break;

    case DrillPhase::Running:
      if (cfg_.durationMs && due(now, endsAt_)) {
        finish(now, false);
        break;
      }
      if (target_ >= 0) {
        // The target vanished: not the player's fault, so no miss.
        if (pods_.status(target_) != PodStatus::Connected) {
          endRound();
          scheduleNext(now);
        } else if (cfg_.timeoutMs && due(now, litAt_ + cfg_.timeoutMs)) {
          const uint8_t p = target_;
          endRound();
          result_.misses++;
          if (p < DrillResult::kMaxPods) result_.podMisses[p]++;
          logShot(p, Shot::Miss, 0);
          emit(DrillEventType::Miss, p);
          if (repsDone()) {
            finish(now, false);
            break;
          }
          scheduleNext(now);
        }
      } else if (due(now, nextLightAt_)) {
        startRound(now);
      }
      break;

    default:
      break;
  }
}

void DrillEngine::onTap(const PodTap& tap, uint32_t now) {
  if (phase_ != DrillPhase::Running) return;
  const uint8_t p = tap.slot;
  const bool litTap = tap.kind != TapKind::WhileOff;

  if (target_ >= 0 && p == (uint8_t)target_ && litTap) {
    const uint32_t ms = tap.podMs;
    endRound();
    result_.hits++;
    result_.sumMs += ms;
    if (!result_.bestMs || ms < result_.bestMs) result_.bestMs = ms;
    if (ms > result_.worstMs) result_.worstMs = ms;
    if (p < DrillResult::kMaxPods) {
      result_.podHits[p]++;
      result_.podSumMs[p] += ms;
      if (!result_.podBestMs[p] || ms < result_.podBestMs[p]) result_.podBestMs[p] = ms;
    }
    lastHitMs_ = ms;
    logShot(p, Shot::Hit, ms);
    emit(DrillEventType::Hit, p, ms);
    if (repsDone()) {
      finish(now, false);
      return;
    }
    scheduleNext(now);
  } else if (target_ >= 0 && p < 8 && (litMask_ & bit(p)) && litTap) {
    // Hit a decoy: a wrong read. The round ends; no reaction time scored.
    endRound();
    result_.mistakes++;
    if (p < DrillResult::kMaxPods) result_.podMistakes[p]++;
    logShot(p, Shot::Mistake, 0);
    emit(DrillEventType::Mistake, p);
    if (repsDone()) {
      finish(now, false);
      return;
    }
    scheduleNext(now);
  } else if (!litTap) {
    result_.wrongTaps++;
    emit(DrillEventType::WrongTap, p);
  }
}

bool DrillEngine::pollEvent(DrillEvent& out) {
  if (!evLen_) return false;
  out = events_[evHead_];
  evHead_ = (evHead_ + 1) % (sizeof(events_) / sizeof(events_[0]));
  evLen_--;
  return true;
}

uint32_t DrillEngine::elapsedMs(uint32_t now) const {
  if (phase_ == DrillPhase::Running) return now - startedAt_;
  if (phase_ == DrillPhase::Finished) return result_.durationMs;
  return 0;
}

uint32_t DrillEngine::remainingMs(uint32_t now) const {
  if (!cfg_.durationMs) return 0;
  if (phase_ == DrillPhase::Countdown) return cfg_.durationMs;
  if (phase_ != DrillPhase::Running || due(now, endsAt_)) return 0;
  return endsAt_ - now;
}

// ------------------------------------------------------------------ private

void DrillEngine::beginRunning(uint32_t now) {
  phase_ = DrillPhase::Running;
  startedAt_ = now;
  endsAt_ = now + cfg_.durationMs;
  emit(DrillEventType::Go);
  startRound(now);  // first light straight after "GO"
}

void DrillEngine::finish(uint32_t now, bool aborted) {
  endRound();
  result_.aborted = aborted;
  result_.durationMs = phase_ == DrillPhase::Running ? now - startedAt_ : 0;
  if (!aborted && cfg_.durationMs && result_.durationMs > cfg_.durationMs) result_.durationMs = cfg_.durationMs;
  phase_ = DrillPhase::Finished;
  emit(DrillEventType::Finished);
}

void DrillEngine::startRound(uint32_t now) {
  bool ok = false;
  switch (cfg_.mode) {
    case RoundMode::Single: ok = roundSingle(cfg_.colour); break;
    case RoundMode::ColourCall: ok = roundColourCall(); break;
    case RoundMode::LeftRight: ok = roundLeftRight(); break;
  }
  if (!ok) {
    endRound();
    nextLightAt_ = now + kRetryMs;
    return;
  }
  lastPod_ = target_;
  litAt_ = now;
  emit(DrillEventType::Lit, target_);
}

bool DrillEngine::roundSingle(Rgb colour) {
  const int8_t p = pickPod(0xFF, lastPod_);
  if (p < 0 || !pods_.setColour(p, colour, true)) return false;
  target_ = p;
  litMask_ = bit(p);
  return true;
}

// Target in green, a decoy in red on another pod. With a single pod there
// is no decoy: it plays like Reaction.
bool DrillEngine::roundColourCall() {
  if (!roundSingle(cfg_.colour)) return false;
  const int8_t decoy = pickPod((uint8_t)~bit(target_), -1);
  if (decoy >= 0 && pods_.setColour(decoy, cfg_.decoyColour, true)) litMask_ |= bit(decoy);
  return true;
}

// One pod on each side lights in the cue colour. Blue = the left one is the
// target, yellow = the right one. If a side has no connected pod, that
// side's cue is used on its own.
bool DrillEngine::roundLeftRight() {
  const uint8_t conn = connectedMask();
  uint8_t left = cfg_.leftMask & conn;
  uint8_t right = (uint8_t)~cfg_.leftMask & conn;
  if (!left && !right) return false;
  bool goLeft;
  if (!left)
    goLeft = false;
  else if (!right)
    goLeft = true;
  else
    goLeft = rand32() & 1;
  const Rgb cue = goLeft ? kCueLeft : kCueRight;
  const int8_t t = pickPod(goLeft ? left : right, lastPod_);
  if (t < 0 || !pods_.setColour(t, cue, true)) return false;
  target_ = t;
  litMask_ = bit(t);
  const int8_t other = pickPod(goLeft ? right : left, -1);
  if (other >= 0 && pods_.setColour(other, cue, true)) litMask_ |= bit(other);
  return true;
}

void DrillEngine::endRound() {
  for (uint8_t p = 0; p < 8; p++)
    if (litMask_ & bit(p)) pods_.setColour(p, kOff);
  litMask_ = 0;
  target_ = -1;
}

void DrillEngine::scheduleNext(uint32_t now) {
  uint32_t span = cfg_.delayMaxMs > cfg_.delayMinMs ? cfg_.delayMaxMs - cfg_.delayMinMs : 0;
  nextLightAt_ = now + cfg_.delayMinMs + (span ? rand32() % (span + 1) : 0);
}

uint8_t DrillEngine::connectedMask() const {
  uint8_t m = 0;
  for (uint8_t p = 0; p < pods_.maxPods() && p < 8; p++)
    if (pods_.status(p) == PodStatus::Connected) m |= bit(p);
  return m;
}

// Picks a connected pod from `allowedMask`, avoiding `avoid` unless it's the
// only choice. Sequence drills go round in order instead.
int8_t DrillEngine::pickPod(uint8_t allowedMask, int8_t avoid) {
  const uint8_t n = pods_.maxPods() < 8 ? pods_.maxPods() : 8;
  const uint8_t ok = connectedMask() & allowedMask;
  if (!ok) return -1;
  if (cfg_.pick == PickMode::Sequence && cfg_.mode == RoundMode::Single) {
    for (uint8_t step = 1; step <= n; step++) {
      const uint8_t p = (uint8_t)((lastPod_ + step + n) % n);
      if (ok & bit(p)) return p;
    }
    return -1;
  }
  int8_t options[8];
  uint8_t count = 0;
  for (uint8_t p = 0; p < n; p++)
    if ((ok & bit(p)) && p != avoid) options[count++] = p;
  if (count == 0) return avoid;  // only the avoided pod is available
  return options[rand32() % count];
}

bool DrillEngine::repsDone() const {
  return cfg_.reps && (uint32_t)(result_.hits + result_.misses + result_.mistakes) >= cfg_.reps;
}

void DrillEngine::logShot(uint8_t pod, Shot::Kind kind, uint32_t ms) {
  if (result_.shotCount >= DrillResult::kMaxShots) return;
  Shot& s = result_.shots[result_.shotCount++];
  s.pod = pod;
  s.kind = kind;
  s.ms = ms > 65535 ? 65535 : (uint16_t)ms;
}

void DrillEngine::emit(DrillEventType t, uint8_t slot, uint32_t ms, uint8_t n) {
  const uint8_t cap = sizeof(events_) / sizeof(events_[0]);
  if (evLen_ == cap) {  // drop the oldest; the UI only needs recent events
    evHead_ = (evHead_ + 1) % cap;
    evLen_--;
  }
  events_[(evHead_ + evLen_) % cap] = DrillEvent{t, slot, n, ms};
  evLen_++;
}

uint32_t DrillEngine::rand32() {  // xorshift32
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return rng_;
}
