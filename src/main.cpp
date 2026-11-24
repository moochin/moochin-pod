// Pod Hub, milestone 1 spike.
//
// Proves the BlazePod protocol works on the Dial: scan, connect, unlock,
// set colour, receive taps with the pod-measured ms. Everything is logged
// on USB serial at 115200.
//
//   - Pods connect by themselves and flash their number colour.
//   - Press the Dial: a random connected pod lights white. Tap it.
//   - The pod's own reaction time is logged and shown on screen.
#include <Arduino.h>
#include <esp_heap_caps.h>

#include "board/Board.h"
#include "pods/BlazePodDriver.h"

static const char* TAG = "hub";

// Pete has 4 pods; the driver supports up to 6.
static constexpr uint8_t kExpectedPods = 4;

// Each pod's identity colour, used when it connects (pod 1 = red, ...).
static const Rgb kPodColours[BlazePodDriver::kMaxSlots] = {
    {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 180, 0}, {200, 0, 255}, {0, 220, 255}};

static Board board;
static BlazePodDriver pods;

static PodStatus lastStatus[BlazePodDriver::kMaxSlots];
static uint32_t identOffAt[BlazePodDriver::kMaxSlots];
static int8_t litPod = -1;
static uint32_t litAt = 0;
static uint32_t lastTapMs = 0;
static int8_t lastTapPod = -1;
static bool screenDirty = true;

static uint16_t toPanel(Rgb c) { return M5.Display.color565(c.r, c.g, c.b); }

static void drawScreen() {
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
  d.drawString("Pod Hub spike", cx, cy - 52);
  char buf[32];
  snprintf(buf, sizeof(buf), "%u/%u connected", pods.connectedCount(), pods.maxPods());
  d.drawString(buf, cx, cy - 32);

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
  d.endWrite();
}

static void logHeap() {
  ESP_LOGI(TAG, "heap free=%u min=%u largest=%u | pods %u/%u", (unsigned)ESP.getFreeHeap(),
           (unsigned)ESP.getMinFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
           pods.connectedCount(), pods.maxPods());
}

void setup() {
  board.begin();
  delay(300);  // let USB serial enumerate so the first lines aren't lost
  ESP_LOGI(TAG, "Pod Hub spike starting. PSRAM: %u bytes", (unsigned)ESP.getPsramSize());
  logHeap();
  for (auto& s : lastStatus) s = PodStatus::Empty;
  pods.begin(kExpectedPods);
}

void loop() {
  board.update();
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

  // Press: light a random connected pod (not the same one twice in a row).
  if (board.wasPressed() && pods.connectedCount() > 0) {
    if (litPod >= 0) pods.setColour(litPod, {0, 0, 0});
    int8_t pick = -1;
    for (int tries = 0; tries < 20 && pick < 0; tries++) {
      int8_t c = random(pods.maxPods());
      if (pods.status(c) == PodStatus::Connected && (c != lastTapPod || pods.connectedCount() == 1)) pick = c;
    }
    if (pick >= 0 && pods.setColour(pick, {255, 255, 255}, true)) {
      litPod = pick;
      litAt = now;
      identOffAt[pick] = 0;
      board.beep(2400, 40);
      ESP_LOGI(TAG, "lit pod %d", pick + 1);
      screenDirty = true;
    }
  }

  if (board.wasLongPressed()) {
    ESP_LOGI(TAG, "long press: rescanning");
    pods.rescan();
  }

  // Taps.
  PodTap tap;
  while (pods.pollTap(tap)) {
    const char* kind = tap.kind == TapKind::WhileOff      ? "while-off"
                       : tap.kind == TapKind::LitStayedOn ? "lit-stayed-on"
                                                          : "lit-turned-off";
    ESP_LOGI(TAG, "TAP pod %u %s pod_ms=%lu (hub saw %lu ms after lighting)", tap.slot + 1, kind,
             (unsigned long)tap.podMs, (unsigned long)(tap.hubMillis - litAt));
    if (tap.kind != TapKind::WhileOff && tap.slot == litPod) {
      lastTapMs = tap.podMs;
      lastTapPod = tap.slot;
      litPod = -1;
      board.beep(1200, 80);
      screenDirty = true;
    }
  }

  static uint32_t lastHeapLog = 0;
  if (now - lastHeapLog > 10000) {
    lastHeapLog = now;
    logHeap();
  }

  static uint32_t lastDraw = 0;
  static uint32_t lastSearchRefresh = 0;
  if (pods.connectedCount() == 0 && now - lastSearchRefresh > 1000) {
    lastSearchRefresh = now;
    screenDirty = true;  // refresh the "heard" list while searching
  }
  if (screenDirty && now - lastDraw > 100) {
    screenDirty = false;
    lastDraw = now;
    drawScreen();
  }
  delay(5);  // yield; not part of any drill timing
}
