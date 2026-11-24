// User settings, stored in NVS. Every setting has a sensible default and
// nothing here is ever required.
#pragma once

#include <Preferences.h>
#include <stdint.h>

struct Settings {
  bool phoneMode = true;     // Wi-Fi AP + web page. On by default while we soak-test (M2).
  uint8_t expectedPods = 4;  // how many pods to look for (1-6)

  void load() {
    Preferences p;
    if (!p.begin("settings", true)) return;  // first boot: defaults
    phoneMode = p.getBool("phone", phoneMode);
    expectedPods = p.getUChar("pods", expectedPods);
    if (expectedPods < 1 || expectedPods > 6) expectedPods = 4;
    p.end();
  }

  void save() const {
    Preferences p;
    if (!p.begin("settings", false)) return;
    p.putBool("phone", phoneMode);
    p.putUChar("pods", expectedPods);
    p.end();
  }
};
