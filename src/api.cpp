#include "api.h"

#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <StreamString.h>
#include <Updater.h>
#include <time.h>

#include "config.h"
#include "energy.h"
#include "log.h"
#include "mqtt.h"
#include "net.h"
#include "outlets.h"
#include "page.h"
#include "settings.h"

namespace {

const char *const ADMIN_USER = "admin";

ESP8266WebServer g_server(80);
bool g_restartPending = false;
uint32_t g_restartAt = 0;

bool g_updateStarted = false;
bool g_updateAuthOk = false;
bool g_updateFirstChunk = true;
String g_updateError;

void sendJson(int code, const String &body) {
  g_server.sendHeader("Cache-Control", "no-store");
  g_server.send(code, "application/json", body);
}

void sendResult(bool ok, const String &message, bool restart = false) {
  JsonDocument doc;
  doc["ok"] = ok;
  doc["message"] = message;
  doc["restart"] = restart;
  String out;
  serializeJson(doc, out);
  sendJson(ok ? 200 : 400, out);
}

bool authorize() {
  const char *admin = Settings::get().admin;
  if (!admin[0] || g_server.authenticate(ADMIN_USER, admin)) return true;
  g_server.requestAuthentication(BASIC_AUTH, LOGIX_MODEL_NAME);
  return false;
}

bool captiveRedirect() {
  if (!Net::apActive() || Net::isOwnHost(g_server.hostHeader())) return false;
  g_server.sendHeader("Location", "http://" + Net::apIp() + "/", true);
  g_server.send(302, "text/plain", "");
  return true;
}

String htmlEscape(const String &s) {
  String out;
  for (char c : s) {
    switch (c) {
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '&': out += "&amp;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

// Both strings in flash; strncmp_P only allows its second argument there.
bool startsWithP(PGM_P s, PGM_P prefix) {
  for (;; s++, prefix++) {
    char c = pgm_read_byte(prefix);
    if (!c) return true;
    if (pgm_read_byte(s) != c) return false;
  }
}

// Streams the page straight from flash, filling in %TITLE% and %VIEW% on the way, so it
// never needs a heap copy of the whole page.
void sendPage(bool settingsView) {
  if (captiveRedirect()) return;
  if (!authorize()) return;
  static const char TITLE[] PROGMEM = "%TITLE%";
  static const char VIEW[] PROGMEM = "%VIEW%";
  String title = htmlEscape(Settings::name());
  g_server.sendHeader("Cache-Control", "no-store");
  g_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  g_server.send(200, "text/html", "");
  PGM_P p = PAGE_HTML;
  for (;;) {
    PGM_P pct = p;
    while (pgm_read_byte(pct) && pgm_read_byte(pct) != '%') pct++;
    if (!pgm_read_byte(pct)) {
      g_server.sendContent_P(p, pct - p);
      break;
    }
    size_t skip = 0;
    if (startsWithP(pct, TITLE)) skip = strlen_P(TITLE);
    else if (startsWithP(pct, VIEW)) skip = strlen_P(VIEW);
    g_server.sendContent_P(p, pct - p + (skip ? 0 : 1));
    if (skip == strlen_P(TITLE)) g_server.sendContent(title);
    else if (skip) g_server.sendContent(settingsView ? "settings" : "main");
    p = pct + (skip ? skip : 1);
  }
  g_server.sendContent("");
}

String resetReason() { return ESP.getResetReason(); }

String isoNow() {
  time_t now = time(nullptr);
  if (now < 1700000000) return "";
  struct tm tm;
  localtime_r(&now, &tm);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &tm);
  return buf;
}

// ---- handlers ----

void handleInfo() { sendJson(200, Api::infoJson()); }  // public, so discovery can read the model

void handleStatus() {
  if (!authorize()) return;
  bool up = Net::connected();
  JsonDocument doc;
  doc["title"] = LOGIX_MODEL_NAME;
  doc["name"] = Settings::name();
  doc["version"] = FW_VERSION;
  doc["build"] = String(__DATE__) + " " + __TIME__;
  doc["mode"] = Net::apActive() ? "portal" : "station";
  doc["savedSsid"] = Settings::get().ssid;
  doc["connected"] = up;
  doc["ip"] = up ? WiFi.localIP().toString() : "";
  doc["rssi"] = up ? WiFi.RSSI() : 0;
  doc["channel"] = up ? WiFi.channel() : 0;
  doc["apSsid"] = Net::apActive() ? Net::apSsid() : "";
  doc["apIp"] = Net::apActive() ? Net::apIp() : "";
  doc["apClients"] = Net::apClients();
  doc["hostname"] = Settings::hostname();
  doc["mac"] = WiFi.macAddress();
  doc["uptime"] = millis() / 1000;
  doc["time"] = isoNow();
  doc["resetReason"] = resetReason();
  doc["heap"] = ESP.getFreeHeap();
  doc["chip"] = "ESP8285";
  doc["flash"] = ESP.getFlashChipRealSize();
  doc["sketch"] = ESP.getSketchSize();
  doc["otaSpace"] = ESP.getFreeSketchSpace();
  doc["sdk"] = ESP.getSdkVersion();
  doc["core"] = ESP.getCoreVersion();
  doc["authEnabled"] = Settings::get().admin[0] != '\0';
  doc["mqttConnected"] = Mqtt::connected();
  doc["adc"] = Outlets::adcRaw();
  doc["meter"] = serialized(Energy::debugJson());
  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

void handleController() {
  if (!authorize()) return;
  sendJson(200, Api::controllerJson(true));
}

void handleOutlet() {
  if (!authorize()) return;
  String message;
  bool ok = Api::setOutlet(g_server.arg("outlet"), g_server.hasArg("state") ? g_server.arg("state") : "toggle", message,
                           "http " + g_server.client().remoteIP().toString());
  sendResult(ok, message);
}

void handlePress() {
  if (!authorize()) return;
  int i = Outlets::parse(g_server.arg("button"));
  if (i < 0 || i >= Outlets::COUNT) return sendResult(false, "button must be 1-2, top or bottom.");
  Outlets::pressButton(i);
  logf("[cmd] press %s from http %s", Outlets::key(i), g_server.client().remoteIP().toString().c_str());
  sendResult(true, String("Outlet ") + Outlets::key(i) + (Outlets::isOn(i) ? " on." : " off."));
}

void handleEnergyGet() {
  if (!authorize()) return;
  JsonDocument doc;
  doc["energy"] = serialized(Energy::json());
  Energy::Calibration cal = Energy::calibration();
  doc["powerCal"] = cal.power;
  doc["voltageCal"] = cal.voltage;
  doc["currentCal"] = cal.current;
  doc["totalStartEpoch"] = Energy::totalStartTime();
  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

double argNumber(const char *name, double fallback) {
  if (!g_server.hasArg(name) || g_server.arg(name).isEmpty()) return fallback;
  return g_server.arg(name).toDouble();
}

void handleEnergyPost() {
  if (!authorize()) return;
  if (g_server.arg("reset") == "1") {
    Energy::reset();
    return sendResult(true, "Energy counters reset.");
  }
  Energy::Calibration cal = {(uint32_t)argNumber("powerCal", 0), (uint32_t)argNumber("voltageCal", 0),
                             (uint32_t)argNumber("currentCal", 0)};
  if (cal.power || cal.voltage || cal.current) Energy::setCalibration(cal);
  double total = argNumber("total", -1), today = argNumber("today", -1), yesterday = argNumber("yesterday", -1);
  uint32_t start = (uint32_t)argNumber("totalStartEpoch", 0);
  if (total >= 0 || today >= 0 || yesterday >= 0 || start) Energy::setTotals(total, today, yesterday, start);
  sendResult(true, "Energy settings saved.");
}

void handleCalibrate() {
  if (!authorize()) return;
  String err = Energy::calibrateTo(argNumber("voltage", 0), argNumber("power", 0), argNumber("current", 0));
  if (!err.isEmpty()) return sendResult(false, err);
  sendResult(true, "Calibrated.");
}

void handleScan() {
  if (!authorize()) return;
  int n = WiFi.scanComplete();
  if (g_server.hasArg("fresh") && n >= 0) {
    WiFi.scanDelete();
    n = WIFI_SCAN_FAILED;
  }
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
    return sendJson(200, "{\"scanning\":true}");
  }
  if (n == WIFI_SCAN_RUNNING) return sendJson(200, "{\"scanning\":true}");

  JsonDocument doc;
  doc["scanning"] = false;
  JsonArray nets = doc["networks"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    bool dup = false;
    for (int k = 0; k < n && !dup; k++) {
      dup = k != i && WiFi.SSID(k) == ssid && (WiFi.RSSI(k) > WiFi.RSSI(i) || (WiFi.RSSI(k) == WiFi.RSSI(i) && k < i));
    }
    if (dup) continue;
    JsonObject o = nets.add<JsonObject>();
    o["ssid"] = ssid;
    o["rssi"] = WiFi.RSSI(i);
    o["secure"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
  }
  WiFi.scanDelete();
  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

void handleWifiSave() {
  if (!authorize()) return;
  String ssid = g_server.arg("ssid");
  String pass = g_server.arg("pass");
  ssid.trim();
  if (ssid.isEmpty() || ssid.length() > 32) return sendResult(false, "SSID must be 1-32 characters.");
  if (pass.length() > 0 && (pass.length() < 8 || pass.length() > 63))
    return sendResult(false, "Password must be empty (open network) or 8-63 characters.");
  Settings::Data &s = Settings::get();
  strlcpy(s.ssid, ssid.c_str(), sizeof(s.ssid));
  strlcpy(s.pass, pass.c_str(), sizeof(s.pass));
  if (!Settings::save()) return sendResult(false, "Failed to save WiFi settings to flash.");
  sendResult(true, "Saved. Restarting to join \"" + ssid + "\". If it can't connect within a minute, the setup AP \"" +
                       Net::apSsid() + "\" comes back.", true);
  Api::scheduleRestart(1500);
}

void handleForget() {
  if (!authorize()) return;
  Settings::Data &s = Settings::get();
  s.ssid[0] = s.pass[0] = '\0';
  if (!Settings::save()) return sendResult(false, "Failed to erase WiFi settings.");
  sendResult(true, "WiFi settings erased. Restarting into the setup AP \"" + Net::apSsid() + "\".", true);
  Api::scheduleRestart(1500);
}

void handleDevice() {
  if (!authorize()) return;
  Settings::Data &s = Settings::get();
  bool restart = false;
  if (g_server.hasArg("name")) {
    String oldHost = Settings::hostname();
    String err = Settings::setName(g_server.arg("name"));
    if (!err.isEmpty()) return sendResult(false, err);
    restart = Settings::hostname() != oldHost;
    Mqtt::publishStateSoon();
  }
  if (g_server.arg("adminClear") == "1") {
    s.admin[0] = '\0';
  } else if (g_server.arg("admin").length() > 0) {
    String a = g_server.arg("admin");
    if (a.length() < 4 || a.length() > 64) return sendResult(false, "Admin password must be 4-64 characters.");
    strlcpy(s.admin, a.c_str(), sizeof(s.admin));
  }
  if (!Settings::save()) return sendResult(false, "Failed to save device settings.");
  sendResult(true, restart ? "Saved. Restarting to apply the new hostname " + Settings::hostname() + "." : "Saved.", restart);
  if (restart) Api::scheduleRestart(1500);
}

void handleMqttGet() {
  if (!authorize()) return;
  const Settings::Data &s = Settings::get();
  JsonDocument doc;
  doc["enabled"] = Mqtt::enabled();
  doc["connected"] = Mqtt::connected();
  doc["host"] = s.mqttHost;
  doc["port"] = s.mqttPort;
  doc["user"] = s.mqttUser;
  doc["passSet"] = s.mqttPass[0] != '\0';
  doc["topic"] = Mqtt::topic();
  doc["defaultTopic"] = Mqtt::defaultTopic();
  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

void handleMqttPost() {
  if (!authorize()) return;
  Settings::Data &s = Settings::get();
  String host = g_server.arg("host"), user = g_server.arg("user"), topic = g_server.arg("topic");
  host.trim();
  user.trim();
  topic.trim();
  while (topic.endsWith("/")) topic.remove(topic.length() - 1);
  long port = g_server.hasArg("port") && g_server.arg("port").length() ? g_server.arg("port").toInt() : 1883;
  if (host.length() > 64 || user.length() > 64 || topic.length() > 64) return sendResult(false, "Host, user and topic must be at most 64 characters.");
  if (port < 1 || port > 65535) return sendResult(false, "Port must be 1-65535.");
  if (topic.indexOf('#') >= 0 || topic.indexOf('+') >= 0) return sendResult(false, "Topic can't contain # or +.");
  if (topic == Mqtt::defaultTopic()) topic = "";
  strlcpy(s.mqttHost, host.c_str(), sizeof(s.mqttHost));
  s.mqttPort = port;
  strlcpy(s.mqttUser, user.c_str(), sizeof(s.mqttUser));
  strlcpy(s.mqttTopic, topic.c_str(), sizeof(s.mqttTopic));
  if (g_server.arg("passClear") == "1") {
    s.mqttPass[0] = '\0';
  } else if (g_server.arg("pass").length() > 0) {
    if (g_server.arg("pass").length() > 64) return sendResult(false, "Password must be at most 64 characters.");
    strlcpy(s.mqttPass, g_server.arg("pass").c_str(), sizeof(s.mqttPass));
  }
  if (!Settings::save()) return sendResult(false, "Failed to save MQTT settings.");
  Mqtt::restart();
  sendResult(true, host.isEmpty() ? "MQTT turned off." : "Saved. Connecting to " + host + ".");
}

void handleLog() {
  if (!authorize()) return;
  g_server.sendHeader("Cache-Control", "no-store");
  g_server.send(200, "text/plain", logText());
}

void handleReboot() {
  if (!authorize()) return;
  sendResult(true, "Restarting.", true);
  Api::scheduleRestart(1000);
}

String updaterError() {
  StreamString s;
  Update.printError(s);
  return s.length() ? String(s.c_str()) : String("Unknown error");
}

void handleUpdateUpload() {
  HTTPUpload &up = g_server.upload();
  if (up.status == UPLOAD_FILE_START) {
    g_updateStarted = true;
    g_updateFirstChunk = true;
    g_updateError = "";
    const char *admin = Settings::get().admin;
    g_updateAuthOk = !admin[0] || g_server.authenticate(ADMIN_USER, admin);
    if (!g_updateAuthOk) {
      g_updateError = "Unauthorized";
      return;
    }
    logf("[ota] receiving %s", up.filename.c_str());
    Energy::save();
    size_t space = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (space == 0 || !Update.begin(space)) g_updateError = updaterError();
    return;
  }
  if (!g_updateAuthOk || !g_updateError.isEmpty()) {
    if ((up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) && Update.isRunning()) Update.end();
    return;
  }
  if (up.status == UPLOAD_FILE_WRITE) {
    if (g_updateFirstChunk) {
      g_updateFirstChunk = false;
      // 0xE9: plain ESP8266 image; 0x1F 0x8B: gzip, which the bootloader unpacks.
      bool plain = up.currentSize >= 16 && up.buf[0] == 0xE9;
      bool gz = up.currentSize >= 16 && up.buf[0] == 0x1F && up.buf[1] == 0x8B;
      if (!plain && !gz) {
        g_updateError = "Not an ESP8266 firmware image (.bin or .bin.gz).";
        Update.end();
        return;
      }
    }
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      g_updateError = updaterError();
      if (Update.isRunning()) Update.end();
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) {
      g_updateError = updaterError();
    } else {
      logf("[ota] received %u bytes", (unsigned)up.totalSize);
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (Update.isRunning()) Update.end();
    g_updateError = "Upload aborted.";
  }
}

void handleUpdateDone() {
  g_server.sendHeader("Connection", "close");
  if (!g_updateStarted) {
    if (!authorize()) return;
    return sendResult(false, "No firmware file received.");
  }
  g_updateStarted = false;
  if (!g_updateAuthOk) return g_server.requestAuthentication();
  if (!g_updateError.isEmpty()) {
    logf("[ota] failed: %s", g_updateError.c_str());
    return sendResult(false, "Update failed: " + g_updateError);
  }
  sendResult(true, "Update successful. Restarting into the new firmware.", true);
  Api::scheduleRestart(1500);
}

void handleNotFound() {
  if (captiveRedirect()) return;
  g_server.send(404, "text/plain", "Not found");
}

}  // namespace

namespace Api {

void begin() {
  g_server.on("/", HTTP_GET, [] { sendPage(Net::apActive()); });  // the setup AP opens on settings
  g_server.on("/settings", HTTP_GET, [] { sendPage(true); });
  g_server.on("/api/info", HTTP_GET, handleInfo);
  g_server.on("/api/status", HTTP_GET, handleStatus);
  g_server.on("/api/controller", HTTP_GET, handleController);
  g_server.on("/api/controller/press", HTTP_POST, handlePress);
  g_server.on("/api/outlet", HTTP_POST, handleOutlet);
  g_server.on("/api/energy", HTTP_GET, handleEnergyGet);
  g_server.on("/api/energy", HTTP_POST, handleEnergyPost);
  g_server.on("/api/energy/calibrate", HTTP_POST, handleCalibrate);
  g_server.on("/api/scan", HTTP_GET, handleScan);
  g_server.on("/api/wifi", HTTP_POST, handleWifiSave);
  g_server.on("/api/forget", HTTP_POST, handleForget);
  g_server.on("/api/device", HTTP_POST, handleDevice);
  g_server.on("/api/mqtt", HTTP_GET, handleMqttGet);
  g_server.on("/api/mqtt", HTTP_POST, handleMqttPost);
  g_server.on("/api/log", HTTP_GET, handleLog);
  g_server.on("/api/reboot", HTTP_POST, handleReboot);
  g_server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  g_server.onNotFound(handleNotFound);
  g_server.begin();
}

void loop() {
  g_server.handleClient();
  if (g_restartPending && (int32_t)(millis() - g_restartAt) >= 0) {
    logf("[sys] restarting");
    Energy::save();
    delay(100);
    ESP.restart();
  }
}

String infoJson() {
  JsonDocument doc;
  doc["manufacturer"] = LOGIX_MANUFACTURER;
  doc["model"] = LOGIX_MODEL;
  doc["name"] = Settings::name();
  doc["id"] = Settings::deviceId();
  doc["mac"] = WiFi.macAddress();
  doc["hostname"] = Settings::hostname();
  doc["ip"] = Net::connected() ? WiFi.localIP().toString() : "";
  doc["fw"] = FW_VERSION;
  doc["api"] = LOGIX_API_VERSION;
  doc["authRequired"] = Settings::get().admin[0] != '\0';
  JsonObject caps = doc["capabilities"].to<JsonObject>();
  caps["outlets"] = Outlets::COUNT;
  caps["buttons"] = Outlets::COUNT;
  JsonArray keys = caps["outletKeys"].to<JsonArray>();
  JsonArray names = caps["outletNames"].to<JsonArray>();
  for (uint8_t i = 0; i < Outlets::COUNT; i++) {
    keys.add(Outlets::key(i));
    names.add(Settings::outletName(i));
  }
  caps["actions"].to<JsonArray>().add("single");
  caps["energy"] = true;
  caps["energyMeters"] = 1;  // one meter for both outlets combined
  String out;
  serializeJson(doc, out);
  // Append the "mqtt" member built by the MQTT module.
  out.remove(out.length() - 1);
  out += "," + Mqtt::infoJson() + "}";
  return out;
}

String controllerJson(bool live) {
  JsonDocument doc;
  if (live) doc["uptimeMs"] = millis();
  doc["name"] = Settings::name();
  JsonArray outlets = doc["outlets"].to<JsonArray>();
  JsonArray buttons = doc["buttons"].to<JsonArray>();
  for (uint8_t i = 0; i < Outlets::COUNT; i++) {
    JsonObject o = outlets.add<JsonObject>();
    o["outlet"] = i + 1;
    o["key"] = Outlets::key(i);
    o["name"] = Settings::outletName(i);
    o["state"] = Outlets::isOn(i) ? "on" : "off";

    JsonObject b = buttons.add<JsonObject>();
    b["button"] = i + 1;
    b["key"] = Outlets::key(i);
    b["pressed"] = Outlets::buttonPressed(i);
    b["pressCount"] = Outlets::buttonPressCount(i);
    b["lastAction"] = Outlets::buttonActionCount(i) ? "single" : "none";
    b["actionCount"] = Outlets::buttonActionCount(i);
    if (live) {
      int32_t ago = Outlets::buttonLastActionMsAgo(i);
      if (ago < 0) {
        b["lastActionMsAgo"] = nullptr;
      } else {
        b["lastActionMsAgo"] = ago;
      }
    }
  }
  doc["energy"] = serialized(Energy::json());
  String out;
  serializeJson(doc, out);
  return out;
}

bool setOutlet(const String &which, const String &stateArg, String &message, const String &source) {
  int i = Outlets::parse(which);
  if (i < 0) {
    message = "outlet must be 1, 2, top, bottom or all.";
    return false;
  }
  String state = stateArg;
  state.trim();
  state.toLowerCase();
  int mode = state == "on" || state == "1" || state == "true"     ? 1
             : state == "off" || state == "0" || state == "false" ? 0
             : state == "toggle" || state.isEmpty()               ? 2
                                                                  : -1;
  if (mode < 0) {
    message = "state must be on, off or toggle.";
    return false;
  }
  uint8_t first = i == Outlets::COUNT ? 0 : i;
  uint8_t last = i == Outlets::COUNT ? Outlets::COUNT - 1 : i;
  for (uint8_t k = first; k <= last; k++) {
    if (mode == 2) {
      Outlets::toggle(k);
    } else {
      Outlets::set(k, mode == 1);
    }
  }
  message = i == Outlets::COUNT ? String("All outlets ") + (mode == 2 ? "toggled." : mode ? "on." : "off.")
                                : String("Outlet ") + Outlets::key(i) + (Outlets::isOn(i) ? " on." : " off.");
  logf("[cmd] %s %s from %s", Outlets::key(i), state.isEmpty() ? "toggle" : state.c_str(), source.c_str());
  return true;
}

void scheduleRestart(uint32_t delayMs) {
  g_restartPending = true;
  g_restartAt = millis() + delayMs;
}

}  // namespace Api
