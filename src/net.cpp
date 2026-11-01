#include "net.h"

#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <time.h>

#include "config.h"
#include "log.h"
#include "settings.h"

namespace {

const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_MASK(255, 255, 255, 0);

DNSServer g_dns;
String g_apSsid;
bool g_ap = false;
bool g_wasConnected = false;
bool g_mdns = false;
uint32_t g_downSince = 0;     // station not connected since (millis)
uint32_t g_retryAt = 0;       // AP mode: last attempt to join the saved network
bool g_retrying = false;
uint32_t g_ledAt = 0;
bool g_led = false;

bool elapsed(uint32_t since, uint32_t ms) { return (uint32_t)(millis() - since) >= ms; }
bool haveSsid() { return Settings::get().ssid[0] != '\0'; }

void beginStation() {
  const Settings::Data &s = Settings::get();
  WiFi.begin(s.ssid, s.pass);
}

void startAp() {
  logf("[wifi] starting setup AP \"%s\" at %s", g_apSsid.c_str(), AP_IP.toString().c_str());
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
  WiFi.softAP(g_apSsid.c_str());
  g_dns.setErrorReplyCode(DNSReplyCode::NoError);
  g_dns.start(53, "*", AP_IP);
  g_ap = true;
  g_retrying = false;
  g_retryAt = millis();
}

void stopAp() {
  logf("[wifi] connected again, stopping setup AP");
  g_dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  g_ap = false;
}

void onConnected() {
  logf("[wifi] connected to \"%s\", IP %s, http://%s.local/", WiFi.SSID().c_str(),
       WiFi.localIP().toString().c_str(), Settings::hostname().c_str());
  if (!g_mdns) {
    String host = Settings::hostname();
    g_mdns = MDNS.begin(host.c_str());
    if (g_mdns) {
      MDNS.addService("http", "tcp", 80);
      MDNS.addService("logix", "tcp", 80);
      MDNS.addServiceTxt("logix", "tcp", "model", LOGIX_MODEL);
      MDNS.addServiceTxt("logix", "tcp", "id", Settings::deviceId().c_str());
      MDNS.addServiceTxt("logix", "tcp", "name", Settings::name().c_str());
      MDNS.addServiceTxt("logix", "tcp", "fw", FW_VERSION);
      MDNS.addServiceTxt("logix", "tcp", "api", String(LOGIX_API_VERSION).c_str());
      MDNS.addServiceTxt("logix", "tcp", "manufacturer", LOGIX_MANUFACTURER);
    }
  } else {
    MDNS.notifyAPChange();
  }
}

// AP mode: try the saved network now and then, but never while someone is using the portal
// (joining a network makes the AP change channel and drops its clients).
void apRetryTick() {
  if (!haveSsid()) return;
  if (g_retrying) {
    if (WiFi.status() == WL_CONNECTED) {
      g_retrying = false;
    } else if (elapsed(g_retryAt, WIFI_AP_ATTEMPT_MS)) {
      WiFi.disconnect(false);
      g_retrying = false;
      g_retryAt = millis();
    }
    return;
  }
  if (elapsed(g_retryAt, WIFI_AP_RETRY_MS) && WiFi.softAPgetStationNum() == 0) {
    logf("[wifi] retrying \"%s\"", Settings::get().ssid);
    beginStation();
    g_retrying = true;
    g_retryAt = millis();
  }
}

// GPIO2 LED (inside the box): off when connected, fast blink while connecting, slow in AP mode.
void ledTick(bool connected) {
  uint32_t period = connected ? 0 : g_ap ? 1000 : 200;
  if (period == 0) {
    if (g_led) digitalWrite(PIN_LED_LINK, HIGH);
    g_led = false;
    return;
  }
  if (!elapsed(g_ledAt, period)) return;
  g_ledAt = millis();
  g_led = !g_led;
  digitalWrite(PIN_LED_LINK, g_led ? LOW : HIGH);
}

}  // namespace

namespace Net {

void begin() {
  digitalWrite(PIN_LED_LINK, HIGH);
  pinMode(PIN_LED_LINK, OUTPUT);

  String suffix = Settings::deviceId().substring(8);
  suffix.toUpperCase();
  g_apSsid = "Logix-Outlet-" + suffix;

  configTime(TZ_POSIX, NTP_SERVER1, NTP_SERVER2);

  WiFi.persistent(false);  // credentials live in our own settings, not the SDK's
  WiFi.mode(WIFI_STA);
  WiFi.hostname(Settings::hostname());
  WiFi.setAutoReconnect(true);
  g_downSince = millis();
  if (haveSsid()) {
    logf("[wifi] connecting to \"%s\"", Settings::get().ssid);
    beginStation();
  } else {
    startAp();
  }
}

void loop() {
  bool up = WiFi.status() == WL_CONNECTED;
  if (up && !g_wasConnected) {
    onConnected();
  } else if (!up && g_wasConnected) {
    logf("[wifi] connection lost");
    g_downSince = millis();
  }
  g_wasConnected = up;

  if (g_ap) {
    g_dns.processNextRequest();
    if (up && WiFi.softAPgetStationNum() == 0) {
      stopAp();
    } else if (!up) {
      apRetryTick();
    }
  } else if (!up && elapsed(g_downSince, WIFI_AP_FALLBACK_MS)) {
    startAp();
  }

  if (up && g_mdns) MDNS.update();
  ledTick(up);
}

bool connected() { return WiFi.status() == WL_CONNECTED; }
bool apActive() { return g_ap; }
String apSsid() { return g_apSsid; }
String apIp() { return AP_IP.toString(); }
uint8_t apClients() { return g_ap ? WiFi.softAPgetStationNum() : 0; }

bool isOwnHost(const String &hostHeader) {
  String h = hostHeader;
  int colon = h.indexOf(':');
  if (colon >= 0) h = h.substring(0, colon);
  h.toLowerCase();
  String host = Settings::hostname();
  return h.isEmpty() || h == AP_IP.toString() || h == WiFi.localIP().toString() || h == host ||
         h == host + ".local";
}

void reconnect() {
  logf("[wifi] joining \"%s\"", Settings::get().ssid);
  g_downSince = millis();
  if (g_ap) {
    // Stay in AP mode until the station is up, so a wrong password can be fixed.
    WiFi.disconnect(false);
    beginStation();
    g_retrying = true;
    g_retryAt = millis();
  } else {
    WiFi.disconnect(false);
    beginStation();
  }
}

}  // namespace Net
