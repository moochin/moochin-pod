// Player profiles. Names default to "Player 1".."Player 4"; they can be
// renamed from the phone page (milestone 4). Results are logged per player.
#pragma once

#include <Preferences.h>
#include <stdio.h>

namespace players {

constexpr size_t kNameLen = 16;

inline void name(uint8_t i, char* out, size_t n) {
  char key[4] = {'n', (char)('0' + i), 0};
  Preferences p;
  out[0] = 0;
  if (p.begin("players", true)) {
    p.getString(key, out, n);
    p.end();
  }
  if (!out[0]) snprintf(out, n, "Player %u", i + 1);
}

inline void rename(uint8_t i, const char* newName) {
  char key[4] = {'n', (char)('0' + i), 0};
  Preferences p;
  if (p.begin("players", false)) {
    p.putString(key, newName);
    p.end();
  }
}

}  // namespace players
