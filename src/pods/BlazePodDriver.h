// BlazePod driver over BLE (NimBLE-Arduino 2.x, central role).
//
// All connect/scan work runs in one FreeRTOS task pinned to core 0 (the
// NimBLE host also runs there). The Arduino loop (core 1) only calls
// setColour() and pollTap(). Taps cross cores through a FreeRTOS queue.
#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <atomic>

#include "PodDriver.h"

class BlazePodDriver : public PodDriver {
 public:
  static constexpr uint8_t kMaxSlots = 6;

  void begin(uint8_t maxPods) override;
  uint8_t maxPods() const override { return maxPods_; }
  PodStatus status(uint8_t slot) const override;
  bool setColour(uint8_t slot, Rgb c, bool offOnTap = false) override;
  bool pollTap(PodTap& out) override;
  int8_t battery(uint8_t slot) const override;
  void rescan() override;
  const char* label(uint8_t slot) const override;

  // Log every GATT service/characteristic on first connection (spike aid).
  void setLogGatt(bool on) { logGatt_ = on; }

  // Diagnostics: BLE devices heard recently, strongest first.
  struct Nearby {
    char name[20];
    int8_t rssi;
    uint8_t mfrLen;
    bool isPod;
  };
  uint8_t nearby(Nearby* out, uint8_t max);
  uint16_t devicesHeard() const { return seenCount_; }

 private:
  struct Slot {
    std::atomic<PodStatus> status{PodStatus::Empty};
    bool assigned = false;
    NimBLEAddress addr;
    uint8_t mfr[31] = {0};
    uint8_t mfrLen = 0;
    NimBLEClient* client = nullptr;
    NimBLERemoteCharacteristic* colourChr = nullptr;  // guarded by mutex_
    uint32_t unlockedAt = 0;
    uint32_t nextAttemptAt = 0;
    uint8_t quickDrops = 0;
    uint8_t failedAttempts = 0;
    bool gattLogged = false;
    std::atomic<int8_t> battery{-1};
    char label[18] = "-";
  };

  struct Candidate {
    NimBLEAddress addr;
    uint8_t mfr[31];
    uint8_t mfrLen;
    int8_t rssi;
  };

  class ScanCallbacks : public NimBLEScanCallbacks {
   public:
    explicit ScanCallbacks(BlazePodDriver& d) : d_(d) {}
    void onDiscovered(const NimBLEAdvertisedDevice* dev) override { d_.handleAdvert(dev); }
    void onResult(const NimBLEAdvertisedDevice* dev) override { d_.handleAdvert(dev); }
    void onScanEnd(const NimBLEScanResults&, int reason) override;

   private:
    BlazePodDriver& d_;
  };

  class ClientCallbacks : public NimBLEClientCallbacks {
   public:
    explicit ClientCallbacks(BlazePodDriver& d) : d_(d) {}
    void onDisconnect(NimBLEClient* c, int reason) override;

   private:
    BlazePodDriver& d_;
  };

  struct Seen {
    NimBLEAddress addr;
    Nearby info;
    uint32_t lastSeen;
  };
  static constexpr uint8_t kSeenMax = 16;

  void handleAdvert(const NimBLEAdvertisedDevice* dev);
  void noteSeen(const NimBLEAdvertisedDevice* dev, const std::string& name, size_t mfrLen, bool pod);
  static void taskEntry(void* self);
  void taskLoop();
  void startScan();
  bool takeCandidate(Candidate& out);
  bool knownAddress(const NimBLEAddress& a);
  bool connectSlot(uint8_t i);
  void logGatt(Slot& s, uint8_t i);
  void readBattery(Slot& s);
  int slotForClient(NimBLEClient* c);
  void onTapNotify(uint8_t slot, const uint8_t* data, size_t len);

  uint8_t maxPods_ = 0;
  Slot slots_[kMaxSlots];
  SemaphoreHandle_t mutex_ = nullptr;
  QueueHandle_t tapQueue_ = nullptr;
  Candidate candidates_[kMaxSlots];
  uint8_t candidateCount_ = 0;  // guarded by mutex_
  std::atomic<bool> scanning_{false};
  std::atomic<bool> rescanRequested_{false};
  uint32_t lastNewPodAt_ = 0;
  bool logGatt_ = true;
  Seen seen_[kSeenMax];
  uint8_t seenUsed_ = 0;     // guarded by mutex_
  uint16_t seenCount_ = 0;   // unique devices heard since boot (capped table)
  uint32_t scanStartedAt_ = 0;
  ScanCallbacks scanCb_{*this};
  ClientCallbacks clientCb_{*this};
};
