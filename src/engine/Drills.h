// The drill catalogue. Defaults are for a U12-U14 player; the drill length
// comes from Settings so it can be changed on the Dial.
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

constexpr uint8_t kCount = 3;

// One line for the phone page's drill tiles.
inline const char* description(uint8_t i) {
  switch (i % kCount) {
    case 1: return "As many hits as you can. Next pod lights straight away.";
    case 2: return "Pods light in order 1, 2, 3... for circuits.";
    default: return "Hit the pod that lights. Random gaps, never the same pod twice.";
  }
}

inline DrillConfig byIndex(uint8_t i, uint32_t durationMs) {
  switch (i % kCount) {
    case 1: return sprint(durationMs);
    case 2: return sequence(durationMs);
    default: return reaction(durationMs);
  }
}

}  // namespace drills
