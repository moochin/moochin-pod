#include "Results.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "../engine/Drills.h"
#include "ShotFormat.h"

static const char* TAG = "results";

namespace results {
namespace {

constexpr const char* kFile = "/sessions.jsonl";
constexpr const char* kTmp = "/sessions.tmp";
constexpr uint16_t kTrimAt = kKeep + 20;  // rewrite the file only now and then

struct Summary {
  uint32_t id;
  When when;
  uint8_t player, drill, seconds, flags;  // flags: 1 aborted, 2 pb, 4 demo
  uint16_t hits, misses, mistakes, avgMs, bestMs;
};

Summary index_[kTrimAt];
uint16_t count_ = 0;     // summaries in RAM (= lines in the file)
uint32_t nextId_ = 1;
bool mounted_ = false;
SemaphoreHandle_t mutex_ = nullptr;

struct Lock {
  Lock() { xSemaphoreTake(mutex_, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex_); }
};

uint8_t drillIndexFor(const char* id) {
  for (uint8_t i = 0; i < drills::kCount; i++)
    if (!strcmp(drills::byIndex(i, 0).id, id)) return i;
  return 0;
}

void formatWhen(const When& w, char* out, size_t n) {
  if (!w.year)
    out[0] = 0;
  else
    snprintf(out, n, "%04u-%02u-%02u %02u:%02u", w.year, w.month, w.day, w.hour, w.minute);
}

When parseWhen(const char* s) {
  When w{};
  unsigned y, mo, d, h, mi;
  if (s && sscanf(s, "%u-%u-%u %u:%u", &y, &mo, &d, &h, &mi) == 5) {
    w.year = y;
    w.month = mo;
    w.day = d;
    w.hour = h;
    w.minute = mi;
  }
  return w;
}

// Parses the summary fields of one line (the shots are skipped).
bool parseSummary(const String& line, Summary& s) {
  JsonDocument filter;
  for (const char* k : {"id", "time", "player", "drill", "sec", "aborted", "pb", "demo", "hits", "misses",
                        "mistakes", "avg", "best"})
    filter[k] = true;
  JsonDocument doc;
  if (deserializeJson(doc, line, DeserializationOption::Filter(filter))) return false;
  s.id = doc["id"] | 0;
  s.when = parseWhen(doc["time"] | "");
  s.player = doc["player"] | 0;
  s.drill = drillIndexFor(doc["drill"] | "");
  s.seconds = doc["sec"] | 0;
  s.flags = (doc["aborted"] | false ? 1 : 0) | (doc["pb"] | false ? 2 : 0) | (doc["demo"] | false ? 4 : 0);
  s.hits = doc["hits"] | 0;
  s.misses = doc["misses"] | 0;
  s.mistakes = doc["mistakes"] | 0;
  s.avgMs = doc["avg"] | 0;
  s.bestMs = doc["best"] | 0;
  return s.id != 0;
}

// Keeps the newest kKeep lines of the file (and the index). Caller holds the lock.
void trim() {
  if (count_ <= kKeep) return;
  const uint16_t drop = count_ - kKeep;
  File in = LittleFS.open(kFile, "r");
  File out = LittleFS.open(kTmp, "w");
  if (!in || !out) return;
  uint16_t line = 0;
  while (in.available()) {
    String l = in.readStringUntil('\n');
    if (l.length() < 2) continue;
    if (line++ >= drop) {
      out.print(l);
      out.print('\n');
    }
  }
  in.close();
  out.close();
  LittleFS.remove(kFile);
  LittleFS.rename(kTmp, kFile);
  memmove(index_, index_ + drop, sizeof(Summary) * kKeep);
  count_ = kKeep;
  ESP_LOGI(TAG, "history trimmed to the newest %u sessions", kKeep);
}

void csvQuoted(File& f, const char* s) {
  f.print('"');
  for (const char* p = s; *p; p++) {
    if (*p == '"') f.print('"');
    f.print(*p);
  }
  f.print('"');
}

}  // namespace

bool begin() {
  if (!mutex_) mutex_ = xSemaphoreCreateMutex();
  Lock lock;
  // Format on first use (or if the partition is corrupt): history is not
  // worth refusing to start over.
  mounted_ = LittleFS.begin(true);
  if (!mounted_) {
    ESP_LOGE(TAG, "filesystem failed to mount: history won't be saved");
    return false;
  }
  count_ = 0;
  File f = LittleFS.open(kFile, "r");
  if (f) {
    while (f.available() && count_ < kTrimAt) {
      String l = f.readStringUntil('\n');
      Summary s;
      if (l.length() > 2 && parseSummary(l, s)) {
        index_[count_++] = s;
        if (s.id >= nextId_) nextId_ = s.id + 1;
      }
    }
    f.close();
  }
  ESP_LOGI(TAG, "%u sessions in history, %u KB used of %u KB", count_, (unsigned)(LittleFS.usedBytes() / 1024),
           (unsigned)(LittleFS.totalBytes() / 1024));
  return true;
}

uint16_t count() { return count_; }

bool save(const SessionInfo& info, const DrillConfig& cfg, const DrillResult& r) {
  if (!mounted_) return false;
  char when[20];
  formatWhen(info.when, when, sizeof(when));

  Lock lock;
  File f = LittleFS.open(kFile, "a");
  if (!f) return false;
  const uint32_t id = nextId_++;
  // Written by hand (not ArduinoJson) so a long session never needs a big
  // buffer in RAM.
  f.printf("{\"id\":%lu,\"time\":\"%s\",\"player\":%u,\"name\":", (unsigned long)id, when, info.player);
  {
    // JSON-escape the player name (printable ASCII only, see PhoneServer).
    f.print('"');
    for (const char* p = info.playerName; *p; p++) {
      if (*p == '"' || *p == '\\') f.print('\\');
      f.print(*p);
    }
    f.print('"');
  }
  f.printf(",\"drill\":\"%s\",\"drillName\":\"%s\",\"sec\":%u,\"demo\":%s,\"aborted\":%s,\"pb\":%s", cfg.id,
           cfg.name, info.seconds, info.demo ? "true" : "false", r.aborted ? "true" : "false",
           info.pb ? "true" : "false");
  f.printf(",\"hits\":%u,\"misses\":%u,\"mistakes\":%u,\"avg\":%lu,\"best\":%lu,\"worst\":%lu,\"durMs\":%lu", r.hits,
           r.misses, r.mistakes, (unsigned long)r.averageMs(), (unsigned long)r.bestMs, (unsigned long)r.worstMs,
           (unsigned long)r.durationMs);
  // Every round in order: "<pod><h|m|d>[ms]", e.g. "1h321 3m 2d 4h287".
  {
    static char shots[shotfmt::kBufferSize];  // loop() only, under the lock
    shotfmt::format(r, shots, sizeof(shots));
    f.print(",\"shots\":\"");
    f.print(shots);
    f.print("\"}\n");
  }
  f.close();

  Summary s{};
  s.id = id;
  s.when = info.when;
  s.player = info.player;
  s.drill = info.drillIndex;
  s.seconds = info.seconds;
  s.flags = (r.aborted ? 1 : 0) | (info.pb ? 2 : 0) | (info.demo ? 4 : 0);
  s.hits = r.hits;
  s.misses = r.misses;
  s.mistakes = r.mistakes;
  s.avgMs = (uint16_t)r.averageMs();
  s.bestMs = (uint16_t)r.bestMs;
  // The file now has count_ + 1 lines; keep the index in step, then trim
  // both together once there are kTrimAt.
  index_[count_++] = s;
  if (count_ >= kTrimAt) trim();
  ESP_LOGI(TAG, "saved session %lu (%u in history)", (unsigned long)id, count_);
  return true;
}

void eraseAll() {
  if (!mounted_) return;
  Lock lock;
  LittleFS.remove(kFile);
  LittleFS.remove("/export.csv");
  count_ = 0;
  ESP_LOGI(TAG, "history erased");
}

void format() {
  if (!mutex_) mutex_ = xSemaphoreCreateMutex();
  Lock lock;
  LittleFS.end();
  LittleFS.format();
  mounted_ = false;
  count_ = 0;
}

void historyJson(String& out, int player, uint16_t max) {
  out = "{\"sessions\":[";
  if (mutex_) {
    Lock lock;
    uint16_t n = 0;
    char when[20], buf[200];
    for (int i = (int)count_ - 1; i >= 0 && n < max; i--) {
      const Summary& s = index_[i];
      if (player >= 0 && s.player != player) continue;
      formatWhen(s.when, when, sizeof(when));
      snprintf(buf, sizeof(buf),
               "%s{\"id\":%lu,\"time\":\"%s\",\"player\":%u,\"drill\":%u,\"sec\":%u,\"hits\":%u,\"misses\":%u,"
               "\"mistakes\":%u,\"avg\":%u,\"best\":%u,\"aborted\":%s,\"pb\":%s,\"demo\":%s}",
               n ? "," : "", (unsigned long)s.id, when, s.player, s.drill, s.seconds, s.hits, s.misses, s.mistakes,
               s.avgMs, s.bestMs, s.flags & 1 ? "true" : "false", s.flags & 2 ? "true" : "false",
               s.flags & 4 ? "true" : "false");
      out += buf;
      n++;
    }
  }
  out += "]}";
}

bool exportCsv(const char* path, int player) {
  if (!mounted_) return false;
  Lock lock;
  File in = LittleFS.open(kFile, "r");
  File out = LittleFS.open(path, "w");
  if (!out) return false;
  out.print("date,player,drill,seconds,hits,missed,wrong_pod,average_ms,best_ms,slowest_ms,personal_best,"
            "stopped_early,demo,reaction_times_ms,rounds\n");
  if (in) {
    while (in.available()) {
      String l = in.readStringUntil('\n');
      if (l.length() < 2) continue;
      JsonDocument doc;
      if (deserializeJson(doc, l)) continue;
      if (player >= 0 && (doc["player"] | -1) != player) continue;
      csvQuoted(out, doc["time"] | "");
      out.print(',');
      csvQuoted(out, doc["name"] | "");
      out.print(',');
      csvQuoted(out, doc["drillName"] | "");
      out.printf(",%u,%u,%u,%u,%lu,%lu,%lu,%s,%s,%s,", (unsigned)(doc["sec"] | 0), (unsigned)(doc["hits"] | 0),
                 (unsigned)(doc["misses"] | 0), (unsigned)(doc["mistakes"] | 0), (unsigned long)(doc["avg"] | 0),
                 (unsigned long)(doc["best"] | 0), (unsigned long)(doc["worst"] | 0),
                 doc["pb"] | false ? "yes" : "no", doc["aborted"] | false ? "yes" : "no",
                 doc["demo"] | false ? "yes" : "no");
      // Hit times only, separated by ';' (spreadsheet-friendly).
      const char* shots = doc["shots"] | "";
      out.print('"');
      bool first = true;
      shotfmt::parse(shots, [&](unsigned, char kind, unsigned ms) {
        if (kind != 'h') return;
        out.printf("%s%u", first ? "" : ";", ms);
        first = false;
      });
      out.print("\",");
      csvQuoted(out, shots);
      out.print('\n');
    }
    in.close();
  }
  out.close();
  return true;
}

}  // namespace results
