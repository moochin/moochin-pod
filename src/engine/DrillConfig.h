// A drill is just data. The engine interprets it.
#pragma once

#include <stdint.h>

#include "../pods/PodDriver.h"

enum class PickMode : uint8_t {
  Random,    // random pod, never the same one twice in a row
  Sequence,  // pods in order 1, 2, 3... then round again
};

// What lights up each round.
enum class RoundMode : uint8_t {
  Single,      // one pod, in `colour`
  ColourCall,  // target in `colour` (green) + a decoy in `decoyColour` (red)
  LeftRight,   // one pod on each side, both in the cue colour:
               // blue = hit the LEFT one, yellow = hit the RIGHT one
  BeepTest,    // 20 m shuttles between two pods, timed by the beep test schedule
};

// How a session is judged a personal best.
enum class PbMetric : uint8_t {
  LowestAverage,  // faster average reaction (needs a minimum number of hits)
  MostHits,       // more hits in the same time
};

enum class DrillIcon : uint8_t { Target, Sprint, Sequence, ColourCall, LeftRight, BeepTest };

struct DrillConfig {
  const char* id;    // stable key for personal bests and history, e.g. "reaction"
  const char* name;  // shown on the Dial
  DrillIcon icon;
  uint32_t durationMs;  // time-based drill length (0 = use `reps`)
  uint16_t reps;        // rep-based drill: rounds in total (0 = time-based)
  uint32_t delayMinMs;  // gap after a hit/miss before the next round
  uint32_t delayMaxMs;
  uint32_t timeoutMs;   // per round; target not hit in time = miss (0 = wait forever)
  PickMode pick;
  Rgb colour;           // target colour (Single, ColourCall)
  PbMetric pb;
  RoundMode mode = RoundMode::Single;
  Rgb decoyColour = {255, 0, 0};
  uint8_t leftMask = 0;  // LeftRight: bit i set = pod i is on the left
  bool beepTaps = true;  // BeepTest: tap the pod at each end (false = group, no taps)
};

// Left/right cue colours.
constexpr Rgb kCueLeft{0, 80, 255};    // blue
constexpr Rgb kCueRight{255, 200, 0};  // yellow
