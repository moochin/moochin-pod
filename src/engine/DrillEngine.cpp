#include "DrillEngine.h"

namespace {
bool due(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }
constexpr Rgb kOff{0, 0, 0};
constexpr uint32_t kRetryMs = 250;  // no pod available: look again soon
}  // namespace

bool DrillEngine::start(const DrillConfig& cfg, uint32_t now) {
  if (pods_.connectedCount() == 0) return false;
  cfg_ = cfg;
  result_ = DrillResult();
  lit_ = -1;
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
      if (lit_ >= 0) {
        // The lit pod vanished: not the player's fault, so no miss.
        if (pods_.status(lit_) != PodStatus::Connected) {
          lit_ = -1;
          scheduleNext(now);
        } else if (cfg_.timeoutMs && due(now, litAt_ + cfg_.timeoutMs)) {
          const uint8_t p = lit_;
          turnOffLit();
          result_.misses++;
          if (p < DrillResult::kMaxPods) result_.podMisses[p]++;
          emit(DrillEventType::Miss, p);
          if (repsDone()) {
            finish(now, false);
            break;
          }
          scheduleNext(now);
        }
      } else if (due(now, nextLightAt_)) {
        lightNext(now);
      }
      break;

    default:
      break;
  }
}

void DrillEngine::onTap(const PodTap& tap, uint32_t now) {
  if (phase_ != DrillPhase::Running) return;
  if (lit_ >= 0 && tap.slot == (uint8_t)lit_ && tap.kind != TapKind::WhileOff) {
    const uint8_t p = lit_;
    const uint32_t ms = tap.podMs;
    if (tap.kind == TapKind::LitStayedOn) pods_.setColour(p, kOff);
    lit_ = -1;
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
    emit(DrillEventType::Hit, p, ms);
    if (repsDone()) {
      finish(now, false);
      return;
    }
    scheduleNext(now);
  } else if (tap.kind == TapKind::WhileOff) {
    result_.wrongTaps++;
    emit(DrillEventType::WrongTap, tap.slot);
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
  lightNext(now);  // first light straight after "GO"
}

void DrillEngine::finish(uint32_t now, bool aborted) {
  turnOffLit();
  result_.aborted = aborted;
  result_.durationMs = phase_ == DrillPhase::Running ? now - startedAt_ : 0;
  if (!aborted && cfg_.durationMs && result_.durationMs > cfg_.durationMs) result_.durationMs = cfg_.durationMs;
  phase_ = DrillPhase::Finished;
  emit(DrillEventType::Finished);
}

void DrillEngine::lightNext(uint32_t now) {
  const int8_t p = pickPod();
  if (p < 0 || !pods_.setColour(p, cfg_.colour, true)) {
    nextLightAt_ = now + kRetryMs;
    return;
  }
  lit_ = p;
  lastPod_ = p;
  litAt_ = now;
  emit(DrillEventType::Lit, p);
}

void DrillEngine::scheduleNext(uint32_t now) {
  uint32_t span = cfg_.delayMaxMs > cfg_.delayMinMs ? cfg_.delayMaxMs - cfg_.delayMinMs : 0;
  nextLightAt_ = now + cfg_.delayMinMs + (span ? rand32() % (span + 1) : 0);
}

void DrillEngine::turnOffLit() {
  if (lit_ >= 0) pods_.setColour(lit_, kOff);
  lit_ = -1;
}

int8_t DrillEngine::pickPod() {
  const uint8_t n = pods_.maxPods();
  if (cfg_.pick == PickMode::Sequence) {
    for (uint8_t step = 1; step <= n; step++) {
      const uint8_t p = (uint8_t)((lastPod_ + step + n) % n);
      if (pods_.status(p) == PodStatus::Connected) return p;
    }
    return -1;
  }
  // Random among connected pods, never the same pod twice in a row unless
  // it's the only one.
  int8_t options[DrillResult::kMaxPods];
  uint8_t count = 0;
  for (uint8_t p = 0; p < n && p < DrillResult::kMaxPods; p++)
    if (pods_.status(p) == PodStatus::Connected && p != lastPod_) options[count++] = p;
  if (count == 0) {
    if (lastPod_ >= 0 && pods_.status(lastPod_) == PodStatus::Connected) return lastPod_;
    return -1;
  }
  return options[rand32() % count];
}

bool DrillEngine::repsDone() const {
  return cfg_.reps && (uint32_t)(result_.hits + result_.misses) >= cfg_.reps;
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
