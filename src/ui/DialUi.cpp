#include "DialUi.h"

#include "../engine/Drills.h"
#include "../storage/PersonalBests.h"
#include "../storage/Players.h"

static const char* TAG = "ui";

namespace {
// Pod identity colours (pod 1 = red, 2 = green, 3 = blue, 4 = yellow, ...).
const uint8_t kPodRgb[6][3] = {{255, 59, 48}, {52, 199, 89}, {10, 132, 255},
                               {255, 180, 0}, {200, 0, 255}, {0, 220, 255}};

constexpr uint32_t kMenuIdleMs = 20000;
constexpr uint32_t kQrTimeoutMs = 60000;
constexpr uint32_t kAutoOffWarnMs = 30000;  // warn this long before switching off

const lgfx::IFont* const kSmall = &fonts::FreeSansBold9pt7b;
const lgfx::IFont* const kMedium = &fonts::FreeSansBold12pt7b;
const lgfx::IFont* const kLarge = &fonts::FreeSansBold18pt7b;
const lgfx::IFont* const kHuge = &fonts::FreeSansBold24pt7b;

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return M5.Display.color565(r, g, b); }
}  // namespace

// ------------------------------------------------------------------ public

void DialUi::begin() {
  drill_ = settings_.lastDrill % drills::kCount;
  players::name(settings_.player, playerName_, sizeof(playerName_));
  board_.setBrightnessLevel(settings_.brightness);
  board_.setSound(settings_.sound);
  lastActivityAt_ = millis();
  go(Screen::Home);
}

bool DialUi::drillActive() const {
  return engine_.phase() == DrillPhase::Countdown || engine_.phase() == DrillPhase::Running;
}

void DialUi::showPhoneQr() { go(Screen::PhoneQr); }

void DialUi::loop(uint32_t now) {
  handleInput(now);
  handleEngineEvents(now);
  handleAutoOff(now);

  // Pod status changed: redraw the dots.
  bool podsChanged = false;
  for (uint8_t i = 0; i < pods_.maxPods() && i < 6; i++) {
    const PodStatus st = pods_.status(i);
    if (st != shownStatus_[i]) {
      shownStatus_[i] = st;
      podsChanged = true;
    }
  }
  if (podsChanged && (screen_ == Screen::Home || screen_ == Screen::Running || screen_ == Screen::Countdown))
    dirty_ = true;

  if (hintUntil_ && (int32_t)(now - hintUntil_) >= 0) {
    hintUntil_ = 0;
    hint_[0] = 0;
    if (screen_ == Screen::Home) dirty_ = true;
  }

  // Timeouts back to home.
  if (screen_ == Screen::Menu && now - board_.lastInputAt() > kMenuIdleMs && now - screenAt_ > kMenuIdleMs)
    go(beforeMenu_ == Screen::Results ? Screen::Results : Screen::Home);
  if (screen_ == Screen::PhoneQr && now - screenAt_ > kQrTimeoutMs) go(Screen::Home);

  // Searching: refresh "heard N devices" now and then.
  static uint32_t lastSearchRefresh = 0;
  if (screen_ == Screen::Home && pods_.connectedCount() == 0 && now - lastSearchRefresh > 2000) {
    lastSearchRefresh = now;
    dirty_ = true;
  }

  // PB flashing on the results screen.
  if (screen_ == Screen::Results && resultsPb_ && resultsPage_ == 0 && now - pbBlinkAt_ > 400) {
    pbBlinkAt_ = now;
    pbBlink_ = !pbBlink_;
    M5GFX& d = board_.display();
    d.startWrite();
    d.setFont(kMedium);
    d.setTextDatum(middle_center);
    d.setTextColor(pbBlink_ ? TFT_YELLOW : TFT_BLACK, TFT_BLACK);
    d.drawString("PB!", board_.width() / 2, 24);
    d.endWrite();
  }

  if (dirty_) {
    dirty_ = false;
    drawFull();
  } else if (screen_ == Screen::Running && now - lastRunDraw_ >= 100) {
    updateRunning(now, false);
  }
}

// ------------------------------------------------------------------ input

void DialUi::go(Screen s) {
  screen_ = s;
  screenAt_ = millis();
  dirty_ = true;
}

void DialUi::handleInput(uint32_t now) {
  const int turn = board_.knob();
  const bool press = board_.pressed();
  const bool hold = board_.longPressed();
  if (turn || press || hold) lastActivityAt_ = now;

  switch (screen_) {
    case Screen::Home:
      if (hold) {
        beforeMenu_ = Screen::Home;
        menuSel_ = kBack;
        go(Screen::Menu);
        board_.beep(1500, 40);
      } else if (turn) {
        drill_ = (uint8_t)((drill_ + drills::kCount + (turn % drills::kCount)) % drills::kCount);
        settings_.lastDrill = drill_;
        board_.beep(3000, 8);
        dirty_ = true;
      } else if (press) {
        startDrill(now);
      }
      break;

    case Screen::Countdown:
    case Screen::Running:
      if (press || hold) {
        engine_.stop(now);  // Finished event takes us to Results
        board_.beep(600, 150);
      }
      break;

    case Screen::Results:
      if (hold) {
        beforeMenu_ = Screen::Results;
        menuSel_ = kBack;
        go(Screen::Menu);
      } else if (turn) {
        uint8_t pages = 1 + pods_.maxPods();
        resultsPage_ = (uint8_t)((resultsPage_ + pages + (turn % pages)) % pages);
        board_.beep(3000, 8);
        dirty_ = true;
      } else if (press) {
        engine_.reset();
        settings_.save();  // remembers the last drill chosen
        go(Screen::Home);
      }
      break;

    case Screen::Menu:
      if (turn) {
        menuSel_ = (uint8_t)((menuSel_ + kMenuCount + (turn % kMenuCount)) % kMenuCount);
        board_.beep(3000, 8);
        dirty_ = true;
      } else if (press) {
        activateMenuItem(now);
      }
      break;

    case Screen::ConfirmOff:
      if (press) shutDown();
      if (turn || hold) go(Screen::Menu);
      break;

    case Screen::AutoOffWarn:
      if (turn || press || hold) {
        board_.beep(1500, 60);
        go(Screen::Home);
      }
      break;

    case Screen::PhoneQr:
      if (turn || press || hold) go(Screen::Home);
      break;

    case Screen::Groups:
      if (press) endGroupSetup(true);
      if (hold) endGroupSetup(false);
      break;
  }
}

void DialUi::startDrill(uint32_t now) {
  if (pods_.connectedCount() == 0) {
    snprintf(hint_, sizeof(hint_), "No pods connected yet");
    hintUntil_ = now + 2500;
    board_.play({{400, 120}, {0, 60}, {300, 160}});
    dirty_ = true;
    return;
  }
  const DrillConfig cfg = currentDrill();
  if (engine_.start(cfg, now)) {
    resultsPage_ = 0;
    resultsPb_ = false;
    ESP_LOGI(TAG, "start %s, %u s, player %u (%s)", cfg.name, settings_.drillSeconds, settings_.player + 1,
             playerName_);
    go(Screen::Countdown);
  }
}

DrillConfig DialUi::currentDrill() const {
  return drills::byIndex(drill_, (uint32_t)settings_.drillSeconds * 1000, leftMask());
}

uint8_t DialUi::leftMask() const {
  return settings_.leftMask ? settings_.leftMask : drills::defaultLeftMask(pods_.maxPods());
}

// ------------------------------------------------------------------ engine

void DialUi::handleEngineEvents(uint32_t now) {
  DrillEvent ev;
  while (engine_.pollEvent(ev)) {
    switch (ev.type) {
      case DrillEventType::CountdownBeat:
        board_.beep(880, 150);
        if (screen_ == Screen::Countdown) dirty_ = true;
        break;
      case DrillEventType::Go:
        board_.beep(1760, 350);
        go(Screen::Running);
        break;
      case DrillEventType::Lit:
        // Short beep: helps when the lit pod is behind the player.
        board_.play({{2400, 40}}, false);
        break;
      case DrillEventType::Hit:
        ESP_LOGI(TAG, "hit pod %u %lu ms", ev.slot + 1, (unsigned long)ev.ms);
        updateRunning(now, true);
        break;
      case DrillEventType::Miss:
        ESP_LOGI(TAG, "miss pod %u", ev.slot + 1);
        board_.play({{440, 80}, {0, 50}, {440, 80}});
        updateRunning(now, true);
        break;
      case DrillEventType::Mistake:
        ESP_LOGI(TAG, "wrong pod %u (decoy)", ev.slot + 1);
        board_.play({{220, 300}});  // low buzz: wrong read
        updateRunning(now, true);
        break;
      case DrillEventType::WrongTap:
        break;
      case DrillEventType::Finished: {
        const DrillResult& r = engine_.result();
        // Demo pods never set personal bests.
        resultsPb_ = !settings_.demoPods && pb::checkAndStore(settings_.player, engine_.config(), r);
        ESP_LOGI(TAG, "finished %s%s: hits=%u misses=%u wrong=%u avg=%lu best=%lu worst=%lu%s",
                 engine_.config().name, r.aborted ? " (stopped)" : "", r.hits, r.misses, r.wrongTaps,
                 (unsigned long)r.averageMs(), (unsigned long)r.bestMs, (unsigned long)r.worstMs,
                 resultsPb_ ? "  NEW PB" : "");
        if (hooks_.sessionFinished) hooks_.sessionFinished(engine_.config(), r, resultsPb_);
        if (resultsPb_)
          board_.play({{800, 90}, {1000, 90}, {1300, 90}, {1800, 200}, {0, 80}, {1800, 90}, {2200, 300}});
        else if (!r.aborted)
          board_.play({{800, 90}, {1000, 90}, {1300, 90}, {1800, 250}});
        resultsPage_ = 0;
        pbBlink_ = false;
        lastActivityAt_ = now;
        go(Screen::Results);
        break;
      }
    }
  }
}

// ------------------------------------------------------------------ menu

void DialUi::menuLabel(uint8_t item, char* label, size_t ln, char* value, size_t vn) {
  value[0] = 0;
  switch (item) {
    case kBack: snprintf(label, ln, "Back"); break;
    case kPlayer:
      snprintf(label, ln, "Player");
      snprintf(value, vn, "%s", playerName_);
      break;
    case kDrillLength:
      snprintf(label, ln, "Drill length");
      snprintf(value, vn, "%u seconds", settings_.drillSeconds);
      break;
    case kGroups: {
      snprintf(label, ln, "Left/right pods");
      const uint8_t m = leftMask();
      size_t o = snprintf(value, vn, "L:");
      for (uint8_t i = 0; i < pods_.maxPods() && o + 3 < vn; i++)
        if (m & (1u << i)) o += snprintf(value + o, vn - o, " %u", i + 1);
      o += snprintf(value + o, vn - o, "  R:");
      for (uint8_t i = 0; i < pods_.maxPods() && o + 3 < vn; i++)
        if (!(m & (1u << i))) o += snprintf(value + o, vn - o, " %u", i + 1);
      break;
    }
    case kSound:
      snprintf(label, ln, "Sound");
      snprintf(value, vn, "%s", settings_.sound ? "On" : "Off");
      break;
    case kBrightness:
      snprintf(label, ln, "Brightness");
      snprintf(value, vn, "%u of 4", settings_.brightness + 1);
      break;
    case kPhone:
      snprintf(label, ln, "Phone mode");
      snprintf(value, vn, "%s", hooks_.phoneOn && hooks_.phoneOn() ? "On" : "Off");
      break;
    case kRescan: snprintf(label, ln, "Find pods again"); break;
    case kAutoOff:
      snprintf(label, ln, "Auto switch-off");
      if (settings_.autoOffMin)
        snprintf(value, vn, "after %u min", settings_.autoOffMin);
      else
        snprintf(value, vn, "Never");
      break;
    case kDemo:
      snprintf(label, ln, "Demo pods");
      snprintf(value, vn, "%s", settings_.demoPods ? "On" : "Off");
      break;
    case kShutDown: snprintf(label, ln, "Switch off"); break;
    default: label[0] = 0;
  }
}

void DialUi::activateMenuItem(uint32_t now) {
  board_.beep(2000, 30);
  switch (menuSel_) {
    case kBack:
      go(beforeMenu_ == Screen::Results ? Screen::Results : Screen::Home);
      return;
    case kPlayer:
      settings_.player = (uint8_t)((settings_.player + 1) % Settings::kPlayers);
      players::name(settings_.player, playerName_, sizeof(playerName_));
      break;
    case kDrillLength:
      settings_.drillSeconds = Settings::nextOption(Settings::kDrillSecondsOptions, settings_.drillSeconds);
      break;
    case kGroups:
      startGroupSetup();
      return;
    case kSound:
      settings_.sound = !settings_.sound;
      board_.setSound(settings_.sound);
      break;
    case kBrightness:
      settings_.brightness = (uint8_t)((settings_.brightness + 1) % 4);
      board_.setBrightnessLevel(settings_.brightness);
      break;
    case kPhone: {
      const bool on = !(hooks_.phoneOn && hooks_.phoneOn());
      if (hooks_.setPhoneMode) hooks_.setPhoneMode(on);
      settings_.save();
      if (on) go(Screen::PhoneQr);
      return;
    }
    case kRescan:
      pods_.rescan();
      snprintf(hint_, sizeof(hint_), "Looking for pods...");
      hintUntil_ = now + 3000;
      go(Screen::Home);
      return;
    case kAutoOff:
      settings_.autoOffMin = Settings::nextOption(Settings::kAutoOffOptions, settings_.autoOffMin);
      break;
    case kDemo:
      settings_.demoPods = !settings_.demoPods;
      settings_.save();
      {
        M5GFX& d = board_.display();
        d.fillScreen(TFT_BLACK);
        d.setFont(kMedium);
        d.setTextDatum(middle_center);
        d.setTextColor(TFT_WHITE);
        d.drawString("Restarting...", board_.width() / 2, board_.height() / 2);
      }
      delay(600);
      if (hooks_.restart) hooks_.restart();
      return;
    case kShutDown:
      go(Screen::ConfirmOff);
      return;
  }
  settings_.save();
  dirty_ = true;
}

// ------------------------------------------------------------------ power

void DialUi::handleAutoOff(uint32_t now) {
  if (!settings_.autoOffMin) return;
  // Busy: a drill is running or a phone is connected.
  if (drillActive() || (hooks_.phoneClients && hooks_.phoneClients() > 0)) {
    lastActivityAt_ = now;
    if (screen_ == Screen::AutoOffWarn) go(Screen::Home);
    return;
  }
  const uint32_t limit = (uint32_t)settings_.autoOffMin * 60000;
  const uint32_t idle = now - lastActivityAt_;
  if (idle >= limit) {
    ESP_LOGI(TAG, "auto switch-off after %u min unused", settings_.autoOffMin);
    shutDown();
  }
  if (idle + kAutoOffWarnMs >= limit) {
    const uint32_t left = (limit - idle + 999) / 1000;
    if (screen_ != Screen::AutoOffWarn) {
      go(Screen::AutoOffWarn);
      lastWarnSecond_ = 0;
    }
    if (left != lastWarnSecond_) {
      lastWarnSecond_ = left;
      if (left % 10 == 0 || left <= 3) board_.beep(1200, 80);
      drawAutoOffWarn(left);
    }
  }
}

void DialUi::shutDown() {
  ESP_LOGI(TAG, "switching off");
  settings_.save();
  for (uint8_t i = 0; i < pods_.maxPods(); i++) pods_.setColour(i, {0, 0, 0});
  M5GFX& d = board_.display();
  d.fillScreen(TFT_BLACK);
  d.setFont(kLarge);
  d.setTextDatum(middle_center);
  d.setTextColor(TFT_WHITE);
  d.drawString("Bye!", board_.width() / 2, board_.height() / 2);
  if (settings_.sound) {
    M5.Speaker.tone(1300, 120);
    delay(150);
    M5.Speaker.tone(900, 120);
    delay(150);
    M5.Speaker.tone(600, 200);
  }
  delay(700);
  board_.powerOff();
}

// ------------------------------------------------------------------ drawing

uint16_t DialUi::podColour(uint8_t i) const {
  return rgb(kPodRgb[i % 6][0], kPodRgb[i % 6][1], kPodRgb[i % 6][2]);
}

void DialUi::drawFull() {
  switch (screen_) {
    case Screen::Home: drawHome(); break;
    case Screen::Countdown: drawCountdown(); break;
    case Screen::Running: drawRunning(); break;
    case Screen::Results: drawResults(); break;
    case Screen::Menu: drawMenu(); break;
    case Screen::ConfirmOff: drawConfirmOff(); break;
    case Screen::AutoOffWarn: drawAutoOffWarn(lastWarnSecond_); break;
    case Screen::PhoneQr: drawPhoneQr(); break;
    case Screen::Groups: drawGroups(); break;
  }
}

// Pod dots around the edge: grey = searching, pod colour = connected,
// white ring = currently lit.
void DialUi::drawPodRing(int radius, int dotR, bool numbers) {
  M5GFX& d = board_.display();
  const int cx = board_.width() / 2, cy = board_.height() / 2;
  const uint8_t n = pods_.maxPods();
  for (uint8_t i = 0; i < n; i++) {
    const float a = (-90.0f + i * 360.0f / n) * DEG_TO_RAD;
    const int x = cx + (int)(radius * cosf(a)), y = cy + (int)(radius * sinf(a));
    const PodStatus st = pods_.status(i);
    const uint16_t col = st == PodStatus::Connected       ? podColour(i)
                         : st == PodStatus::NotResponding ? (uint16_t)TFT_MAROON
                                                          : (uint16_t)TFT_DARKGREY;
    d.fillCircle(x, y, dotR + 3, TFT_BLACK);
    d.fillCircle(x, y, dotR, col);
    if (engine_.litPod() == (int8_t)i) d.drawCircle(x, y, dotR + 2, TFT_WHITE);
    if (numbers) {
      d.setFont(&fonts::Font2);
      d.setTextDatum(middle_center);
      d.setTextColor(TFT_BLACK);
      d.drawNumber(i + 1, x, y + 1);
    }
  }
}

void DialUi::drawIcon(DrillIcon icon, int cx, int cy, uint16_t c) {
  M5GFX& d = board_.display();
  switch (icon) {
    case DrillIcon::Target:
      d.fillCircle(cx, cy, 20, c);
      d.fillCircle(cx, cy, 14, TFT_BLACK);
      d.fillCircle(cx, cy, 8, c);
      break;
    case DrillIcon::Sprint:
      for (int k = 0; k < 2; k++) {
        const int x = cx - 18 + k * 16;
        d.fillTriangle(x, cy - 16, x + 16, cy, x, cy + 16, c);
        d.fillTriangle(x, cy - 7, x + 7, cy, x, cy + 7, TFT_BLACK);
      }
      break;
    case DrillIcon::ColourCall:
      d.fillCircle(cx - 13, cy, 13, rgb(0, 220, 0));
      d.fillCircle(cx + 13, cy, 13, rgb(230, 0, 0));
      break;
    case DrillIcon::LeftRight:
      d.fillTriangle(cx - 30, cy, cx - 8, cy - 16, cx - 8, cy + 16, rgb(kCueLeft.r, kCueLeft.g, kCueLeft.b));
      d.fillTriangle(cx + 30, cy, cx + 8, cy - 16, cx + 8, cy + 16, rgb(kCueRight.r, kCueRight.g, kCueRight.b));
      break;
    case DrillIcon::Sequence:
      for (int k = 0; k < 3; k++) {
        d.fillCircle(cx - 22 + k * 22, cy, 9, c);
        d.setFont(&fonts::Font2);
        d.setTextDatum(middle_center);
        d.setTextColor(TFT_BLACK);
        d.drawNumber(k + 1, cx - 22 + k * 22, cy + 1);
      }
      break;
  }
}

void DialUi::drawHome() {
  M5GFX& d = board_.display();
  d.startWrite();
  d.fillScreen(TFT_BLACK);
  drawPodRing(104, 11, true);
  drawHomeCentre();
  d.endWrite();
}

void DialUi::drawHomeCentre() {
  M5GFX& d = board_.display();
  const int cx = board_.width() / 2, cy = board_.height() / 2;
  const DrillConfig cfg = currentDrill();
  char buf[40];

  d.setTextDatum(middle_center);
  // Player, top
  d.setFont(kSmall);
  d.setTextColor(TFT_LIGHTGREY);
  d.drawString(playerName_, cx, cy - 66);

  drawIcon(cfg.icon, cx, cy - 30, rgb(cfg.colour.r, cfg.colour.g, cfg.colour.b));

  d.setFont(kLarge);
  d.setTextColor(TFT_WHITE);
  d.drawString(cfg.name, cx, cy + 12);

  d.setFont(kSmall);
  d.setTextColor(TFT_LIGHTGREY);
  snprintf(buf, sizeof(buf), "%u seconds", settings_.drillSeconds);
  d.drawString(buf, cx, cy + 38);

  // Bottom hint line
  const uint8_t connected = pods_.connectedCount();
  if (hint_[0]) {
    d.setTextColor(TFT_ORANGE);
    snprintf(buf, sizeof(buf), "%s", hint_);
  } else if (connected == 0) {
    d.setTextColor(TFT_ORANGE);
    const uint16_t heard = hooks_.devicesHeard ? hooks_.devicesHeard() : 0;
    if (heard)
      snprintf(buf, sizeof(buf), "Finding pods (%u heard)", heard);
    else
      snprintf(buf, sizeof(buf), "Finding pods...");
  } else {
    d.setTextColor(TFT_GREEN);
    snprintf(buf, sizeof(buf), "Press to start");
  }
  d.drawString(buf, cx, cy + 62);

  if (settings_.demoPods || (hooks_.phoneOn && hooks_.phoneOn())) {
    d.setFont(&fonts::Font0);
    d.setTextColor(TFT_DARKGREY);
    snprintf(buf, sizeof(buf), "%s%s%s", settings_.demoPods ? "DEMO" : "",
             settings_.demoPods && hooks_.phoneOn && hooks_.phoneOn() ? " - " : "",
             hooks_.phoneOn && hooks_.phoneOn() ? "Wi-Fi on" : "");
    d.drawString(buf, cx, cy + 80);
  }
}

void DialUi::drawCountdown() {
  M5GFX& d = board_.display();
  const int cx = board_.width() / 2, cy = board_.height() / 2;
  d.startWrite();
  d.fillScreen(TFT_BLACK);
  drawPodRing(104, 11, true);
  d.setTextDatum(middle_center);
  d.setTextColor(TFT_WHITE);
  d.setFont(kHuge);
  d.setTextSize(2);
  d.drawNumber(engine_.countdown(), cx, cy);
  d.setTextSize(1);
  d.setFont(kSmall);
  d.setTextColor(TFT_LIGHTGREY);
  d.drawString(engine_.config().name, cx, cy + 62);
  d.endWrite();
}

void DialUi::drawRunning() {
  M5GFX& d = board_.display();
  const int cx = board_.width() / 2, cy = board_.height() / 2;
  d.startWrite();
  d.fillScreen(TFT_BLACK);
  d.setTextDatum(middle_center);
  d.setFont(kSmall);
  d.setTextColor(TFT_LIGHTGREY);
  d.drawString(engine_.config().name, cx, cy - 62);
  d.drawString("hits", cx, cy + 24);
  d.endWrite();
  updateRunning(millis(), true);
}

void DialUi::updateRunning(uint32_t now, bool force) {
  if (screen_ != Screen::Running) return;
  lastRunDraw_ = now;
  M5GFX& d = board_.display();
  const int cx = board_.width() / 2, cy = board_.height() / 2;
  const DrillConfig& cfg = engine_.config();
  d.startWrite();

  // Progress ring: time left in the drill colour, time used in grey.
  if (cfg.durationMs) {
    const uint32_t left = engine_.remainingMs(now);
    const float frac = (float)left / (float)cfg.durationMs;
    const float split = -90.0f + 360.0f * (1.0f - frac);
    const uint16_t col = rgb(cfg.colour.r, cfg.colour.g, cfg.colour.b);
    if (split > -90.0f) d.fillArc(cx, cy, 110, 119, -90.0f, split, (uint16_t)0x2104);
    if (split < 270.0f) d.fillArc(cx, cy, 110, 119, split, 270.0f, col);
  }

  // Small pod dots inside the ring (white ring = the lit pod).
  static int8_t shownLit = -2;
  if (force || shownLit != engine_.litPod()) {
    shownLit = engine_.litPod();
    drawPodRing(94, 7, false);
  }

  if (force) {
    const DrillResult& r = engine_.result();
    char buf[24];
    d.setTextDatum(middle_center);
    d.setFont(&fonts::Font7);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.setTextPadding(120);
    d.drawNumber(r.hits, cx, cy - 12);

    d.setFont(kMedium);
    d.setTextColor(TFT_CYAN, TFT_BLACK);
    d.setTextPadding(140);
    if (engine_.lastHitMs())
      snprintf(buf, sizeof(buf), "%lu ms", (unsigned long)engine_.lastHitMs());
    else
      buf[0] = 0;
    d.drawString(buf, cx, cy + 48);

    d.setFont(kSmall);
    d.setTextColor(r.misses ? TFT_ORANGE : TFT_DARKGREY, TFT_BLACK);
    d.setTextPadding(160);
    if (drills::hasDecoys(engine_.config()))
      snprintf(buf, sizeof(buf), "%u missed  %u wrong", r.misses, r.mistakes);
    else
      snprintf(buf, sizeof(buf), "%u missed", r.misses);
    d.drawString(buf, cx, cy + 72);
    d.setTextPadding(0);
  }
  d.endWrite();
}

void DialUi::drawResults() {
  M5GFX& d = board_.display();
  const int cx = board_.width() / 2, cy = board_.height() / 2;
  const DrillResult& r = engine_.result();
  const uint8_t pages = 1 + pods_.maxPods();
  char buf[40];

  d.startWrite();
  d.fillScreen(TFT_BLACK);
  d.setTextDatum(middle_center);

  if (resultsPage_ == 0) {
    d.setFont(kSmall);
    d.setTextColor(TFT_LIGHTGREY);
    snprintf(buf, sizeof(buf), "%s%s", engine_.config().name, r.aborted ? " (stopped)" : "");
    d.drawString(buf, cx, cy - 76);

    // Font7 digits are 48 px tall: centred at cy-38 they span cy-62..cy-14,
    // leaving a clear gap above the "hits" label.
    d.setFont(&fonts::Font7);
    d.setTextColor(TFT_WHITE);
    d.drawNumber(r.hits, cx, cy - 38);
    d.setFont(kSmall);
    d.setTextColor(TFT_LIGHTGREY);
    d.drawString("hits", cx, cy + 2);

    d.setFont(kMedium);
    d.setTextColor(TFT_CYAN);
    snprintf(buf, sizeof(buf), "avg %lu ms", (unsigned long)r.averageMs());
    d.drawString(r.hits ? buf : "no hits", cx, cy + 28);
    d.setFont(kSmall);
    d.setTextColor(TFT_WHITE);
    if (drills::hasDecoys(engine_.config()))
      snprintf(buf, sizeof(buf), "best %lu  miss %u  wrong %u", (unsigned long)r.bestMs, r.misses, r.mistakes);
    else
      snprintf(buf, sizeof(buf), "best %lu  missed %u", (unsigned long)r.bestMs, r.misses);
    d.drawString(buf, cx, cy + 52);
  } else {
    const uint8_t p = resultsPage_ - 1;
    d.fillCircle(cx, cy - 52, 20, podColour(p));
    d.setFont(kMedium);
    d.setTextColor(TFT_BLACK);
    d.drawNumber(p + 1, cx, cy - 50);
    d.setTextColor(TFT_WHITE);
    snprintf(buf, sizeof(buf), "%u hits", r.podHits[p]);
    d.drawString(buf, cx, cy - 10);
    d.setTextColor(TFT_CYAN);
    if (r.podHits[p])
      snprintf(buf, sizeof(buf), "avg %lu ms", (unsigned long)r.podAverageMs(p));
    else
      snprintf(buf, sizeof(buf), "-");
    d.drawString(buf, cx, cy + 18);
    d.setFont(kSmall);
    d.setTextColor(TFT_WHITE);
    if (drills::hasDecoys(engine_.config()))
      snprintf(buf, sizeof(buf), "best %lu  miss %u  wrong %u", (unsigned long)r.podBestMs[p], r.podMisses[p],
               r.podMistakes[p]);
    else
      snprintf(buf, sizeof(buf), "best %lu  missed %u", (unsigned long)r.podBestMs[p], r.podMisses[p]);
    d.drawString(buf, cx, cy + 44);
  }

  // Page dots + hint.
  for (uint8_t i = 0; i < pages; i++)
    d.fillCircle(cx - (pages - 1) * 6 + i * 12, cy + 74, 3, i == resultsPage_ ? TFT_WHITE : TFT_DARKGREY);
  d.setFont(&fonts::Font0);
  d.setTextColor(TFT_DARKGREY);
  d.drawString("turn: pods  press: done", cx, cy + 90);
  d.endWrite();
  pbBlinkAt_ = 0;  // PB label drawn by loop()
}

void DialUi::drawMenu() {
  M5GFX& d = board_.display();
  const int cx = board_.width() / 2, cy = board_.height() / 2;
  char label[24], value[24];
  d.startWrite();
  d.fillScreen(TFT_BLACK);
  d.setTextDatum(middle_center);

  // Previous / next items, dimmed.
  for (int off : {-1, 1}) {
    const uint8_t item = (uint8_t)((menuSel_ + kMenuCount + off) % kMenuCount);
    menuLabel(item, label, sizeof(label), value, sizeof(value));
    d.setFont(kSmall);
    d.setTextColor(TFT_DARKGREY);
    d.drawString(label, cx, cy + off * 62);
  }
  // Selected item, big, with its value below.
  menuLabel(menuSel_, label, sizeof(label), value, sizeof(value));
  d.drawRoundRect(14, cy - 32, board_.width() - 28, 64, 14, TFT_DARKGREY);
  d.setFont(kMedium);
  d.setTextColor(menuSel_ == kShutDown ? TFT_ORANGE : TFT_WHITE);
  d.drawString(label, cx, value[0] ? cy - 10 : cy);
  if (value[0]) {
    d.setFont(kSmall);
    d.setTextColor(TFT_CYAN);
    d.drawString(value, cx, cy + 16);
  }
  d.setFont(&fonts::Font0);
  d.setTextColor(TFT_DARKGREY);
  d.drawString("MENU", cx, 16);
  d.endWrite();
}

void DialUi::drawConfirmOff() {
  M5GFX& d = board_.display();
  const int cx = board_.width() / 2, cy = board_.height() / 2;
  d.startWrite();
  d.fillScreen(TFT_BLACK);
  d.setTextDatum(middle_center);
  d.setFont(kLarge);
  d.setTextColor(TFT_ORANGE);
  d.drawString("Switch off?", cx, cy - 20);
  d.setFont(kSmall);
  d.setTextColor(TFT_WHITE);
  d.drawString("Press = yes", cx, cy + 18);
  d.setTextColor(TFT_LIGHTGREY);
  d.drawString("Turn = no", cx, cy + 42);
  d.endWrite();
}

void DialUi::drawAutoOffWarn(uint32_t secondsLeft) {
  M5GFX& d = board_.display();
  const int cx = board_.width() / 2, cy = board_.height() / 2;
  d.startWrite();
  d.fillScreen(TFT_BLACK);
  d.setTextDatum(middle_center);
  d.setFont(kSmall);
  d.setTextColor(TFT_LIGHTGREY);
  d.drawString("Switching off in", cx, cy - 46);
  d.setFont(&fonts::Font7);
  d.setTextColor(TFT_ORANGE);
  d.drawNumber(secondsLeft, cx, cy - 4);
  d.setFont(kSmall);
  d.setTextColor(TFT_WHITE);
  d.drawString("Press to keep it on", cx, cy + 46);
  d.endWrite();
}

// QR code that joins the open "PodHub" network. iPhone camera: point, tap
// the banner, and the page opens by itself (captive portal).
void DialUi::drawPhoneQr() {
  M5GFX& d = board_.display();
  const int cx = board_.width() / 2;
  d.startWrite();
  d.fillScreen(TFT_WHITE);  // QR needs a light quiet zone
  const int size = 130;
  d.qrcode("WIFI:T:nopass;S:PodHub;;", cx - size / 2, 46, size, 3);
  d.setTextDatum(middle_center);
  d.setTextColor(TFT_BLACK);
  d.setFont(&fonts::Font2);
  d.drawString("Scan to join PodHub", cx, 28);
  d.drawString("then open 192.168.4.1", cx, 190);
  d.setFont(&fonts::Font0);
  d.drawString("press to close", cx, 210);
  d.endWrite();
}

// ------------------------------------------------------------------ remote (phone page)

void DialUi::selectDrill(uint8_t i) {
  if (drillActive() || i >= drills::kCount) return;
  drill_ = i;
  settings_.lastDrill = i;
  settings_.save();
  if (screen_ == Screen::Home) dirty_ = true;
}

void DialUi::selectPlayer(uint8_t i) {
  if (drillActive() || i >= Settings::kPlayers) return;
  settings_.player = i;
  settings_.save();
  reloadPlayerName();
}

void DialUi::setDrillSeconds(uint8_t seconds) {
  if (drillActive()) return;
  for (uint8_t s : Settings::kDrillSecondsOptions) {
    if (s == seconds) {
      settings_.drillSeconds = s;
      settings_.save();
      if (screen_ == Screen::Home || screen_ == Screen::Menu) dirty_ = true;
      return;
    }
  }
}

void DialUi::reloadPlayerName() {
  players::name(settings_.player, playerName_, sizeof(playerName_));
  if (screen_ == Screen::Home || screen_ == Screen::Menu) dirty_ = true;
}

bool DialUi::startSelected(uint32_t now) {
  if (drillActive()) return false;
  if (engine_.phase() == DrillPhase::Finished) engine_.reset();
  startDrill(now);
  return drillActive();
}

void DialUi::stopDrill(uint32_t now) {
  if (!drillActive()) return;
  engine_.stop(now);
  board_.beep(600, 150);
}

void DialUi::closeResults() {
  if (engine_.phase() != DrillPhase::Finished) return;
  engine_.reset();
  go(Screen::Home);
}

// ------------------------------------------------------------------ left/right setup

void DialUi::startGroupSetup() {
  if (drillActive()) return;
  if (engine_.phase() == DrillPhase::Finished) engine_.reset();
  groupsEdit_ = leftMask();
  showGroupsOnPods();
  go(Screen::Groups);
}

// Left pods glow blue, right pods yellow, so you can see the split.
void DialUi::showGroupsOnPods() {
  for (uint8_t i = 0; i < pods_.maxPods(); i++) pods_.setColour(i, (groupsEdit_ & (1u << i)) ? kCueLeft : kCueRight);
}

void DialUi::endGroupSetup(bool save) {
  for (uint8_t i = 0; i < pods_.maxPods(); i++) pods_.setColour(i, {0, 0, 0});
  if (save) {
    settings_.leftMask = groupsEdit_ ? groupsEdit_ : drills::defaultLeftMask(pods_.maxPods());
    settings_.save();
    ESP_LOGI(TAG, "left/right groups saved: left mask 0x%02X", settings_.leftMask);
    board_.play({{1500, 60}, {2000, 90}});
  }
  go(Screen::Home);
}

void DialUi::onTap(const PodTap& tap) {
  if (screen_ != Screen::Groups || tap.slot >= pods_.maxPods()) return;
  groupsEdit_ ^= (uint8_t)(1u << tap.slot);
  pods_.setColour(tap.slot, (groupsEdit_ & (1u << tap.slot)) ? kCueLeft : kCueRight);
  board_.beep(groupsEdit_ & (1u << tap.slot) ? 1200 : 1800, 50);
  lastActivityAt_ = millis();
  dirty_ = true;
}

void DialUi::drawGroups() {
  M5GFX& d = board_.display();
  const int cx = board_.width() / 2, cy = board_.height() / 2;
  d.startWrite();
  d.fillScreen(TFT_BLACK);
  d.setTextDatum(middle_center);
  d.setFont(kSmall);
  d.setTextColor(TFT_LIGHTGREY);
  d.drawString("Tap a pod to swap sides", cx, cy - 66);

  // Left list in blue, right list in yellow.
  char left[24] = "", right[24] = "";
  size_t lo = 0, ro = 0;
  for (uint8_t i = 0; i < pods_.maxPods(); i++) {
    if (groupsEdit_ & (1u << i))
      lo += snprintf(left + lo, sizeof(left) - lo, "%s%u", lo ? " " : "", i + 1);
    else
      ro += snprintf(right + ro, sizeof(right) - ro, "%s%u", ro ? " " : "", i + 1);
  }
  d.setFont(kMedium);
  d.setTextColor(rgb(kCueLeft.r, kCueLeft.g, kCueLeft.b));
  d.drawString("LEFT", cx - 52, cy - 28);
  d.setTextColor(rgb(kCueRight.r, kCueRight.g, kCueRight.b));
  d.drawString("RIGHT", cx + 52, cy - 28);
  d.setFont(kLarge);
  d.setTextColor(TFT_WHITE);
  d.drawString(left[0] ? left : "-", cx - 52, cy + 6);
  d.drawString(right[0] ? right : "-", cx + 52, cy + 6);
  d.drawFastVLine(cx, cy - 40, 64, TFT_DARKGREY);

  d.setFont(kSmall);
  d.setTextColor(TFT_GREEN);
  d.drawString("Press to save", cx, cy + 50);
  d.setTextColor(TFT_DARKGREY);
  d.drawString("Hold to cancel", cx, cy + 72);
  d.endWrite();
}
