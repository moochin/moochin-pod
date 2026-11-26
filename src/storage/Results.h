// Session history on LittleFS.
//
// /sessions.jsonl holds one JSON line per session: when, who, which drill,
// the totals and every round ("shots": hits with the pod-measured ms,
// misses, wrong-pod mistakes). A summary of each session is also kept in RAM
// for the phone's history screen. The newest kKeep sessions are kept.
//
// All file access goes through one mutex: sessions are saved from loop(),
// history and CSV are read from the web server task.
#pragma once

#include <Arduino.h>

#include "../engine/DrillEngine.h"

namespace results {

constexpr uint16_t kKeep = 200;

struct When {
  uint16_t year;  // 0 = clock not set yet
  uint8_t month, day, hour, minute;
};

struct SessionInfo {
  When when;
  uint8_t player;
  const char* playerName;
  uint8_t drillIndex;
  uint8_t seconds;
  bool pb;
  bool demo;
};

bool begin();  // mounts the filesystem and loads the summaries
bool save(const SessionInfo& info, const DrillConfig& cfg, const DrillResult& r);
void eraseAll();  // deletes every saved session
// Factory reset: wipes the whole filesystem.
void format();
uint16_t count();

// Newest first, at most `max`, only `player` (or everyone if player < 0).
void historyJson(String& out, int player, uint16_t max);

// Writes a CSV (one row per session) to `path`. Returns false on failure.
bool exportCsv(const char* path, int player);

}  // namespace results
