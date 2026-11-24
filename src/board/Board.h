// Thin hardware wrapper so the rest of the firmware doesn't care which
// ESP32-S3 board it runs on. Today: M5Stack Dial via M5Unified.
#pragma once

#include <M5Unified.h>

class Board {
 public:
  void begin() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    cfg.internal_spk = true;
    M5.begin(cfg);
    M5.Display.setBrightness(160);
    M5.Speaker.setVolume(160);
    M5.BtnA.setHoldThresh(1500);
  }

  void update() { M5.update(); }

  // Pressing the whole Dial face (BtnA on the Dial).
  bool wasPressed() { return M5.BtnA.wasClicked(); }
  bool wasLongPressed() { return M5.BtnA.wasHold(); }

  void beep(float hz, uint32_t ms) {
    if (soundOn_) M5.Speaker.tone(hz, ms);
  }
  void setSound(bool on) { soundOn_ = on; }

  M5GFX& display() { return M5.Display; }
  int width() { return M5.Display.width(); }
  int height() { return M5.Display.height(); }

 private:
  bool soundOn_ = true;
};
