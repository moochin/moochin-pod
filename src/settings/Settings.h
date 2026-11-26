// User settings, stored in NVS. Every setting has a sensible default and
// nothing here is ever required.
#pragma once

#include <Preferences.h>
#include <stdint.h>

struct Settings {
  bool phoneMode = false;    // Wi-Fi AP + web page: only when switched on (saves RAM)
  uint8_t expectedPods = 4;  // how many pods to look for (1-6)
  uint8_t drillSeconds = 30; // drill length (U12-U14 default)
  bool sound = true;
  uint8_t brightness = 2;    // 0..3
  uint8_t autoOffMin = 15;   // switch off after this long unused (0 = never)
  bool demoPods = false;     // simulated pods instead of real ones
  uint8_t lastDrill = 0;     // drill shown on the home screen at start
  uint8_t player = 0;        // current profile (0..kPlayers-1); results are logged against it

  static constexpr uint8_t kPlayers = 4;

  static constexpr uint8_t kDrillSecondsOptions[] = {20, 30, 45, 60};
  static constexpr uint8_t kAutoOffOptions[] = {5, 10, 15, 30, 0};

  void load() {
    Preferences p;
    if (!p.begin("settings", true)) return;  // first boot: defaults
    phoneMode = p.getBool("phone", phoneMode);
    expectedPods = p.getUChar("pods", expectedPods);
    drillSeconds = p.getUChar("drillS", drillSeconds);
    sound = p.getBool("sound", sound);
    brightness = p.getUChar("bright", brightness);
    autoOffMin = p.getUChar("autoOff", autoOffMin);
    demoPods = p.getBool("demo", demoPods);
    lastDrill = p.getUChar("drill", lastDrill);
    player = p.getUChar("player", player);
    p.end();
    if (player >= kPlayers) player = 0;
    if (expectedPods < 1 || expectedPods > 6) expectedPods = 4;
    if (drillSeconds < 10 || drillSeconds > 120) drillSeconds = 30;
    if (brightness > 3) brightness = 2;
  }

  void save() const {
    Preferences p;
    if (!p.begin("settings", false)) return;
    p.putBool("phone", phoneMode);
    p.putUChar("pods", expectedPods);
    p.putUChar("drillS", drillSeconds);
    p.putBool("sound", sound);
    p.putUChar("bright", brightness);
    p.putUChar("autoOff", autoOffMin);
    p.putBool("demo", demoPods);
    p.putUChar("drill", lastDrill);
    p.putUChar("player", player);
    p.end();
  }

  template <size_t N>
  static uint8_t nextOption(const uint8_t (&opts)[N], uint8_t current) {
    for (size_t i = 0; i < N; i++)
      if (opts[i] == current) return opts[(i + 1) % N];
    return opts[0];
  }
};
