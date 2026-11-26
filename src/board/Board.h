// Thin hardware wrapper so the rest of the firmware doesn't care which
// ESP32-S3 board it runs on. Today: M5Stack Dial via M5Unified.
//
// Inputs: the knob (rotary encoder) and pressing the whole Dial face.
// Everything works without touch, so muddy hands and gloves are fine.
#pragma once

#include <M5Unified.h>

class Board {
 public:
  // Brightness steps offered in the menu (index 0..3).
  static constexpr uint8_t kBrightness[4] = {40, 90, 160, 255};

  void begin();
  void update();

  // Knob: clicks turned since the last call (+ clockwise, - anticlockwise).
  int knob();
  // Press = press and release; long press = held for kLongPressMs.
  bool pressed() { return M5.BtnA.wasClicked(); }
  bool longPressed() { return M5.BtnA.wasHold(); }
  bool anyInput() const { return lastInputAt_ != 0; }
  uint32_t lastInputAt() const { return lastInputAt_; }

  // Sound: non-blocking. Notes play one after another from update().
  struct Note {
    uint16_t hz;     // 0 = rest
    uint16_t ms;
  };
  void beep(uint16_t hz, uint16_t ms) { play({{hz, ms}}); }
  // replace = true: drop anything still queued (immediate feedback).
  // replace = false: play after what's queued (don't cut a miss beep short).
  void play(std::initializer_list<Note> notes, bool replace = true);
  void setSound(bool on) { soundOn_ = on; }

  void setBrightnessLevel(uint8_t level);  // 0..3

  M5GFX& display() { return M5.Display; }
  int width() { return M5.Display.width(); }
  int height() { return M5.Display.height(); }

  // Cuts the power when running on the battery (releases the Dial's
  // power-hold pin). On USB power the Dial can't switch itself off, so it
  // goes into deep sleep with the screen off until USB is unplugged or reset.
  [[noreturn]] void powerOff();

 private:
  static constexpr uint32_t kLongPressMs = 3000;
  static constexpr int kCountsPerClick = 4;  // quadrature counts per knob detent

  bool soundOn_ = true;
  uint32_t lastInputAt_ = 0;
  int knobRemainder_ = 0;
  Note notes_[12];
  uint8_t noteHead_ = 0, noteLen_ = 0;
  uint32_t noteEndsAt_ = 0;
};
