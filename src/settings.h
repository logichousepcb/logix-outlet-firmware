#pragma once

// Saved settings (LittleFS) and device identity. On the very first boot after Tasmota the
// WiFi, MQTT and energy settings are imported from Tasmota's settings sector, so the outlet
// comes back on the same network without visiting the setup AP.

#include <Arduino.h>

namespace Settings {

struct __attribute__((packed)) Data {
  uint32_t magic;
  char name[33];  // friendly device name; the outlets are "<name> Top" and "<name> Bottom"
  char ssid[33];
  char pass[65];
  char admin[65];  // web/API admin password, "" = none
  char mqttHost[65];  // "" = MQTT off
  uint16_t mqttPort;
  char mqttUser[65];
  char mqttPass[65];
  char mqttTopic[65];  // "" = default <model>/<id>
  uint8_t tasmotaImported;
  uint8_t reserved[7];
  uint32_t checksum;
};

// Call after Energy::begin(): the Tasmota import also brings over the energy totals.
void begin();
Data &get();
bool save();

// Lowercase MAC without separators, e.g. "c4dd573121fb"; the stable unique id.
String deviceId();
// Friendly name, e.g. "Logix Outlet 21FB" (default) or "Family Room Outlet".
String name();
// "<name> Top" / "<name> Bottom"
String outletName(uint8_t index);
// Hostname derived from the name, e.g. "family-room-outlet": DHCP, mDNS, MQTT client id.
String hostname();
String hostnameFor(const String &name);
// Validate and save a new name. Returns an error message, or "" on success.
String setName(const String &name);
String defaultName();

}  // namespace Settings
