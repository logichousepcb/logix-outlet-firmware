#pragma once

// MQTT link: mirrors the HTTP API onto <topic>/... (see API.md).

#include <Arduino.h>

namespace Mqtt {

void begin();
void loop();
// Base topic: the saved one, or the default <model>/<id>.
String topic();
String defaultTopic();
bool enabled();
bool connected();
// Settings changed: drop the connection and reconnect with the new ones.
void restart();
// "mqtt":{enabled,connected,topic} member for /api/info.
String infoJson();
// Ask for T/state to be republished soon (e.g. after a rename).
void publishStateSoon();

}  // namespace Mqtt
