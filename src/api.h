#pragma once

// Web server on port 80: the web page, the HTTP API (see API.md) and firmware upload.

#include <Arduino.h>

namespace Api {

void begin();
void loop();

// GET /api/info: identity and capabilities; also the MQTT discovery payload.
String infoJson();
// GET /api/controller; live adds the fields that change every request (uptime, ms ago).
String controllerJson(bool live);
// Shared by HTTP and MQTT. which: 1, 2, top, bottom or all. state: on, off or toggle.
// source (e.g. "http 192.168.22.10", "mqtt") goes to the log.
bool setOutlet(const String &which, const String &state, String &message, const String &source);

void scheduleRestart(uint32_t delayMs);

}  // namespace Api
