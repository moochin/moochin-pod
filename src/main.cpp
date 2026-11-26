// Pod Hub firmware: wiring only. See src/ui/DialUi.* for the on-Dial UI,
// src/engine for drills, src/pods for the pod drivers.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include "board/Board.h"
#include "engine/DrillEngine.h"
#include "engine/Drills.h"
#include "pods/BlazePodDriver.h"
#include "pods/MockPodDriver.h"
#include "settings/Settings.h"
#include "storage/Players.h"
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

// ------------------------------------------------------------------ phone page

// Everything the phone page shows, sent over the WebSocket.
static void buildState(String& out) {
  const uint32_t now = millis();
  JsonDocument doc;
  doc["t"] = "state";
  const DrillPhase ph = engine->phase();
  doc["phase"] = ph == DrillPhase::Countdown ? "countdown"
                 : ph == DrillPhase::Running ? "running"
                 : ph == DrillPhase::Finished ? "finished"
                                              : "idle";
  doc["countdown"] = engine->countdown();
  doc["drill"] = ui->selectedDrill();
  JsonArray dl = doc["drills"].to<JsonArray>();
  for (uint8_t i = 0; i < drills::kCount; i++) {
    JsonObject d = dl.add<JsonObject>();
    d["name"] = drills::byIndex(i, 0).name;
    d["desc"] = drills::description(i);
  }
  doc["seconds"] = settings.drillSeconds;
  JsonArray so = doc["secondsOptions"].to<JsonArray>();
  for (uint8_t s : Settings::kDrillSecondsOptions) so.add(s);
  doc["player"] = settings.player;
  JsonArray pl = doc["players"].to<JsonArray>();
  char name[players::kNameLen];
  for (uint8_t i = 0; i < Settings::kPlayers; i++) {
    players::name(i, name, sizeof(name));
    pl.add(name);
  }
  JsonArray pa = doc["pods"].to<JsonArray>();
  for (uint8_t i = 0; i < pods->maxPods(); i++) {
    JsonObject p = pa.add<JsonObject>();
    p["n"] = i + 1;
    p["st"] = pods->status(i) == PodStatus::Connected ? "connected" : "searching";
    p["c"] = kPodHex[i % 6];
  }
  doc["demo"] = settings.demoPods;
  const DrillResult& r = engine->result();
  if (ph == DrillPhase::Countdown || ph == DrillPhase::Running) {
    JsonObject run = doc["run"].to<JsonObject>();
    run["hits"] = r.hits;
    run["misses"] = r.misses;
    run["lastMs"] = engine->lastHitMs();
    run["avgMs"] = r.averageMs();
    run["remainingMs"] = engine->remainingMs(now);
    run["durationMs"] = engine->config().durationMs;
    run["lit"] = engine->litPod();
  } else if (ph == DrillPhase::Finished) {
    JsonObject res = doc["result"].to<JsonObject>();
    res["hits"] = r.hits;
    res["misses"] = r.misses;
    res["wrong"] = r.wrongTaps;
    res["avgMs"] = r.averageMs();
    res["bestMs"] = r.bestMs;
    res["worstMs"] = r.worstMs;
    res["aborted"] = r.aborted;
    res["pb"] = ui->lastResultWasPb();
    JsonArray rp = res["pods"].to<JsonArray>();
    for (uint8_t i = 0; i < pods->maxPods() && i < DrillResult::kMaxPods; i++) {
      JsonObject p = rp.add<JsonObject>();
      p["hits"] = r.podHits[i];
      p["misses"] = r.podMisses[i];
      p["avgMs"] = r.podAverageMs(i);
      p["bestMs"] = r.podBestMs[i];
    }
  }
  serializeJson(doc, out);
}

static void flashPod(uint8_t i, uint32_t now) {
  if (i >= pods->maxPods() || i >= 6 || ui->drillActive()) return;
  if (pods->setColour(i, kPodColours[i])) identOffAt[i] = now + 1500;
}

// The phone's clock sets the Dial's RTC (used to timestamp sessions).
static void setClockFromPhone(const uint16_t t[6]) {
  if (t[0] < 2024 || t[1] < 1 || t[1] > 12 || t[2] < 1 || t[2] > 31) return;
  m5::rtc_datetime_t dt;
  dt.date.year = t[0];
  dt.date.month = t[1];
  dt.date.date = t[2];
  dt.time.hours = t[3];
  dt.time.minutes = t[4];
  dt.time.seconds = t[5];
  M5.Rtc.setDateTime(dt);
  ESP_LOGI(TAG, "clock set from phone: %04u-%02u-%02u %02u:%02u:%02u", t[0], t[1], t[2], t[3], t[4], t[5]);
}

static void handlePhoneCommands(uint32_t now) {
  WebCmd c;
  bool changed = false;
  while (phone.pollCommand(c)) {
    changed = true;
    ui->noteActivity(now);
    switch (c.type) {
      case WebCmd::Hello: setClockFromPhone(c.time); break;
      case WebCmd::Start: ui->startSelected(now); break;
      case WebCmd::Stop: ui->stopDrill(now); break;
      case WebCmd::Done: ui->closeResults(); break;
      case WebCmd::Repeat: ui->startSelected(now); break;
      case WebCmd::Drill: if (c.value >= 0) ui->selectDrill((uint8_t)c.value); break;
      case WebCmd::Seconds: if (c.value > 0) ui->setDrillSeconds((uint8_t)c.value); break;
      case WebCmd::Player: if (c.value >= 0) ui->selectPlayer((uint8_t)c.value); break;
      case WebCmd::Flash: if (c.value >= 0) flashPod((uint8_t)c.value, now); break;
      case WebCmd::Rename:
        if (c.value >= 0 && c.value < Settings::kPlayers) {
          players::rename((uint8_t)c.value, c.text);
          ESP_LOGI(TAG, "player %d renamed to '%s'", c.value + 1, c.text);
          if (c.value == settings.player) ui->reloadPlayerName();
        }
        break;
    }
  }
  // Push state: right away after a command or a new page, fast while a
  // drill runs, otherwise every second.
  static uint32_t lastPush = 0;
  static DrillPhase lastPhase = DrillPhase::Idle;
  static uint16_t lastHits = 0, lastMisses = 0;
  if (phone.pageClients() == 0) {
    phone.takeNewClient();
    return;
  }
  const DrillPhase ph = engine->phase();
  const bool active = ph == DrillPhase::Countdown || ph == DrillPhase::Running;
  const DrillResult& r = engine->result();
  const bool eventChange = ph != lastPhase || r.hits != lastHits || r.misses != lastMisses ||
                           (ph == DrillPhase::Countdown && now - lastPush > 300);
  if (phone.takeNewClient() || changed || eventChange || now - lastPush > (active ? 1000u : 2000u)) {
    lastPush = now;
    lastPhase = ph;
    lastHits = r.hits;
    lastMisses = r.misses;
    String json;
    json.reserve(1200);
    buildState(json);
    phone.broadcast(json);
  }
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
  hooks.phoneClients = [] { return phone.clients(); };  // Wi-Fi joined counts as "in use"
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

  handlePhoneCommands(now);
  ui->loop(now);

  static uint32_t lastLog = 0;
  if (now - lastLog > 30000) {
    lastLog = now;
    logStatus();
  }
  delay(2);  // yield; drill timing uses pod-measured ms, not this loop
}
