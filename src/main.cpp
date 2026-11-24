// Pod Hub, milestone 2: all pods + Wi-Fi, stability soak.
//
//   - Pods connect by themselves, keep their numbers across restarts, and
//     flash their number colour when they (re)connect.
//   - 1 press:  light a random pod white; tap it to see its reaction time.
//   - 2 presses: phone mode on/off (Wi-Fi "PodHub" + status page + QR code).
//   - 3 presses: soak test on/off (hub lights random pods by itself).
//   - Hold 1.5 s: forget all pods and search again.
// Everything is logged on USB serial at 115200.
#include <Arduino.h>
#include <esp_heap_caps.h>

#include "board/Board.h"
#include "pods/BlazePodDriver.h"
#include "settings/Settings.h"
#include "web/PhoneServer.h"

static const char* TAG = "hub";

// Each pod's identity colour, used when it connects (pod 1 = red, ...).
static const Rgb kPodColours[BlazePodDriver::kMaxSlots] = {
    {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 180, 0}, {200, 0, 255}, {0, 220, 255}};
static const char* kPodHex[BlazePodDriver::kMaxSlots] = {"#ff3b30", "#34c759", "#0a84ff",
                                                          "#ffb400", "#c800ff", "#00dcff"};

// Soak test: how long a lit pod waits for a tap, and the gap between lights.
static constexpr uint32_t kSoakTimeoutMs = 2000;
static constexpr uint32_t kSoakGapMinMs = 800, kSoakGapMaxMs = 2500;

static Board board;
static BlazePodDriver pods;
static Settings settings;
static PhoneServer phone;

enum class Screen { Home, PhoneQr };
static Screen screen = Screen::Home;
static uint32_t qrShownAt = 0;

static PodStatus lastStatus[BlazePodDriver::kMaxSlots];
static uint32_t identOffAt[BlazePodDriver::kMaxSlots];
static int8_t litPod = -1;
static uint32_t litAt = 0;
static uint32_t lastTapMs = 0;
static int8_t lastTapPod = -1;
static bool screenDirty = true;

static struct {
  bool on = false;
  uint32_t nextLightAt = 0;
  uint32_t lit = 0, hit = 0, miss = 0;
} soak;

static uint16_t toPanel(Rgb c) { return M5.Display.color565(c.r, c.g, c.b); }

static void fmtUptime(char* out, size_t n) {
  uint32_t s = millis() / 1000;
  if (s >= 3600)
    snprintf(out, n, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
             (unsigned long)(s % 60));
  else
    snprintf(out, n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

static uint16_t totalDrops() {
  uint16_t d = 0;
  for (uint8_t i = 0; i < pods.maxPods(); i++) d += pods.stats(i).drops;
  return d;
}

// ------------------------------------------------------------------ screens

// Bottom stats line, redrawn every second without clearing the whole screen.
static void drawStatsLine() {
  M5GFX& d = board.display();
  const int cx = board.width() / 2, y = board.height() / 2 + 82;
  char up[12], buf[48];
  fmtUptime(up, sizeof(up));
  snprintf(buf, sizeof(buf), "%s  drops %u  %uk", up, totalDrops(), (unsigned)(ESP.getFreeHeap() / 1024));
  d.fillRect(cx - 80, y - 7, 160, 14, TFT_BLACK);
  d.setFont(&fonts::Font0);
  d.setTextDatum(middle_center);
  d.setTextColor(TFT_LIGHTGREY);
  d.drawString(buf, cx, y);
}

static void drawHome() {
  M5GFX& d = board.display();
  const int cx = board.width() / 2, cy = board.height() / 2;
  d.startWrite();
  d.fillScreen(TFT_BLACK);

  // Pod dots around the edge: grey = searching, colour = connected.
  for (uint8_t i = 0; i < pods.maxPods(); i++) {
    float a = (-90.0f + i * 360.0f / pods.maxPods()) * DEG_TO_RAD;
    int x = cx + (int)(98 * cosf(a)), y = cy + (int)(98 * sinf(a));
    PodStatus st = pods.status(i);
    uint16_t col = st == PodStatus::Connected       ? toPanel(kPodColours[i])
                   : st == PodStatus::NotResponding ? TFT_MAROON
                                                    : TFT_DARKGREY;
    d.fillCircle(x, y, 14, col);
    if (i == litPod) d.drawCircle(x, y, 17, TFT_WHITE);
    d.setTextDatum(middle_center);
    d.setTextColor(TFT_BLACK);
    d.setFont(&fonts::Font2);
    d.drawNumber(i + 1, x, y);
  }

  d.setTextColor(TFT_WHITE);
  d.setFont(&fonts::Font2);
  char buf[40];
  snprintf(buf, sizeof(buf), "%u/%u pods%s", pods.connectedCount(), pods.maxPods(),
           phone.running() ? "  Wi-Fi" : "");
  d.drawString(buf, cx, cy - 52);
  if (soak.on) {
    d.setTextColor(TFT_YELLOW);
    snprintf(buf, sizeof(buf), "SOAK %lu hit %lu miss", (unsigned long)soak.hit, (unsigned long)soak.miss);
    d.drawString(buf, cx, cy - 34);
    d.setTextColor(TFT_WHITE);
  }

  if (lastTapPod >= 0) {
    d.setFont(&fonts::Font7);
    d.drawNumber(lastTapMs, cx, cy + 8);
    d.setFont(&fonts::Font2);
    snprintf(buf, sizeof(buf), "ms  (pod %d)", lastTapPod + 1);
    d.drawString(buf, cx, cy + 42);
  } else if (pods.connectedCount()) {
    d.drawString("Press to light a pod", cx, cy + 10);
  } else {
    // Searching: show what the Dial can hear, so a failed search is diagnosable
    // without a laptop. Pods are shown in green.
    snprintf(buf, sizeof(buf), "Searching... heard %u", pods.devicesHeard());
    d.drawString(buf, cx, cy - 10);
    BlazePodDriver::Nearby near[4];
    uint8_t n = pods.nearby(near, 4);
    d.setFont(&fonts::Font0);
    for (uint8_t k = 0; k < n; k++) {
      snprintf(buf, sizeof(buf), "%.14s %ddB m%u", near[k].name[0] ? near[k].name : "(no name)", near[k].rssi,
               near[k].mfrLen);
      d.setTextColor(near[k].isPod ? TFT_GREEN : TFT_LIGHTGREY);
      d.drawString(buf, cx, cy + 10 + k * 12);
    }
    d.setFont(&fonts::Font2);
    d.setTextColor(TFT_WHITE);
  }
  for (uint8_t i = 0; i < pods.maxPods(); i++) {
    if (pods.status(i) == PodStatus::NotResponding) {
      snprintf(buf, sizeof(buf), "Pod %u not responding", i + 1);
      d.setTextColor(TFT_ORANGE);
      d.drawString(buf, cx, cy + 62);
      break;
    }
  }
  drawStatsLine();
  d.endWrite();
}

// QR code that joins the open "PodHub" network. iPhone camera: point, tap
// the banner, and the page opens by itself (captive portal).
static void drawPhoneQr() {
  M5GFX& d = board.display();
  const int cx = board.width() / 2;
  d.startWrite();
  d.fillScreen(TFT_WHITE);  // QR needs a light quiet zone
  const int size = 130;
  d.qrcode("WIFI:T:nopass;S:PodHub;;", cx - size / 2, 46, size, 3);
  d.setTextDatum(middle_center);
  d.setTextColor(TFT_BLACK);
  d.setFont(&fonts::Font2);
  d.drawString("Scan to join PodHub", cx, 28);
  d.drawString("or open 192.168.4.1", cx, 190);
  d.setFont(&fonts::Font0);
  d.drawString("press to close", cx, 210);
  d.endWrite();
}

static void drawScreen() {
  if (screen == Screen::PhoneQr)
    drawPhoneQr();
  else
    drawHome();
}

// ------------------------------------------------------------------ status JSON

static void buildStatus(String& out) {
  char buf[200];
  snprintf(buf, sizeof(buf),
           "{\"up\":%lu,\"heap\":%u,\"minHeap\":%u,\"largest\":%u,\"allConnMs\":%lu,\"phones\":%u,"
           "\"soak\":{\"on\":%s,\"lit\":%lu,\"hit\":%lu,\"miss\":%lu},\"pods\":[",
           (unsigned long)millis(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), (unsigned long)pods.allConnectedAfterMs(),
           phone.clients(), soak.on ? "true" : "false", (unsigned long)soak.lit, (unsigned long)soak.hit,
           (unsigned long)soak.miss);
  out += buf;
  for (uint8_t i = 0; i < pods.maxPods(); i++) {
    const BlazePodDriver::PodStats st = pods.stats(i);
    const PodStatus s = pods.status(i);
    const char* name = s == PodStatus::Connected       ? "connected"
                       : s == PodStatus::Connecting    ? "connecting"
                       : s == PodStatus::Lost          ? "reconnecting"
                       : s == PodStatus::NotResponding ? "not responding"
                                                       : "searching";
    snprintf(buf, sizeof(buf),
             "%s{\"n\":%u,\"st\":\"%s\",\"c\":\"%s\",\"rssi\":%d,\"bat\":%d,\"connects\":%u,\"drops\":%u,"
             "\"gap\":%lu,\"worst\":%lu}",
             i ? "," : "", i + 1, name, kPodHex[i], st.rssi, pods.battery(i), st.connects, st.drops,
             (unsigned long)st.lastGapMs, (unsigned long)st.longestGapMs);
    out += buf;
  }
  out += "]}";
}

// ------------------------------------------------------------------ actions

static void setPhoneMode(bool on) {
  settings.phoneMode = on;
  settings.save();
  if (on) {
    phone.start(buildStatus);
    screen = Screen::PhoneQr;
    qrShownAt = millis();
  } else {
    phone.stop();
    screen = Screen::Home;
  }
  board.beep(on ? 2000 : 1000, 120);
  screenDirty = true;
}

static bool lightRandomPod() {
  if (pods.connectedCount() == 0) return false;
  if (litPod >= 0) pods.setColour(litPod, {0, 0, 0});
  int8_t pick = -1;
  for (int tries = 0; tries < 30 && pick < 0; tries++) {
    int8_t c = random(pods.maxPods());
    if (pods.status(c) == PodStatus::Connected && (c != lastTapPod || pods.connectedCount() == 1)) pick = c;
  }
  if (pick < 0 || !pods.setColour(pick, {255, 255, 255}, true)) return false;
  litPod = pick;
  litAt = millis();
  identOffAt[pick] = 0;
  board.beep(2400, 40);
  ESP_LOGI(TAG, "lit pod %d", pick + 1);
  screenDirty = true;
  return true;
}

static void logStatus() {
  char up[12];
  fmtUptime(up, sizeof(up));
  ESP_LOGI(TAG, "[%s] heap free=%u min=%u largest=%u | pods %u/%u | phones %u | soak %lu/%lu/%lu", up,
           (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), pods.connectedCount(), pods.maxPods(),
           phone.clients(), (unsigned long)soak.lit, (unsigned long)soak.hit, (unsigned long)soak.miss);
}

static void logPodStats() {
  for (uint8_t i = 0; i < pods.maxPods(); i++) {
    const BlazePodDriver::PodStats st = pods.stats(i);
    ESP_LOGI(TAG, "  pod %u: %s rssi=%d connects=%u drops=%u lastGap=%lums worstGap=%lums", i + 1,
             pods.status(i) == PodStatus::Connected ? "up  " : "DOWN", st.rssi, st.connects, st.drops,
             (unsigned long)st.lastGapMs, (unsigned long)st.longestGapMs);
  }
}

// ------------------------------------------------------------------ main

void setup() {
  board.begin();
  delay(300);  // let USB serial enumerate so the first lines aren't lost
  settings.load();
  ESP_LOGI(TAG, "Pod Hub starting. PSRAM: %u bytes, expecting %u pods, phone mode %s",
           (unsigned)ESP.getPsramSize(), settings.expectedPods, settings.phoneMode ? "on" : "off");
  logStatus();
  for (auto& s : lastStatus) s = PodStatus::Empty;
  pods.begin(settings.expectedPods);
  if (settings.phoneMode) phone.start(buildStatus);
  logStatus();
}

void loop() {
  board.update();
  phone.loop();
  const uint32_t now = millis();

  // Status changes: flash a pod's number colour when it (re)connects.
  for (uint8_t i = 0; i < pods.maxPods(); i++) {
    PodStatus st = pods.status(i);
    if (st == lastStatus[i]) continue;
    if (st == PodStatus::Connected) {
      pods.setColour(i, kPodColours[i]);
      identOffAt[i] = now + 1500;
      board.beep(1800, 60);
    }
    if (i == litPod && st != PodStatus::Connected) litPod = -1;
    lastStatus[i] = st;
    screenDirty = true;
  }
  for (uint8_t i = 0; i < pods.maxPods(); i++) {
    if (identOffAt[i] && (int32_t)(now - identOffAt[i]) >= 0) {
      identOffAt[i] = 0;
      if (i != litPod) pods.setColour(i, {0, 0, 0});
    }
  }

  // Button.
  switch (board.clicks()) {
    case 0:
      break;
    case 1:
      if (screen == Screen::PhoneQr) {
        screen = Screen::Home;
        screenDirty = true;
      } else {
        lightRandomPod();
      }
      break;
    case 2:
      setPhoneMode(!phone.running());
      break;
    default:  // 3+
      soak.on = !soak.on;
      soak.nextLightAt = now + 1000;
      ESP_LOGI(TAG, "soak test %s", soak.on ? "ON" : "OFF");
      board.beep(soak.on ? 2600 : 900, 200);
      screenDirty = true;
      break;
  }
  if (board.wasLongPressed()) {
    ESP_LOGI(TAG, "long press: forgetting pods and searching again");
    pods.rescan();
    screenDirty = true;
  }
  if (screen == Screen::PhoneQr && now - qrShownAt > 60000) {
    screen = Screen::Home;
    screenDirty = true;
  }

  // Taps.
  PodTap tap;
  while (pods.pollTap(tap)) {
    const char* kind = tap.kind == TapKind::WhileOff      ? "while-off"
                       : tap.kind == TapKind::LitStayedOn ? "lit-stayed-on"
                                                          : "lit-turned-off";
    if (tap.slot == litPod)
      ESP_LOGI(TAG, "TAP pod %u %s pod_ms=%lu (hub saw %lu ms after lighting)", tap.slot + 1, kind,
               (unsigned long)tap.podMs, (unsigned long)(tap.hubMillis - litAt));
    else
      ESP_LOGI(TAG, "TAP pod %u %s pod_ms=%lu", tap.slot + 1, kind, (unsigned long)tap.podMs);
    if (tap.kind != TapKind::WhileOff && tap.slot == litPod) {
      lastTapMs = tap.podMs;
      lastTapPod = tap.slot;
      litPod = -1;
      if (soak.on) {
        soak.hit++;
        soak.nextLightAt = now + random(kSoakGapMinMs, kSoakGapMaxMs);
      }
      board.beep(1200, 80);
      screenDirty = true;
    }
  }

  // Soak test: light random pods; untapped ones time out and count as misses.
  if (soak.on) {
    if (litPod >= 0 && now - litAt > kSoakTimeoutMs) {
      pods.setColour(litPod, {0, 0, 0});
      litPod = -1;
      soak.miss++;
      soak.nextLightAt = now + random(kSoakGapMinMs, kSoakGapMaxMs);
      screenDirty = true;
    }
    if (litPod < 0 && (int32_t)(now - soak.nextLightAt) >= 0) {
      if (lightRandomPod())
        soak.lit++;
      else
        soak.nextLightAt = now + 500;
    }
  }

  static uint32_t lastLog = 0, lastPodLog = 0;
  if (now - lastLog > 10000) {
    lastLog = now;
    logStatus();
  }
  if (now - lastPodLog > 60000) {
    lastPodLog = now;
    logPodStats();
  }

  static uint32_t lastDraw = 0, lastStatsLine = 0, lastSearchRefresh = 0;
  if (screen == Screen::Home && pods.connectedCount() == 0 && now - lastSearchRefresh > 1000) {
    lastSearchRefresh = now;
    screenDirty = true;  // refresh the "heard" list while searching
  }
  if (screenDirty && now - lastDraw > 100) {
    screenDirty = false;
    lastDraw = lastStatsLine = now;
    drawScreen();
  } else if (screen == Screen::Home && now - lastStatsLine > 1000) {
    lastStatsLine = now;
    board.display().startWrite();
    drawStatsLine();
    board.display().endWrite();
  }
  delay(2);  // yield; not part of any drill timing
}
