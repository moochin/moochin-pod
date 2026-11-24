#include "PhoneServer.h"

#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "status_page.h"

static const char* TAG = "web";

static AsyncWebServer server(80);
static DNSServer dns;
static const IPAddress kIp(192, 168, 4, 1);

void PhoneServer::start(StatusFn status) {
  if (running_) return;
  status_ = status;

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(kIp, kIp, IPAddress(255, 255, 255, 0));
  // Open network, max 4 phones. Channel 1 is fine; BLE coexists with it.
  WiFi.softAP(kSsid, nullptr, 1, 0, 4);

  // Answer every DNS name with our IP: that's what makes phones show the
  // "sign in to network" sheet, and makes http://pod.hub work.
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", kIp);

  if (!routesAdded_) {
    routesAdded_ = true;
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
      AsyncWebServerResponse* r = req->beginResponse(200, "text/html", reinterpret_cast<const uint8_t*>(kStatusPage), sizeof(kStatusPage) - 1);
      r->addHeader("Cache-Control", "no-store");
      req->send(r);
    });
    server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
      String json;
      json.reserve(768);
      if (status_) status_(json);
      AsyncWebServerResponse* r = req->beginResponse(200, "application/json", json);
      r->addHeader("Cache-Control", "no-store");
      req->send(r);
    });
    // Everything else, including the phones' connectivity checks
    // (iOS /hotspot-detect.html, Android /generate_204), goes to our page.
    // A redirect instead of the expected answer makes the phone open the
    // captive-portal sheet.
    server.onNotFound([](AsyncWebServerRequest* req) { req->redirect("http://192.168.4.1/"); });
  }
  server.begin();
  running_ = true;
  ESP_LOGI(TAG, "phone mode on: join Wi-Fi '%s', page at http://192.168.4.1/", kSsid);
}

void PhoneServer::stop() {
  if (!running_) return;
  dns.stop();
  server.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  running_ = false;
  ESP_LOGI(TAG, "phone mode off");
}

void PhoneServer::loop() {
  if (running_) dns.processNextRequest();
}

uint8_t PhoneServer::clients() const { return running_ ? WiFi.softAPgetStationNum() : 0; }
