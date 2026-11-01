#include "mqtt.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "api.h"
#include "config.h"
#include "energy.h"
#include "log.h"
#include "net.h"
#include "outlets.h"
#include "settings.h"

namespace {

const uint32_t STATE_MIN_MS = 100;       // at most this often after an outlet change
const uint32_t STATE_ENERGY_MS = 10000;  // energy updates
const uint32_t STATE_MAX_MS = 60000;     // republish at least this often

WiFiClient g_net;
PubSubClient g_client(g_net);
String g_topic;
uint32_t g_lastAttempt = 0;
bool g_attempted = false;
uint32_t g_publishedChanges = 0;
uint32_t g_statePublishedAt = 0;
String g_lastEnergy;
uint32_t g_energyCheckedAt = 0;
bool g_stateSoon = false;

bool elapsed(uint32_t since, uint32_t ms) { return (uint32_t)(millis() - since) >= ms; }

String sub(const char *suffix) { return g_topic + "/" + suffix; }

void result(const char *command, bool ok, const String &message) {
  JsonDocument doc;
  doc["command"] = command;
  doc["ok"] = ok;
  doc["message"] = message;
  String out;
  serializeJson(doc, out);
  g_client.publish(sub("result").c_str(), out.c_str());
}

void publishState() {
  String state = Api::controllerJson(false);
  g_client.publish(sub("state").c_str(), state.c_str(), true);
  g_publishedChanges = Outlets::changeCount();
  g_statePublishedAt = millis();
  g_lastEnergy = Energy::json();
  g_stateSoon = false;
}

void onMessage(char *topicC, byte *payload, unsigned int len) {
  String t = topicC;
  String p;
  p.reserve(len);
  for (unsigned int i = 0; i < len; i++) p += (char)payload[i];
  if (!t.startsWith(g_topic + "/")) return;
  t = t.substring(g_topic.length() + 1);

  if (t == "get") {
    publishState();
  } else if (t.startsWith("outlet/") && t.endsWith("/set")) {
    String which = t.substring(7, t.length() - 4);
    String message;
    bool ok = Api::setOutlet(which, p, message);
    result("outlet", ok, message);
  } else if (t == "set") {
    // {"top":"on","bottom":"toggle"} or {"outlet1":"off"} or {"all":"off"}
    JsonDocument doc;
    if (deserializeJson(doc, p) || !doc.is<JsonObject>()) return result("set", false, "Payload must be a JSON object.");
    bool ok = true;
    String message;
    for (JsonPair kv : doc.as<JsonObject>()) {
      String key = kv.key().c_str();
      if (key.startsWith("outlet")) key = key.substring(6);
      String value = kv.value().is<bool>() ? String(kv.value().as<bool>() ? "on" : "off") : kv.value().as<String>();
      ok = Api::setOutlet(key, value, message) && ok;
    }
    result("set", ok, message);
  } else if (t == "press") {
    JsonDocument doc;
    String which = p;
    if (!deserializeJson(doc, p) && doc.is<JsonObject>()) which = doc["button"].as<String>();
    int i = Outlets::parse(which);
    if (i < 0 || i >= Outlets::COUNT) return result("press", false, "button must be 1-2, top or bottom.");
    Outlets::pressButton(i);
    result("press", true, String("Outlet ") + Outlets::key(i) + (Outlets::isOn(i) ? " on." : " off."));
  }
}

bool connect() {
  const Settings::Data &s = Settings::get();
  g_client.setServer(s.mqttHost, s.mqttPort);
  String client = Settings::hostname();
  String will = sub("status");
  logf("[mqtt] connecting to %s:%u", s.mqttHost, s.mqttPort);
  bool ok = s.mqttUser[0]
                ? g_client.connect(client.c_str(), s.mqttUser, s.mqttPass, will.c_str(), 0, true, "offline")
                : g_client.connect(client.c_str(), will.c_str(), 0, true, "offline");
  if (!ok) {
    logf("[mqtt] connect failed (state %d)", g_client.state());
    return false;
  }
  logf("[mqtt] connected, topic %s", g_topic.c_str());
  g_client.publish(will.c_str(), "online", true);
  g_client.publish((String("logix/discovery/") + Settings::deviceId()).c_str(), Api::infoJson().c_str(), true);
  g_client.subscribe(sub("get").c_str());
  g_client.subscribe(sub("set").c_str());
  g_client.subscribe(sub("press").c_str());
  g_client.subscribe(sub("outlet/+/set").c_str());
  publishState();
  return true;
}

}  // namespace

namespace Mqtt {

void begin() {
  g_topic = topic();
  g_client.setBufferSize(1024);
  g_client.setKeepAlive(30);
  g_client.setSocketTimeout(3);
  g_net.setTimeout(2000);  // an unreachable broker blocks loop() this long (buttons keep working)
  g_client.setCallback(onMessage);
}

void loop() {
  // Button events are taken even when they can't be sent, so they never arrive late in a burst.
  bool events[Outlets::COUNT];
  for (uint8_t i = 0; i < Outlets::COUNT; i++) events[i] = Outlets::takeButtonEvent(i);

  if (!enabled() || !Net::connected()) {
    if (g_client.connected()) g_client.disconnect();
    return;
  }
  if (!g_client.connected()) {
    if (g_attempted && !elapsed(g_lastAttempt, MQTT_RETRY_MS)) return;
    g_attempted = true;
    g_lastAttempt = millis();
    if (!connect()) return;
  }
  g_client.loop();

  for (uint8_t i = 0; i < Outlets::COUNT; i++) {
    if (events[i]) g_client.publish(sub(("button/" + String(i + 1) + "/action").c_str()).c_str(), "single");
  }

  bool outletsChanged = Outlets::changeCount() != g_publishedChanges;
  if ((outletsChanged || g_stateSoon) && elapsed(g_statePublishedAt, STATE_MIN_MS)) {
    publishState();
  } else if (elapsed(g_statePublishedAt, STATE_MAX_MS)) {
    publishState();
  } else if (elapsed(g_statePublishedAt, STATE_ENERGY_MS) && elapsed(g_energyCheckedAt, STATE_ENERGY_MS)) {
    g_energyCheckedAt = millis();
    if (Energy::json() != g_lastEnergy) publishState();
  }
}

String defaultTopic() { return String(LOGIX_MODEL "/") + Settings::deviceId(); }

String topic() {
  const char *t = Settings::get().mqttTopic;
  return t[0] ? String(t) : defaultTopic();
}

bool enabled() { return Settings::get().mqttHost[0] != '\0'; }
bool connected() { return g_client.connected(); }

void restart() {
  if (g_client.connected()) {
    g_client.publish(sub("status").c_str(), "offline", true);
    g_client.disconnect();
  }
  g_topic = topic();
  g_attempted = false;
}

String infoJson() {
  JsonDocument doc;
  JsonObject m = doc["mqtt"].to<JsonObject>();
  m["enabled"] = enabled();
  m["connected"] = connected();
  m["topic"] = topic();
  String out;
  serializeJson(doc, out);
  // Strip the outer braces: this is a member of the /api/info object.
  return out.substring(1, out.length() - 1);
}

void publishStateSoon() { g_stateSoon = true; }

}  // namespace Mqtt
