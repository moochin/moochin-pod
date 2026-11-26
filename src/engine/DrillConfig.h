// A drill is just data. The engine interprets it.
#pragma once

#include <stdint.h>

#include "../pods/PodDriver.h"

enum class PickMode : uint8_t {
  Random,    // random pod, never the same one twice in a row
  Sequence,  // pods in order 1, 2, 3... then round again
};

// How a session is judged a personal best.
enum class PbMetric : uint8_t {
  LowestAverage,  // faster average reaction (needs a minimum number of hits)
  MostHits,       // more hits in the same time
};

enum class DrillIcon : uint8_t { Target, Sprint, Sequence };

struct DrillConfig {
  const char* id;    // stable key for personal bests, e.g. "reaction"
  const char* name;  // shown on the Dial
  DrillIcon icon;
  uint32_t durationMs;  // time-based drill length (0 = use `reps`)
  uint16_t reps;        // rep-based drill: lights in total (0 = time-based)
  uint32_t delayMinMs;  // gap after a hit/miss before the next light
  uint32_t delayMaxMs;
  uint32_t timeoutMs;   // per light; not hit in time = miss (0 = wait forever)
  PickMode pick;
  Rgb colour;
  PbMetric pb;
};
