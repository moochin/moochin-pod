#include "MockPodDriver.h"

void MockPodDriver::begin(uint8_t maxPods) {
  maxPods_ = maxPods > kMaxSlots ? kMaxSlots : maxPods;
  for (uint8_t i = 0; i < maxPods_; i++) {
    pods_[i] = Pod();
    pods_[i].connectAt = now_ + 600 * (i + 1);  // trickle in like real pods
  }
}

PodStatus MockPodDriver::status(uint8_t slot) const {
  if (slot >= maxPods_) return PodStatus::Empty;
  return pods_[slot].connected ? PodStatus::Connected : PodStatus::Empty;
}

bool MockPodDriver::setColour(uint8_t slot, Rgb c, bool offOnTap) {
  if (slot >= maxPods_ || !pods_[slot].connected) return false;
  Pod& p = pods_[slot];
  p.colour = c;
  const bool on = c.r || c.g || c.b;
  if (on && !p.lit) {
    p.lit = true;
    p.litAt = now_;
    p.tapAt = 0;
    if (autoTap_ && rand32() % 100 >= missPct_) {
      uint32_t span = maxMs_ > minMs_ ? maxMs_ - minMs_ : 1;
      p.tapAt = now_ + minMs_ + rand32() % span;
      if (p.tapAt == 0) p.tapAt = 1;
    }
  } else if (!on) {
    p.lit = false;
    p.tapAt = 0;
  }
  p.offOnTap = offOnTap;
  return true;
}

bool MockPodDriver::pollTap(PodTap& out) {
  if (qLen_ == 0) return false;
  out = queue_[qHead_];
  qHead_ = (qHead_ + 1) % (sizeof(queue_) / sizeof(queue_[0]));
  qLen_--;
  return true;
}

void MockPodDriver::rescan() {
  for (uint8_t i = 0; i < maxPods_; i++) {
    pods_[i] = Pod();
    pods_[i].connectAt = now_ + 600 * (i + 1);
  }
}

const char* MockPodDriver::label(uint8_t slot) const {
  static const char* names[kMaxSlots] = {"mock-1", "mock-2", "mock-3", "mock-4", "mock-5", "mock-6"};
  return slot < kMaxSlots ? names[slot] : "-";
}

void MockPodDriver::tick(uint32_t now) {
  now_ = now;
  for (uint8_t i = 0; i < maxPods_; i++) {
    Pod& p = pods_[i];
    if (!p.connected && p.connectAt && (int32_t)(now - p.connectAt) >= 0) {
      p.connected = true;
      p.connectAt = 0;
    }
    if (p.connected && p.lit && p.tapAt && (int32_t)(now - p.tapAt) >= 0) {
      PodTap t;
      t.slot = i;
      t.kind = p.offOnTap ? TapKind::LitTurnedOff : TapKind::LitStayedOn;
      t.podMs = p.tapAt - p.litAt;  // like a real pod: measured from when it lit
      t.hubMillis = now;
      p.tapAt = 0;
      if (p.offOnTap) p.lit = false;
      const uint8_t cap = sizeof(queue_) / sizeof(queue_[0]);
      if (qLen_ < cap) {
        queue_[(qHead_ + qLen_) % cap] = t;
        qLen_++;
      }
    }
  }
}

void MockPodDriver::setConnected(uint8_t slot, bool on) {
  if (slot >= maxPods_) return;
  pods_[slot].connected = on;
  pods_[slot].connectAt = 0;
  if (!on) {
    pods_[slot].lit = false;
    pods_[slot].tapAt = 0;
  }
}

void MockPodDriver::connectAllNow() {
  for (uint8_t i = 0; i < maxPods_; i++) setConnected(i, true);
}

uint32_t MockPodDriver::rand32() {  // xorshift32
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return rng_;
}
