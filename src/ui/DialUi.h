// On-Dial user interface: everything works with the knob and a press.
//
//   Home      turn = choose drill, press = start, hold 3 s = menu
//   Countdown big 3-2-1 with beeps (press = cancel)
//   Running   hits, last reaction, ring of time left (press = stop)
//   Results   turn = per-pod pages, press = back to home
//   Menu      turn = move, press = change/choose
#pragma once

#include <Arduino.h>
#include <functional>

#include "../board/Board.h"
#include "../engine/DrillEngine.h"
#include "../pods/PodDriver.h"
#include "../settings/Settings.h"

class DialUi {
 public:
  struct Hooks {
    std::function<void(bool on)> setPhoneMode;  // start/stop the Wi-Fi page
    std::function<bool()> phoneOn;
    std::function<uint8_t()> phoneClients;
    std::function<void()> restart;             // e.g. after toggling demo pods
    std::function<uint16_t()> devicesHeard;     // BLE diagnostics (0 if n/a)
    // A drill ended: save it to history.
    std::function<void(const DrillConfig&, const DrillResult&, bool pb)> sessionFinished;
  };

  DialUi(Board& board, PodDriver& pods, DrillEngine& engine, Settings& settings, Hooks hooks)
      : board_(board), pods_(pods), engine_(engine), settings_(settings), hooks_(hooks) {}

  void begin();
  void loop(uint32_t now);
  void showPhoneQr();
  bool drillActive() const;
  // True while the pods' lights belong to the UI (drill or setup screen).
  bool podsInUse() const { return drillActive() || screen_ == Screen::Groups; }

  // --- same actions as the knob/press, for the phone page ---
  uint8_t selectedDrill() const { return drill_; }
  bool lastResultWasPb() const { return resultsPb_; }
  void selectDrill(uint8_t i);
  void selectPlayer(uint8_t i);
  void setDrillSeconds(uint8_t seconds);
  void reloadPlayerName();
  bool startSelected(uint32_t now);  // from home or results
  void stopDrill(uint32_t now);
  void closeResults();
  void noteActivity(uint32_t now) { lastActivityAt_ = now; }
  // Every tap from the pods (the engine gets them too). Used by setup screens.
  void onTap(const PodTap& tap);
  // Left/right setup: tap the pods to swap sides, press the Dial to save.
  void startGroupSetup();
  // Settings changed from the phone: apply brightness/sound and redraw.
  void applySettings();
  // Firmware update in progress (0..100): takes over the screen.
  void showUpdate(int8_t percent);
  uint8_t leftMask() const;

 private:
  enum class Screen : uint8_t { Home, Countdown, Running, Results, Menu, ConfirmOff, AutoOffWarn, PhoneQr, Groups, Updating };
  enum MenuItem : uint8_t {
    kBack, kPlayer, kDrillLength, kGroups, kSound, kBrightness, kPhone, kRescan, kAutoOff, kDemo, kShutDown, kMenuCount
  };

  void go(Screen s);
  void handleInput(uint32_t now);
  void handleEngineEvents(uint32_t now);
  void handleAutoOff(uint32_t now);
  void startDrill(uint32_t now);
  void activateMenuItem(uint32_t now);
  void shutDown();

  // drawing
  void drawFull();
  void drawHome();
  void drawHomeCentre();
  void drawPodRing(int radius, int dotR, bool numbers);
  void drawIcon(DrillIcon icon, int cx, int cy, uint16_t colour);
  void drawCountdown();
  void drawRunning();
  void updateRunning(uint32_t now, bool force);
  void drawResults();
  void drawMenu();
  void drawConfirmOff();
  void drawAutoOffWarn(uint32_t secondsLeft);
  void drawPhoneQr();
  void drawGroups();
  void showGroupsOnPods();
  void endGroupSetup(bool save);
  void menuLabel(uint8_t item, char* label, size_t ln, char* value, size_t vn);
  uint16_t podColour(uint8_t i) const;

  DrillConfig currentDrill() const;

  Board& board_;
  PodDriver& pods_;
  DrillEngine& engine_;
  Settings& settings_;
  Hooks hooks_;

  Screen screen_ = Screen::Home;
  Screen beforeMenu_ = Screen::Home;
  bool dirty_ = true;
  uint8_t drill_ = 0;
  uint8_t menuSel_ = 0;
  uint8_t resultsPage_ = 0;
  bool resultsPb_ = false;
  bool pbBlink_ = false;
  uint32_t pbBlinkAt_ = 0;
  uint32_t screenAt_ = 0;     // when the current screen was entered
  uint32_t lastRunDraw_ = 0;
  uint32_t lastActivityAt_ = 0;
  uint32_t lastWarnSecond_ = 0;
  char hint_[32] = "";
  uint32_t hintUntil_ = 0;
  PodStatus shownStatus_[6] = {};
  char playerName_[16] = "";
  uint8_t groupsEdit_ = 0;  // working copy while setting up left/right
  int8_t updatePct_ = -1;
};
