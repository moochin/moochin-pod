#include "Board.h"

#include <esp_sleep.h>

namespace {
// Dial encoder pins (same as M5Stack's M5Dial library).
constexpr int kEncA = 41;
constexpr int kEncB = 40;

volatile int32_t encCount = 0;
volatile uint8_t encState = 0;

// Quadrature decode: index = (previous AB << 2) | current AB.
// +1 / -1 for valid single steps, 0 for no change or invalid (bounce).
const int8_t kQuad[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

void IRAM_ATTR onEncoder() {
  const uint8_t ab = (uint8_t)((digitalRead(kEncA) << 1) | digitalRead(kEncB));
  encCount += kQuad[(encState << 2) | ab];
  encState = ab;
}
}  // namespace

void Board::begin() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_spk = true;
  M5.begin(cfg);
  M5.Display.setBrightness(kBrightness[2]);
  M5.Speaker.setVolume(160);
  M5.BtnA.setHoldThresh(kLongPressMs);

  pinMode(kEncA, INPUT_PULLUP);
  pinMode(kEncB, INPUT_PULLUP);
  encState = (uint8_t)((digitalRead(kEncA) << 1) | digitalRead(kEncB));
  attachInterrupt(kEncA, onEncoder, CHANGE);
  attachInterrupt(kEncB, onEncoder, CHANGE);
}

void Board::update() {
  M5.update();
  if (M5.BtnA.wasPressed() || M5.BtnA.wasHold()) lastInputAt_ = millis();

  // Sound queue.
  const uint32_t now = millis();
  if (noteLen_ && (int32_t)(now - noteEndsAt_) >= 0) {
    const Note n = notes_[noteHead_];
    noteHead_ = (noteHead_ + 1) % (sizeof(notes_) / sizeof(notes_[0]));
    noteLen_--;
    if (n.hz && soundOn_) M5.Speaker.tone(n.hz, n.ms);
    noteEndsAt_ = now + n.ms + 15;  // small gap so repeated notes are distinct
  }
}

int Board::knob() {
  noInterrupts();
  const int32_t c = encCount;
  encCount = 0;
  interrupts();
  knobRemainder_ += c;
  const int clicks = knobRemainder_ / kCountsPerClick;
  knobRemainder_ -= clicks * kCountsPerClick;
  if (clicks) lastInputAt_ = millis();
  return clicks;
}

void Board::play(std::initializer_list<Note> notes, bool replace) {
  const uint8_t cap = sizeof(notes_) / sizeof(notes_[0]);
  if (replace) {
    noteHead_ = 0;
    noteLen_ = 0;
    noteEndsAt_ = 0;
  }
  for (const Note& n : notes) {
    if (noteLen_ >= cap) break;
    notes_[(noteHead_ + noteLen_) % cap] = n;
    noteLen_++;
  }
}

void Board::setBrightnessLevel(uint8_t level) {
  M5.Display.setBrightness(kBrightness[level < 4 ? level : 3]);
}

void Board::powerOff() {
  M5.Speaker.stop();
  M5.Display.setBrightness(0);
  M5.Display.sleep();
  delay(50);
  // Releases GPIO46 (power hold): on battery the power is cut here.
  // On USB it falls through to deep sleep with no wake source.
  M5.Power.powerOff();
  esp_deep_sleep_start();
}
