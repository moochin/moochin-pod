#include "PhoneServer.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "../storage/Results.h"
#include "index_html_gz.h"

static const char* TAG = "web";

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static DNSServer dns;
static const IPAddress kIp(192, 168, 4, 1);
static QueueHandle_t cmdQueue = nullptr;
static volatile bool newClient = false;

// Phones that have opened the page. Their connectivity checks get the
// "internet OK" answer, so the phone stays on PodHub (iOS shows "Done"
// instead of "Cancel", Android stops nagging). Everyone else is redirected
// to the page, which is what makes it pop up on joining.
static uint32_t knownIps[8];
static uint8_t knownCount = 0;
static portMUX_TYPE knownMux = portMUX_INITIALIZER_UNLOCKED;

static void rememberIp(uint32_t ip) {
  portENTER_CRITICAL(&knownMux);
  bool found = false;
  for (uint8_t i = 0; i < knownCount; i++) found |= knownIps[i] == ip;
  if (!found) {
    if (knownCount < 8) {
      knownIps[knownCount++] = ip;
    } else {
      memmove(knownIps, knownIps + 1, sizeof(knownIps) - sizeof(knownIps[0]));
      knownIps[7] = ip;
    }
  }
  portEXIT_CRITICAL(&knownMux);
}

static bool knownIp(uint32_t ip) {
  portENTER_CRITICAL(&knownMux);
  bool found = false;
  for (uint8_t i = 0; i < knownCount; i++) found |= knownIps[i] == ip;
  portEXIT_CRITICAL(&knownMux);
  return found;
}

static void redirectToPage(AsyncWebServerRequest* req) { req->redirect("http://192.168.4.1/"); }

static void servePage(AsyncWebServerRequest* req) {
  AsyncWebServerResponse* r = req->beginResponse(200, "text/html", kIndexHtmlGz, kIndexHtmlGzLen);
  r->addHeader("Content-Encoding", "gzip");
  r->addHeader("Cache-Control", "no-store");
  req->send(r);
}

// Parses one command from a phone. Runs in the web server task.
static void onWsData(AsyncWebSocketClient* client, const uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) return;
  const char* cmd = doc["cmd"] | "";
  WebCmd c{};
  if (!strcmp(cmd, "hello")) {
    c.type = WebCmd::Hello;
    JsonArrayConst t = doc["time"];
    if (t.size() == 6)
      for (uint8_t i = 0; i < 6; i++) c.time[i] = t[i] | 0;
    rememberIp((uint32_t)client->remoteIP());
  } else if (!strcmp(cmd, "start")) {
    c.type = WebCmd::Start;
  } else if (!strcmp(cmd, "stop")) {
    c.type = WebCmd::Stop;
  } else if (!strcmp(cmd, "done")) {
    c.type = WebCmd::Done;
  } else if (!strcmp(cmd, "repeat")) {
    c.type = WebCmd::Repeat;
  } else if (!strcmp(cmd, "drill")) {
    c.type = WebCmd::Drill;
    c.value = doc["drill"] | -1;
  } else if (!strcmp(cmd, "seconds")) {
    c.type = WebCmd::Seconds;
    c.value = doc["seconds"] | -1;
  } else if (!strcmp(cmd, "player")) {
    c.type = WebCmd::Player;
    c.value = doc["player"] | -1;
  } else if (!strcmp(cmd, "groups")) {
    c.type = WebCmd::Groups;
  } else if (!strcmp(cmd, "flash")) {
    c.type = WebCmd::Flash;
    c.value = doc["pod"] | -1;
  } else if (!strcmp(cmd, "rename")) {
    c.type = WebCmd::Rename;
    c.value = doc["player"] | -1;
    // Keep printable ASCII only: the Dial's fonts can't draw anything else.
    const char* name = doc["name"] | "";
    size_t n = 0;
    for (const char* p = name; *p && n < sizeof(c.text) - 1; p++)
      if (*p >= 32 && *p < 127) c.text[n++] = *p;
    c.text[n] = 0;
    while (n && c.text[n - 1] == ' ') c.text[--n] = 0;
    if (!n) return;
  } else {
    return;
  }
  if (cmdQueue) xQueueSend(cmdQueue, &c, 0);
}

static void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data,
                      size_t len) {
  if (type == WS_EVT_CONNECT) {
    // Each extra phone costs RAM: keep it to a few.
    if (ws.count() > 4) {
      client->close();
      return;
    }
    newClient = true;
    ESP_LOGI(TAG, "page opened by %s", client->remoteIP().toString().c_str());
  } else if (type == WS_EVT_DATA) {
    const AwsFrameInfo* info = (const AwsFrameInfo*)arg;
    // Commands are tiny: only accept complete single-frame text messages.
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT && len < 256)
      onWsData(client, data, len);
  }
}

void PhoneServer::start(StatusFn status) {
  if (running_) return;
  status_ = status;
  if (!cmdQueue) cmdQueue = xQueueCreate(8, sizeof(WebCmd));

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(kIp, kIp, IPAddress(255, 255, 255, 0));
  // Open network, max 4 phones. BLE coexists with Wi-Fi on the S3.
  WiFi.softAP(kSsid, nullptr, 1, 0, 4);

  // Answer every DNS name with our IP: that's what makes phones show the
  // "sign in to network" sheet, and makes http://pod.hub work.
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", kIp);

  if (!routesAdded_) {
    routesAdded_ = true;
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.on("/", HTTP_GET, servePage);
    server.on("/index.html", HTTP_GET, servePage);
    server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
      String json;
      json.reserve(768);
      if (status_) status_(json);
      AsyncWebServerResponse* r = req->beginResponse(200, "application/json", json);
      r->addHeader("Cache-Control", "no-store");
      req->send(r);
    });

    // History for the phone's History screen: ?player=N (default: everyone).
    server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest* req) {
      const int player = req->hasParam("player") ? req->getParam("player")->value().toInt() : -1;
      String json;
      json.reserve(8192);
      results::historyJson(json, player, 100);
      AsyncWebServerResponse* r = req->beginResponse(200, "application/json", json);
      r->addHeader("Cache-Control", "no-store");
      req->send(r);
    });
    // CSV download (one row per session): ?player=N (default: everyone).
    server.on("/api/export.csv", HTTP_GET, [](AsyncWebServerRequest* req) {
      const int player = req->hasParam("player") ? req->getParam("player")->value().toInt() : -1;
      if (!results::exportCsv("/export.csv", player)) {
        req->send(500, "text/plain", "Could not create the CSV");
        return;
      }
      AsyncWebServerResponse* r = req->beginResponse(LittleFS, "/export.csv", "text/csv");
      r->addHeader("Content-Disposition", "attachment; filename=\"podhub-history.csv\"");
      r->addHeader("Cache-Control", "no-store");
      req->send(r);
    });

    // Connectivity checks. Unknown phone -> redirect (opens the portal).
    // Phone that already has the page -> the expected "online" answer.
    auto apple = [](AsyncWebServerRequest* req) {
      if (knownIp((uint32_t)req->client()->remoteIP()))
        req->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
      else
        redirectToPage(req);
    };
    server.on("/hotspot-detect.html", HTTP_GET, apple);
    server.on("/library/test/success.html", HTTP_GET, apple);
    auto android = [](AsyncWebServerRequest* req) {
      if (knownIp((uint32_t)req->client()->remoteIP()))
        req->send(204);
      else
        redirectToPage(req);
    };
    server.on("/generate_204", HTTP_GET, android);
    server.on("/gen_204", HTTP_GET, android);
    server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* req) {
      if (knownIp((uint32_t)req->client()->remoteIP()))
        req->send(200, "text/plain", "Microsoft Connect Test");
      else
        redirectToPage(req);
    });
    server.onNotFound(redirectToPage);
  }
  server.begin();
  running_ = true;
  ESP_LOGI(TAG, "phone mode on: join Wi-Fi '%s', page at http://192.168.4.1/ (%u bytes gzipped)", kSsid,
           (unsigned)kIndexHtmlGzLen);
}

void PhoneServer::stop() {
  if (!running_) return;
  ws.closeAll();
  dns.stop();
  server.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  running_ = false;
  portENTER_CRITICAL(&knownMux);
  knownCount = 0;
  portEXIT_CRITICAL(&knownMux);
  ESP_LOGI(TAG, "phone mode off");
}

void PhoneServer::loop() {
  if (!running_) return;
  dns.processNextRequest();
  static uint32_t lastCleanup = 0;
  if (millis() - lastCleanup > 2000) {
    lastCleanup = millis();
    ws.cleanupClients(4);
  }
}

uint8_t PhoneServer::clients() const { return running_ ? WiFi.softAPgetStationNum() : 0; }

uint8_t PhoneServer::pageClients() const { return running_ ? (uint8_t)ws.count() : 0; }

bool PhoneServer::pollCommand(WebCmd& out) { return cmdQueue && xQueueReceive(cmdQueue, &out, 0) == pdTRUE; }

void PhoneServer::broadcast(const String& json) {
  if (!running_ || ws.count() == 0) return;
  if (!ws.availableForWriteAll()) return;  // a phone is behind: skip this update
  ws.textAll(json);
}

bool PhoneServer::takeNewClient() {
  if (!newClient) return false;
  newClient = false;
  return true;
}
