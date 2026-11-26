// Pod Hub firmware: wiring only. See src/ui/DialUi.* for the on-Dial UI,
// src/engine for drills, src/pods for the pod drivers.
#include <Arduino.h>
#include <esp_heap_caps.h>

#include "board/Board.h"
#include "engine/DrillEngine.h"
#include "pods/BlazePodDriver.h"
#include "pods/MockPodDriver.h"
#include "settings/Settings.h"
#include "ui/DialUi.h"
#include "web/PhoneServer.h"

static const char* TAG = "hub";

// Each pod's identity colour, flashed when it (re)connects (pod 1 = red, ...).
static const Rgb kPodColours[6] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255},
                                   {255, 180, 0}, {200, 0, 255}, {0, 220, 255}};
static const char* kPodHex[6] = {"#ff3b30", "#34c759", "#0a84ff", "#ffb400", "#c800ff", "#00dcff"};

static Board board;
static Settings settings;
static BlazePodDriver blePods;
static MockPodDriver mockPods(esp_random());
static PodDriver* pods = &blePods;  // chosen in setup()
static DrillEngine* engine = nullptr;
static DialUi* ui = nullptr;
static PhoneServer phone;

static PodStatus lastStatus[6];
static uint32_t identOffAt[6];

static bool usingBle() { return pods == &blePods; }

// ------------------------------------------------------------------ status JSON
// Milestone 2 status page; replaced by the full phone UI in milestone 4.

static void buildStatus(String& out) {
  char buf[200];
  snprintf(buf, sizeof(buf),
           "{\"up\":%lu,\"heap\":%u,\"minHeap\":%u,\"largest\":%u,\"allConnMs\":%lu,\"phones\":%u,"
           "\"soak\":{\"on\":false,\"lit\":0,\"hit\":0,\"miss\":0},\"pods\":[",
           (unsigned long)millis(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
           (unsigned long)(usingBle() ? blePods.allConnectedAfterMs() : 0), phone.clients());
  out += buf;
  for (uint8_t i = 0; i < pods->maxPods(); i++) {
    BlazePodDriver::PodStats st{};
    if (usingBle()) st = blePods.stats(i);
    const PodStatus s = pods->status(i);
    const char* name = s == PodStatus::Connected       ? "connected"
                       : s == PodStatus::Connecting    ? "connecting"
                       : s == PodStatus::Lost          ? "reconnecting"
                       : s == PodStatus::NotResponding ? "not responding"
                                                       : "searching";
    snprintf(buf, sizeof(buf),
             "%s{\"n\":%u,\"st\":\"%s\",\"c\":\"%s\",\"rssi\":%d,\"bat\":%d,\"connects\":%u,\"drops\":%u,"
             "\"gap\":%lu,\"worst\":%lu}",
             i ? "," : "", i + 1, name, kPodHex[i % 6], st.rssi, pods->battery(i), st.connects, st.drops,
             (unsigned long)st.lastGapMs, (unsigned long)st.longestGapMs);
    out += buf;
  }
  out += "]}";
}

static void setPhoneMode(bool on) {
  settings.phoneMode = on;
  if (on)
    phone.start(buildStatus);
  else
    phone.stop();
}

static void logStatus() {
  ESP_LOGI(TAG, "[%lus] heap free=%u min=%u largest=%u | pods %u/%u | phones %u", (unsigned long)(millis() / 1000),
           (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), pods->connectedCount(), pods->maxPods(),
           phone.clients());
}

// ------------------------------------------------------------------ main

void setup() {
  board.begin();
  delay(300);  // let USB serial enumerate so the first lines aren't lost
  settings.load();
  ESP_LOGI(TAG, "Pod Hub starting. %s pods, expecting %u, phone mode %s", settings.demoPods ? "DEMO" : "real",
           settings.expectedPods, settings.phoneMode ? "on" : "off");

  pods = settings.demoPods ? static_cast<PodDriver*>(&mockPods) : static_cast<PodDriver*>(&blePods);
  mockPods.tick(millis());
  pods->begin(settings.expectedPods);
  for (auto& s : lastStatus) s = PodStatus::Empty;

  engine = new DrillEngine(*pods, esp_random());
  DialUi::Hooks hooks;
  hooks.setPhoneMode = setPhoneMode;
  hooks.phoneOn = [] { return phone.running(); };
  hooks.phoneClients = [] { return phone.clients(); };
  hooks.restart = [] { ESP.restart(); };
  hooks.devicesHeard = [] { return usingBle() ? blePods.devicesHeard() : (uint16_t)0; };
  ui = new DialUi(board, *pods, *engine, settings, hooks);
  ui->begin();

  if (settings.phoneMode) phone.start(buildStatus);
  logStatus();
}

void loop() {
  board.update();
  phone.loop();
  const uint32_t now = millis();

  pods->tick(now);
  PodTap tap;
  while (pods->pollTap(tap)) engine->onTap(tap, now);
  engine->tick(now);

  // A pod (re)connected while no drill is running: flash its number colour
  // so you know which is which. During a drill this would confuse the player.
  for (uint8_t i = 0; i < pods->maxPods() && i < 6; i++) {
    const PodStatus st = pods->status(i);
    if (st == lastStatus[i]) continue;
    lastStatus[i] = st;
    if (st == PodStatus::Connected && !ui->drillActive()) {
      pods->setColour(i, kPodColours[i]);
      identOffAt[i] = now + 1500;
      board.beep(1800, 60);
    }
  }
  for (uint8_t i = 0; i < pods->maxPods() && i < 6; i++) {
    if (identOffAt[i] && (int32_t)(now - identOffAt[i]) >= 0) {
      identOffAt[i] = 0;
      if (engine->litPod() != (int8_t)i) pods->setColour(i, {0, 0, 0});
    }
  }

  ui->loop(now);

  static uint32_t lastLog = 0;
  if (now - lastLog > 30000) {
    lastLog = now;
    logStatus();
  }
  delay(2);  // yield; drill timing uses pod-measured ms, not this loop
}
