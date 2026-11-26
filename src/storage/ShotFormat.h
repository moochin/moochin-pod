// The compact per-round log stored with each session:
//   "<pod><kind>[ms]" separated by spaces, e.g. "1h321 3m 2d 4h287"
//   kind: h = hit (with the pod-measured ms), m = missed, d = hit a decoy.
// Pure functions (no Arduino), unit tested natively.
#pragma once

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../engine/DrillEngine.h"

namespace shotfmt {

// Longest entry: " 6h65535" = 8 chars. A full session needs kBufferSize.
constexpr size_t kBufferSize = DrillResult::kMaxShots * 8 + 1;

// Writes the log for `r` into `out` (always NUL-terminated). Stops before an
// entry that wouldn't fit whole. Returns the length.
inline size_t format(const DrillResult& r, char* out, size_t n) {
  if (!n) return 0;
  size_t o = 0;
  out[0] = 0;
  for (uint16_t i = 0; i < r.shotCount; i++) {
    const Shot& s = r.shots[i];
    char e[24];
    const int len = s.kind == Shot::Hit
                        ? snprintf(e, sizeof(e), "%s%u%c%u", i ? " " : "", s.pod + 1, (char)s.kind, s.ms)
                        : snprintf(e, sizeof(e), "%s%u%c", i ? " " : "", s.pod + 1, (char)s.kind);
    if (len <= 0 || o + (size_t)len + 1 > n) break;
    memcpy(out + o, e, (size_t)len + 1);
    o += (size_t)len;
  }
  return o;
}

// Calls fn(pod1based, kind, ms) for each round in a stored log.
template <typename Fn>
inline void parse(const char* p, Fn fn) {
  while (p && *p) {
    while (*p == ' ') p++;
    unsigned pod = 0;
    while (*p >= '0' && *p <= '9') pod = pod * 10 + (unsigned)(*p++ - '0');
    const char kind = *p;
    if (kind != 'h' && kind != 'm' && kind != 'd') return;  // malformed: stop
    p++;
    unsigned ms = 0;
    if (kind == 'h')
      while (*p >= '0' && *p <= '9') ms = ms * 10 + (unsigned)(*p++ - '0');
    fn(pod, kind, ms);
  }
}

}  // namespace shotfmt
