#include "BlazePodDriver.h"

#include "blazepod_protocol.h"

static const char* TAG = "pods";

namespace {
constexpr uint32_t kConnectTimeoutMs = 4000;
// A pod that drops this soon after unlocking probably rejected the unlock.
constexpr uint32_t kQuickDropMs = 15000;
constexpr uint8_t kQuickDropsBeforeGivingUp = 3;
constexpr uint32_t kNotRespondingRetryMs = 10000;
constexpr size_t kTapQueueLen = 32;

const NimBLEUUID kBatteryService((uint16_t)0x180F);
const NimBLEUUID kBatteryLevel((uint16_t)0x2A19);

struct Lock {
  explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(m_); }
  SemaphoreHandle_t m_;
};

void hex(const uint8_t* d, size_t n, char* out, size_t outLen) {
  size_t o = 0;
  for (size_t i = 0; i < n && o + 3 < outLen; i++) o += snprintf(out + o, outLen - o, "%02X", d[i]);
  out[o] = 0;
}
}  // namespace

// ---------------------------------------------------------------- public API

void BlazePodDriver::begin(uint8_t maxPods) {
  maxPods_ = maxPods > kMaxSlots ? kMaxSlots : maxPods;
  mutex_ = xSemaphoreCreateMutex();
  tapQueue_ = xQueueCreate(kTapQueueLen, sizeof(PodTap));

  NimBLEDevice::init("PodHub");
  NimBLEDevice::setPower(9);  // max TX power: pods may be 10-20 m away on a pitch

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCb_, false);
  scan->setActiveScan(true);  // the "BlazePod" name may be in the scan response
  scan->setMaxResults(0);     // don't keep results in RAM; we filter in onResult
  scan->setInterval(100);
  scan->setWindow(60);

  lastNewPodAt_ = millis();
  // Core 0 alongside the NimBLE host; the Arduino loop/UI runs on core 1.
  xTaskCreatePinnedToCore(taskEntry, "pods", 6144, this, 2, nullptr, 0);
  ESP_LOGI(TAG, "started, expecting up to %u pods", maxPods_);
}

PodStatus BlazePodDriver::status(uint8_t slot) const {
  return slot < maxPods_ ? slots_[slot].status.load() : PodStatus::Empty;
}

bool BlazePodDriver::setColour(uint8_t slot, Rgb c, bool offOnTap) {
  if (slot >= maxPods_) return false;
  uint8_t buf[4];
  size_t n = blazepod::encodeColour(c.r, c.g, c.b, offOnTap, buf);
  Lock lock(mutex_);
  NimBLERemoteCharacteristic* chr = slots_[slot].colourChr;
  if (chr == nullptr) return false;
  return chr->writeValue(buf, n, false);  // no response: fast, non-blocking
}

bool BlazePodDriver::pollTap(PodTap& out) {
  return tapQueue_ && xQueueReceive(tapQueue_, &out, 0) == pdTRUE;
}

int8_t BlazePodDriver::battery(uint8_t slot) const {
  return slot < maxPods_ ? slots_[slot].battery.load() : -1;
}

void BlazePodDriver::rescan() { rescanRequested_ = true; }

const char* BlazePodDriver::label(uint8_t slot) const {
  return slot < maxPods_ ? slots_[slot].label : "-";
}

// ------------------------------------------------------------- BLE callbacks
// These run in the NimBLE host task: keep them short, never block.

void BlazePodDriver::ScanCallbacks::onResult(const NimBLEAdvertisedDevice* dev) {
  // Pick the longest manufacturer data entry (same rule as both reference ports).
  std::string best;
  for (uint8_t i = 0; i < dev->getManufacturerDataCount(); i++) {
    std::string m = dev->getManufacturerData(i);
    if (m.size() > best.size()) best = m;
  }
  std::string name = dev->getName();
  if (!blazepod::looksLikePod(name.c_str(), best.size())) return;
  if (!dev->isConnectable()) return;
  if (d_.knownAddress(dev->getAddress())) return;

  Lock lock(d_.mutex_);
  if (d_.candidateCount_ >= kMaxSlots) return;
  Candidate& c = d_.candidates_[d_.candidateCount_++];
  c.addr = dev->getAddress();
  c.mfrLen = best.size() > sizeof(c.mfr) ? sizeof(c.mfr) : best.size();
  memcpy(c.mfr, best.data(), c.mfrLen);
  c.rssi = dev->getRSSI();
}

void BlazePodDriver::ScanCallbacks::onScanEnd(const NimBLEScanResults&, int reason) {
  d_.scanning_ = false;
  ESP_LOGD(TAG, "scan ended (%d)", reason);
}

void BlazePodDriver::ClientCallbacks::onDisconnect(NimBLEClient* c, int reason) {
  int i = d_.slotForClient(c);
  if (i < 0) return;
  Slot& s = d_.slots_[i];
  {
    Lock lock(d_.mutex_);
    s.colourChr = nullptr;
  }
  // Only react to drops of fully connected pods. A failed connect attempt
  // (or a rescan) has already set status and retry time itself.
  if (!s.assigned || s.status != PodStatus::Connected) return;
  uint32_t now = millis();
  uint32_t upFor = now - s.unlockedAt;
  s.quickDrops = upFor < kQuickDropMs ? s.quickDrops + 1 : 0;
  ESP_LOGW(TAG, "pod %d (%s) disconnected after %lu ms, reason 0x%X", i + 1, s.label,
           (unsigned long)upFor, reason);
  if (s.quickDrops >= kQuickDropsBeforeGivingUp) {
    s.status = PodStatus::NotResponding;
    s.nextAttemptAt = now + kNotRespondingRetryMs;
    ESP_LOGE(TAG,
             "pod %d keeps dropping straight after unlock. It may have newer BlazePod "
             "firmware that this hub doesn't understand.",
             i + 1);
  } else {
    s.status = PodStatus::Lost;
    s.nextAttemptAt = now + 300;
  }
}

void BlazePodDriver::onTapNotify(uint8_t slot, const uint8_t* data, size_t len) {
  char raw[24];
  hex(data, len, raw, sizeof(raw));
  blazepod::Tap t;
  blazepod::TapParse r = blazepod::parseTap(data, len, t);
  if (r != blazepod::TapParse::Ok) {
    ESP_LOGW(TAG, "pod %u: unrecognised tap payload %s", slot + 1, raw);
    return;
  }
  PodTap ev;
  ev.slot = slot;
  ev.kind = t.state == blazepod::TapState::WhileOff      ? TapKind::WhileOff
            : t.state == blazepod::TapState::LitStayedOn ? TapKind::LitStayedOn
                                                         : TapKind::LitTurnedOff;
  ev.podMs = t.elapsedMs;
  ev.hubMillis = millis();
  ESP_LOGD(TAG, "pod %u raw tap %s", slot + 1, raw);
  if (xQueueSend(tapQueue_, &ev, 0) != pdTRUE) ESP_LOGW(TAG, "tap queue full, dropped");
}

// ------------------------------------------------------------- worker task

void BlazePodDriver::taskEntry(void* self) { static_cast<BlazePodDriver*>(self)->taskLoop(); }

void BlazePodDriver::taskLoop() {
  for (;;) {
    if (rescanRequested_) {
      rescanRequested_ = false;
      NimBLEDevice::getScan()->stop();
      for (uint8_t i = 0; i < maxPods_; i++) {
        Slot& s = slots_[i];
        if (s.client && s.client->isConnected()) {
          s.status = PodStatus::Empty;  // so onDisconnect doesn't count a quick drop
          s.client->disconnect();
        }
        s.assigned = false;
        s.status = PodStatus::Empty;
        s.quickDrops = s.failedAttempts = 0;
        s.battery = -1;
        strcpy(s.label, "-");
      }
      Lock lock(mutex_);
      candidateCount_ = 0;
      ESP_LOGI(TAG, "rescan requested: forgot all pods");
    }

    // 1. New pods found by the scanner: give each the next free number.
    Candidate c;
    if (takeCandidate(c)) {
      for (uint8_t i = 0; i < maxPods_; i++) {
        Slot& s = slots_[i];
        if (s.assigned) continue;
        s.assigned = true;
        s.addr = c.addr;
        memcpy(s.mfr, c.mfr, c.mfrLen);
        s.mfrLen = c.mfrLen;
        snprintf(s.label, sizeof(s.label), "%s", c.addr.toString().c_str());
        char m[64];
        hex(s.mfr, s.mfrLen, m, sizeof(m));
        ESP_LOGI(TAG, "found pod -> #%u  addr=%s rssi=%d mfr=%s", i + 1, s.label, c.rssi, m);
        lastNewPodAt_ = millis();
        connectSlot(i);
        break;
      }
      continue;
    }

    // 2. Reconnect pods we already know, straight to their address (no scan).
    uint32_t now = millis();
    for (uint8_t i = 0; i < maxPods_; i++) {
      Slot& s = slots_[i];
      if (!s.assigned) continue;
      PodStatus st = s.status;
      if (st == PodStatus::Connected) {
        if (s.quickDrops && now - s.unlockedAt > kQuickDropMs) s.quickDrops = 0;
        continue;
      }
      if ((int32_t)(now - s.nextAttemptAt) >= 0) connectSlot(i);
    }

    // 3. Keep looking while there are free slots.
    bool freeSlot = false;
    for (uint8_t i = 0; i < maxPods_; i++) freeSlot |= !slots_[i].assigned;
    if (freeSlot && !scanning_) startScan();
    if (!freeSlot && scanning_) {
      NimBLEDevice::getScan()->stop();
      scanning_ = false;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void BlazePodDriver::startScan() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  // Once some pods are connected, scan gently so connection events have airtime.
  bool any = connectedCount() > 0;
  scan->setInterval(any ? 200 : 100);
  scan->setWindow(any ? 40 : 60);
  if (scan->start(0, false, true)) scanning_ = true;  // 0 = until stopped
}

bool BlazePodDriver::takeCandidate(Candidate& out) {
  Lock lock(mutex_);
  if (candidateCount_ == 0) return false;
  out = candidates_[0];
  for (uint8_t i = 1; i < candidateCount_; i++) candidates_[i - 1] = candidates_[i];
  candidateCount_--;
  return true;
}

bool BlazePodDriver::knownAddress(const NimBLEAddress& a) {
  for (uint8_t i = 0; i < maxPods_; i++)
    if (slots_[i].assigned && slots_[i].addr == a) return true;
  Lock lock(mutex_);
  for (uint8_t i = 0; i < candidateCount_; i++)
    if (candidates_[i].addr == a) return true;
  return false;
}

int BlazePodDriver::slotForClient(NimBLEClient* c) {
  for (uint8_t i = 0; i < maxPods_; i++)
    if (slots_[i].client == c) return i;
  return -1;
}

bool BlazePodDriver::connectSlot(uint8_t i) {
  Slot& s = slots_[i];
  const bool giveUpMode = s.status == PodStatus::NotResponding;
  if (!giveUpMode) s.status = PodStatus::Connecting;
  {
    Lock lock(mutex_);
    s.colourChr = nullptr;  // attributes are rebuilt on (re)connect
  }
  if (s.client == nullptr) {
    s.client = NimBLEDevice::createClient();
    s.client->setClientCallbacks(&clientCb_, false);
    s.client->setConnectTimeout(kConnectTimeoutMs);
    // 30-50 ms interval leaves airtime for 6 links; 3 s supervision timeout
    // means a pod that vanishes is noticed quickly.
    s.client->setConnectionParams(24, 40, 0, 300);
  }

  auto fail = [&](const char* why) {
    s.failedAttempts++;
    uint32_t backoff = s.failedAttempts < 3 ? 500u * s.failedAttempts : 2000u;
    if (giveUpMode || s.quickDrops >= kQuickDropsBeforeGivingUp) {
      s.status = PodStatus::NotResponding;
      backoff = kNotRespondingRetryMs;
    } else {
      s.status = PodStatus::Lost;
    }
    s.nextAttemptAt = millis() + backoff;
    ESP_LOGW(TAG, "pod %u: %s (attempt %u)", i + 1, why, s.failedAttempts);
    if (s.client->isConnected()) s.client->disconnect();
    return false;
  };

  uint32_t t0 = millis();
  if (!s.client->connect(s.addr, true)) return fail("connect failed");

  NimBLERemoteService* uart = s.client->getService(blazepod::kUartServiceUuid);
  NimBLERemoteCharacteristic* rx = uart ? uart->getCharacteristic(blazepod::kUartRxCharUuid) : nullptr;
  NimBLERemoteService* tapSvc = s.client->getService(blazepod::kTapServiceUuid);
  NimBLERemoteCharacteristic* tap = tapSvc ? tapSvc->getCharacteristic(blazepod::kTapCharUuid) : nullptr;
  NimBLERemoteService* colSvc = s.client->getService(blazepod::kColourServiceUuid);
  NimBLERemoteCharacteristic* col = colSvc ? colSvc->getCharacteristic(blazepod::kColourCharUuid) : nullptr;

  if (logGatt_ && !s.gattLogged) logGatt(s, i);

  if (!rx || !tap || !col) {
    s.quickDrops = kQuickDropsBeforeGivingUp;  // protocol mismatch: no point hammering it
    return fail("expected BlazePod services missing (newer pod firmware?)");
  }

  uint8_t auth[blazepod::kAuthPayloadLen];
  if (!blazepod::buildAuthPayload(s.mfr, s.mfrLen, auth)) return fail("manufacturer data too short");
  char authHex[16];
  hex(auth, sizeof(auth), authHex, sizeof(authHex));
  if (!rx->writeValue(auth, sizeof(auth), true)) return fail("unlock write rejected");

  if (!tap->subscribe(true, [this, i](NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
        onTapNotify(i, d, n);
      })) {
    return fail("could not subscribe to taps");
  }

  readBattery(s);

  {
    Lock lock(mutex_);
    s.colourChr = col;
  }
  s.unlockedAt = millis();
  s.failedAttempts = 0;
  s.status = PodStatus::Connected;
  ESP_LOGI(TAG, "pod %u connected+unlocked in %lu ms (auth %s, battery %d)", i + 1,
           (unsigned long)(s.unlockedAt - t0), authHex, s.battery.load());
  return true;
}

void BlazePodDriver::readBattery(Slot& s) {
  NimBLERemoteService* svc = s.client->getService(kBatteryService);
  NimBLERemoteCharacteristic* chr = svc ? svc->getCharacteristic(kBatteryLevel) : nullptr;
  if (!chr || !chr->canRead()) {
    s.battery = -1;
    return;
  }
  NimBLEAttValue v = chr->readValue();
  s.battery = v.size() >= 1 && v[0] <= 100 ? (int8_t)v[0] : -1;
}

void BlazePodDriver::logGatt(Slot& s, uint8_t i) {
  s.gattLogged = true;
  ESP_LOGI(TAG, "pod %u GATT table:", i + 1);
  for (NimBLERemoteService* svc : s.client->getServices(true)) {
    ESP_LOGI(TAG, "  svc %s", svc->getUUID().toString().c_str());
    for (NimBLERemoteCharacteristic* chr : svc->getCharacteristics(true)) {
      ESP_LOGI(TAG, "    chr %s %s%s%s%s", chr->getUUID().toString().c_str(), chr->canRead() ? "R" : "",
               chr->canWrite() ? "W" : "", chr->canWriteNoResponse() ? "w" : "", chr->canNotify() ? "N" : "");
    }
  }
}
