// "Phone mode": the hub's own Wi-Fi network (open, SSID "PodHub") with a
// captive portal, so joining the network opens the control page by itself.
// Started only on demand, to keep RAM for BLE when no phone is used.
//
// The page talks to the hub over a WebSocket (/ws). Commands from phones are
// parsed in the web server task and queued; the main loop drains them with
// pollCommand(), so all hub state is only ever touched from loop().
#pragma once

#include <Arduino.h>
#include <functional>

struct WebCmd {
  enum Type : uint8_t { Hello, Start, Stop, Done, Repeat, Drill, Seconds, Player, Rename, Flash, Groups };
  Type type;
  int16_t value;      // drill / seconds / player / pod
  uint16_t time[6];   // Hello: phone's local Y, M, D, h, m, s (0 = not sent)
  char text[16];      // Rename: new name
};

class PhoneServer {
 public:
  static constexpr const char* kSsid = "PodHub";

  // Fills `out` with the /api/status JSON (diagnostics). Called from the web task.
  using StatusFn = std::function<void(String& out)>;

  void start(StatusFn status);
  void stop();
  void loop();  // call from loop(): DNS for the captive portal, WS housekeeping
  bool running() const { return running_; }
  uint8_t clients() const;   // phones joined to the Wi-Fi
  uint8_t pageClients() const;  // phones with the page open (WebSocket)

  bool pollCommand(WebCmd& out);
  // Sends a state message to every open page. Skipped if a phone is slow to
  // read, so a stalled phone can't eat the hub's memory.
  void broadcast(const String& json);
  // True once after a page connects: send it the current state.
  bool takeNewClient();

 private:
  bool running_ = false;
  bool routesAdded_ = false;
  StatusFn status_;
};
