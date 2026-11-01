#pragma once

// WiFi station, setup access point and the services that need a network (mDNS, NTP).
//
// Nothing here blocks: the station connects in the background. When there is no saved
// network, or no connection for WIFI_AP_FALLBACK_MS, the setup AP "Logix-Outlet-XXXX" comes
// up at 192.168.4.1 with a captive portal, and the saved network is retried every
// WIFI_AP_RETRY_MS while nobody is connected to the AP. Once the station is back the AP
// goes away again.

#include <Arduino.h>

namespace Net {

void begin();
void loop();

bool connected();
bool apActive();
String apSsid();
String apIp();
uint8_t apClients();
// True if a request's Host header names this device (used for the captive portal redirect).
bool isOwnHost(const String &host);
// Apply a new saved network now.
void reconnect();

}  // namespace Net
