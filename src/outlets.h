#pragma once

// The two outlets (relay + LED each) and their two buttons.
//
// Buttons are scanned from a timer, not from loop(), so they keep toggling the outlets while
// the network code is busy (WiFi connecting, MQTT broker unreachable, HTTP request). The
// outlet state is written to flash on every change and restored first thing at boot.

#include <Arduino.h>

namespace Outlets {

constexpr uint8_t COUNT = 2;  // index 0 = top (outlet 1), 1 = bottom (outlet 2)

void begin();  // restores the saved state and starts the button scan; call first in setup()
void loop();   // saves state changes to flash

bool isOn(uint8_t index);
bool anyOn();
void set(uint8_t index, bool on);
void toggle(uint8_t index);
// Bumped on every outlet change, physical or remote; compare to detect changes.
uint32_t changeCount();

// "top" / "bottom"
const char *key(uint8_t index);
// Outlet number 1-2, "top", "bottom" or "all" (returns COUNT for all); -1 if not valid.
int parse(const String &value);

// ---- buttons (index as for outlets: the top button toggles the top outlet) ----
bool buttonPressed(uint8_t index);
uint32_t buttonPressCount(uint8_t index);
// Completed actions since boot (physical and simulated) and ms since the last one (-1 = none).
uint32_t buttonActionCount(uint8_t index);
int32_t buttonLastActionMsAgo(uint8_t index);
// Simulate a press exactly like the physical button: toggles the outlet.
void pressButton(uint8_t index);
// True once per action not yet taken; used to publish one MQTT event per press.
bool takeButtonEvent(uint8_t index);
// Latest raw A0 reading (0-1024).
int adcRaw();

}  // namespace Outlets
