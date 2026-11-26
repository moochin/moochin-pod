// The drill catalogue. Defaults are for a U12-U14 player; the drill length
// comes from Settings so it can be changed on the Dial or phone.
#pragma once

#include "DrillConfig.h"

namespace drills {

// Reaction (default): one random pod, hit it, the next lights after a
// random 0.5-2 s. Never the same pod twice in a row. 3 s to hit or it's a miss.
inline DrillConfig reaction(uint32_t durationMs) {
  return DrillConfig{"reaction", "Reaction", DrillIcon::Target, durationMs, 0, 500, 2000, 3000,
                     PickMode::Random, Rgb{255, 255, 255}, PbMetric::LowestAverage};
}

// Sprint count: as many hits as possible, the next pod lights immediately.
inline DrillConfig sprint(uint32_t durationMs) {
  return DrillConfig{"sprint", "Sprint", DrillIcon::Sprint, durationMs, 0, 0, 0, 3000,
                     PickMode::Random, Rgb{0, 255, 0}, PbMetric::MostHits};
}

// Fixed sequence: pods light in order 1, 2, 3... for conditioning circuits.
inline DrillConfig sequence(uint32_t durationMs) {
  return DrillConfig{"sequence", "Sequence", DrillIcon::Sequence, durationMs, 0, 0, 0, 3000,
                     PickMode::Sequence, Rgb{255, 140, 0}, PbMetric::MostHits};
}

// Colour call: green and red light together. Hit green; red is a decoy and
// counts as a mistake. Simulates reading the defence.
inline DrillConfig colourCall(uint32_t durationMs) {
  DrillConfig c{"colour", "Colour call", DrillIcon::ColourCall, durationMs, 0, 500, 2000, 3000,
                PickMode::Random, Rgb{0, 255, 0}, PbMetric::LowestAverage};
  c.mode = RoundMode::ColourCall;
  c.decoyColour = Rgb{255, 0, 0};
  return c;
}

// Left / right: one pod on each side lights. Blue = go left, yellow = go
// right. `leftMask` says which pods are on the left (set up on the Dial).
inline DrillConfig leftRight(uint32_t durationMs, uint8_t leftMask) {
  DrillConfig c{"leftright", "Left / right", DrillIcon::LeftRight, durationMs, 0, 500, 2000, 3000,
                PickMode::Random, kCueLeft, PbMetric::LowestAverage};
  c.mode = RoundMode::LeftRight;
  c.leftMask = leftMask;
  return c;
}

// Beep test: two pods 20 m apart. At each beep the pod to run to lights;
// it gets faster every level. taps = true: tap the lit pod to count each
// shuttle, two late in a row ends it. taps = false: group mode, no taps,
// press Stop when done. Score = shuttles completed (shown as level.shuttle).
inline DrillConfig beepTest(bool taps) {
  DrillConfig c{"beeptest", "Beep test", DrillIcon::BeepTest, 0, 0, 0, 0, 0,
                PickMode::Sequence, Rgb{0, 160, 255}, PbMetric::MostHits};
  c.mode = RoundMode::BeepTest;
  c.beepTaps = taps;
  return c;
}

constexpr uint8_t kCount = 6;
constexpr uint8_t kLeftRightIndex = 4;
constexpr uint8_t kBeepTestIndex = 5;

// Default split when no groups have been set: first half of the pods left.
inline uint8_t defaultLeftMask(uint8_t pods) {
  uint8_t m = 0;
  for (uint8_t i = 0; i < pods / 2; i++) m |= (uint8_t)(1u << i);
  return m ? m : 1;
}

inline DrillConfig byIndex(uint8_t i, uint32_t durationMs, uint8_t leftMask = 0x03, bool beepTaps = true) {
  switch (i % kCount) {
    case 5: return beepTest(beepTaps);
    case 1: return sprint(durationMs);
    case 2: return sequence(durationMs);
    case 3: return colourCall(durationMs);
    case 4: return leftRight(durationMs, leftMask);
    default: return reaction(durationMs);
  }
}

// One line for the phone page's drill tiles.
inline const char* description(uint8_t i) {
  switch (i % kCount) {
    case 1: return "As many hits as you can. Next pod lights straight away.";
    case 2: return "Pods light in order 1, 2, 3... for circuits.";
    case 3: return "Green and red light together. Hit green. Red is a decoy.";
    case 4: return "Blue = hit the left pod, yellow = hit the right pod.";
    case 5: return "20 m shuttles between two pods. Beat the beep; it gets faster every level.";
    default: return "Hit the pod that lights. Random gaps, never the same pod twice.";
  }
}

// Drills that have decoys, so "wrong pod" mistakes are worth showing.
inline bool hasDecoys(const DrillConfig& c) {
  return c.mode == RoundMode::ColourCall || c.mode == RoundMode::LeftRight;
}

}  // namespace drills
