// Personal bests per drill (and drill length), kept in NVS.
#pragma once

#include <Preferences.h>
#include <stdio.h>

#include "../engine/DrillEngine.h"

namespace pb {

// Need at least this many hits for an average to count as a PB.
constexpr uint16_t kMinHitsForAverage = 5;

// Returns true (and stores it) if `r` beats this player's best for this
// drill and length.
inline bool checkAndStore(uint8_t player, const DrillConfig& cfg, const DrillResult& r) {
  if (r.aborted || r.hits == 0) return false;
  char key[16];
  // NVS keys max 15 chars: "<id8>_<seconds>_<player>"
  snprintf(key, sizeof(key), "%.8s_%lu_%u", cfg.id, (unsigned long)(cfg.durationMs / 1000), player);
  Preferences p;
  if (!p.begin("pb", false)) return false;
  const uint32_t best = p.getUInt(key, 0);
  bool beat = false;
  uint32_t value = 0;
  if (cfg.pb == PbMetric::LowestAverage) {
    if (r.hits >= kMinHitsForAverage) {
      value = r.averageMs();
      beat = best == 0 || value < best;
    }
  } else {
    value = r.hits;
    beat = value > best;
  }
  if (beat) p.putUInt(key, value);
  p.end();
  return beat;
}

inline void clearAll() {
  Preferences p;
  if (p.begin("pb", false)) {
    p.clear();
    p.end();
  }
}

}  // namespace pb
