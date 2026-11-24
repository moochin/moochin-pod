// "Phone mode": the hub's own Wi-Fi network (open, SSID "PodHub") with a
// captive portal, so joining the network opens the page automatically.
// Started only on demand, to keep RAM for BLE when no phone is used.
#pragma once

#include <Arduino.h>
#include <functional>

class PhoneServer {
 public:
  static constexpr const char* kSsid = "PodHub";

  // Fills `out` with the /api/status JSON. Called from the web server task.
  using StatusFn = std::function<void(String& out)>;

  void start(StatusFn status);
  void stop();
  void loop();  // call from loop(): answers DNS for the captive portal
  bool running() const { return running_; }
  uint8_t clients() const;

 private:
  bool running_ = false;
  bool routesAdded_ = false;
  StatusFn status_;
};
