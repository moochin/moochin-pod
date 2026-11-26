// The 20 m multi-stage fitness test ("beep test") schedule.
//
// Standard 20 m version: level 1 runs at 8.0 km/h, level 2 at 9.0 km/h,
// then +0.5 km/h per level. Each level lasts about a minute, so the number
// of shuttles per level grows as they get faster. One shuttle is 20 m:
// shuttle time = 20 m / speed = 72 / (km/h) seconds.
// Pure functions (no Arduino), unit tested natively.
#pragma once

#include <stdint.h>

namespace beeptest {

constexpr uint8_t kLevels = 21;
constexpr uint8_t kShuttleMetres = 20;

// Shuttles in each level, 1..21.
constexpr uint8_t kShuttles[kLevels] = {7, 8, 8, 9, 9, 10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16};

inline uint8_t shuttlesInLevel(uint8_t level) {
  return level >= 1 && level <= kLevels ? kShuttles[level - 1] : 0;
}

// Speed in tenths of km/h (level 1 = 80, level 2 = 90, level 3 = 95, ...).
inline uint16_t speedTenths(uint8_t level) {
  if (level <= 1) return 80;
  return (uint16_t)(90 + 5 * (level - 2));
}

// Time for one 20 m shuttle at this level, in ms (rounded).
inline uint32_t shuttleMs(uint8_t level) {
  // 20 m / (v km/h) = 72 / v s = 720000 / (v * 10) ms
  const uint32_t v = speedTenths(level);
  return (720000u + v / 2) / v;
}

// Shuttles in all levels before `level`.
inline uint16_t shuttlesBefore(uint8_t level) {
  uint16_t n = 0;
  for (uint8_t l = 1; l < level && l <= kLevels; l++) n += kShuttles[l - 1];
  return n;
}

inline uint16_t totalShuttles() { return shuttlesBefore(kLevels + 1); }

// The score for `completed` shuttles, as level and shuttle-within-level of
// the last one completed (the usual "level 8.4" score). 0 shuttles = 1.0.
struct Score {
  uint8_t level;
  uint8_t shuttle;
};
inline Score scoreFor(uint16_t completed) {
  if (completed == 0) return {1, 0};
  uint16_t left = completed;
  for (uint8_t l = 1; l <= kLevels; l++) {
    const uint8_t n = kShuttles[l - 1];
    if (left <= n) return {l, (uint8_t)left};
    left -= n;
  }
  return {kLevels, kShuttles[kLevels - 1]};
}

}  // namespace beeptest
