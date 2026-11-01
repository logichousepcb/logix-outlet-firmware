#include "settings.h"

#include <ESP8266WiFi.h>

#include <memory>

#include "config.h"
#include "energy.h"
#include "log.h"
#include "storage.h"

namespace {

const uint32_t SETTINGS_MAGIC = 0x4C4F5531;  // "LOU1"
const char *const SETTINGS_PATH = "/settings.bin";

Settings::Data g_s = {};

template <size_t N>
void setField(char (&field)[N], const String &value) {
  memset(field, 0, N);
  strncpy(field, value.c_str(), N - 1);
}

template <size_t N>
void terminate(char (&field)[N]) {
  field[N - 1] = '\0';
}

// ---- Tasmota import ----
// Tasmota 12 on a 1 MB ESP8266 keeps its Settings struct in the sector at 0xFB000 (DEVICE.md
// §9). Offsets were confirmed against this device's own config dump.
const uint32_t TASMOTA_SECTOR = 0xFB;
const size_t TASMOTA_SIZE = 4096;
const size_t T_TEXT_POOL = 0x017, T_TEXT_POOL_LEN = 699;
const size_t T_ENERGY_TOTAL = 0x32C;  // uint32, 0.01 Wh
const size_t T_POWER_CAL = 0x364, T_VOLTAGE_CAL = 0x368, T_CURRENT_CAL = 0x36C;
const size_t T_ENERGY_START = 0x7B4;  // uint32 epoch
const size_t T_MQTT_PORT = 0xEFC;     // uint16
// Text pool order (SettingsTextIndex)
enum { T_SSID1 = 4, T_PWD1 = 6, T_MQTT_HOST = 12, T_MQTT_USER = 14, T_MQTT_PWD = 15, T_TEXTS = 16 };

uint32_t u32(const uint8_t *b, size_t off) { uint32_t v; memcpy(&v, b + off, 4); return v; }
uint16_t u16(const uint8_t *b, size_t off) { uint16_t v; memcpy(&v, b + off, 2); return v; }

bool importTasmota() {
  std::unique_ptr<uint32_t[]> buf(new (std::nothrow) uint32_t[TASMOTA_SIZE / 4]);
  if (!buf || !ESP.flashRead(TASMOTA_SECTOR * SPI_FLASH_SEC_SIZE, buf.get(), TASMOTA_SIZE)) return false;
  const uint8_t *b = reinterpret_cast<const uint8_t *>(buf.get());

  uint16_t cfgSize = u16(b, 2);
  uint32_t version = u32(b, 8);
  uint8_t major = version >> 24;
  if (cfgSize != TASMOTA_SIZE || major < 9 || major > 14) return false;

  const char *texts[T_TEXTS];
  const char *p = reinterpret_cast<const char *>(b + T_TEXT_POOL);
  const char *end = p + T_TEXT_POOL_LEN;
  for (int i = 0; i < T_TEXTS; i++) {
    const char *nul = static_cast<const char *>(memchr(p, '\0', end - p));
    if (!nul) return false;
    texts[i] = p;
    p = nul + 1;
  }
  if (strncmp(texts[0], "http", 4) != 0) return false;  // OTA URL: sanity check of the layout
  size_t ssidLen = strlen(texts[T_SSID1]);
  if (ssidLen == 0 || ssidLen > 32 || strlen(texts[T_PWD1]) > 64) return false;

  setField(g_s.ssid, texts[T_SSID1]);
  setField(g_s.pass, texts[T_PWD1]);
  setField(g_s.mqttHost, texts[T_MQTT_HOST]);
  setField(g_s.mqttUser, texts[T_MQTT_USER]);
  setField(g_s.mqttPass, texts[T_MQTT_PWD]);
  uint16_t port = u16(b, T_MQTT_PORT);
  g_s.mqttPort = port ? port : 1883;
  logf("[tasmota] imported %u.%u.%u settings: WiFi \"%s\", MQTT %s:%u", major, (version >> 16) & 0xFF,
       (version >> 8) & 0xFF, g_s.ssid, g_s.mqttHost[0] ? g_s.mqttHost : "(off)", g_s.mqttPort);

  if (major == 12) {
    uint32_t total = u32(b, T_ENERGY_TOTAL);
    uint32_t start = u32(b, T_ENERGY_START);
    Energy::Calibration cal = {u32(b, T_POWER_CAL), u32(b, T_VOLTAGE_CAL), u32(b, T_CURRENT_CAL)};
    bool calOk = cal.power > 1000 && cal.power < 100000 && cal.voltage > 100 && cal.voltage < 20000 &&
                 cal.current > 100 && cal.current < 50000;
    if (total < 100000000 && start > 1400000000 && calOk) {
      Energy::setCalibration(cal);
      Energy::setTotals(total / 100000.0, -1, -1, start);
      logf("[tasmota] imported energy total %.3f kWh", total / 100000.0);
    }
  }
  return true;
}

void eraseTasmota() {
  // Tasmota's copy holds the WiFi and MQTT passwords; don't leave it behind once imported.
  if (!ESP.flashEraseSector(TASMOTA_SECTOR)) logf("[tasmota] could not erase the old settings sector");
}

}  // namespace

namespace Settings {

void begin() {
  bool loaded = Storage::load(SETTINGS_PATH, g_s, SETTINGS_MAGIC);
  if (!loaded) {
    g_s = {};
    g_s.mqttPort = 1883;
  }
  terminate(g_s.name);
  terminate(g_s.ssid);
  terminate(g_s.pass);
  terminate(g_s.admin);
  terminate(g_s.mqttHost);
  terminate(g_s.mqttUser);
  terminate(g_s.mqttPass);
  terminate(g_s.mqttTopic);
  if (!g_s.name[0] || hostnameFor(g_s.name).isEmpty()) setField(g_s.name, defaultName());

  if (!g_s.tasmotaImported) {
    bool imported = importTasmota();
    g_s.tasmotaImported = 1;
    if (save() && imported) eraseTasmota();
  } else if (!loaded) {
    save();
  }
}

Data &get() { return g_s; }

bool save() {
  if (Storage::save(SETTINGS_PATH, g_s, SETTINGS_MAGIC)) return true;
  logf("[settings] failed to save");
  return false;
}

String deviceId() {
  String id = WiFi.macAddress();
  id.replace(":", "");
  id.toLowerCase();
  return id;
}

String defaultName() {
  String suffix = deviceId().substring(8);
  suffix.toUpperCase();
  return String(LOGIX_MODEL_NAME " ") + suffix;
}

String name() { return g_s.name; }

String outletName(uint8_t index) { return name() + (index == 0 ? " Top" : " Bottom"); }

String hostnameFor(const String &n) {
  String h;
  bool dash = false;
  for (size_t i = 0; i < n.length() && h.length() < 32; i++) {
    char c = tolower(n[i]);
    if (isalnum(c)) {
      if (dash && h.length()) {
        if (h.length() >= 31) break;
        h += '-';
      }
      h += c;
      dash = false;
    } else {
      dash = true;
    }
  }
  return h;
}

String hostname() { return hostnameFor(g_s.name); }

String setName(const String &value) {
  String n = value;
  n.trim();
  if (n.isEmpty() || n.length() > 32) return "Name must be 1-32 characters.";
  for (size_t i = 0; i < n.length(); i++) {
    if ((uint8_t)n[i] < 0x20 || n[i] == '"' || n[i] == '\\') return "Name can't contain quotes, backslashes or control characters.";
  }
  if (hostnameFor(n).isEmpty()) return "Name needs at least one letter or digit.";
  setField(g_s.name, n);
  return save() ? "" : "Failed to save the name to flash.";
}

}  // namespace Settings
