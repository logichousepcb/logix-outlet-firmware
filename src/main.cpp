// Logix Outlet: firmware for the Gosund WO1 in-wall duplex receptacle.
//
// Boot order matters: the outlets are back in their saved state and the buttons are live
// before WiFi, MQTT or the web server are touched, and none of those ever block them.

#include <Arduino.h>

#include "api.h"
#include "config.h"
#include "energy.h"
#include "log.h"
#include "mqtt.h"
#include "net.h"
#include "outlets.h"
#include "settings.h"
#include "storage.h"

void setup() {
  Serial.begin(115200);
  Storage::begin();  // formats LittleFS on the first boot after Tasmota
  Outlets::begin();
  logf("\n" LOGIX_MODEL_NAME " " FW_VERSION " (%s)", ESP.getResetReason().c_str());
  Energy::begin();
  Settings::begin();  // after Energy: the Tasmota import brings energy totals along
  logf("[sys] %s (%s)", Settings::name().c_str(), Settings::hostname().c_str());
  Net::begin();
  Api::begin();
  Mqtt::begin();
}

void loop() {
  Outlets::loop();
  Energy::loop();
  Net::loop();
  Api::loop();
  Mqtt::loop();
}
