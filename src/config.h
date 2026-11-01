#pragma once
#include <Arduino.h>

#define FW_VERSION "1.0.0"
// Logix device contract (LogixDIY Home Assistant integration)
#define LOGIX_MANUFACTURER "Logix"
#define LOGIX_MODEL "logixoutlet"
#define LOGIX_MODEL_NAME "Logix Outlet"
#define LOGIX_API_VERSION 1

// ---- Gosund WO1 pin map (hard-coded, see DEVICE.md §3) ----
constexpr uint8_t PIN_LED_TOP = 0;        // outlet 1 LED, active low (boot strap: high at boot)
constexpr uint8_t PIN_LED_LINK = 2;       // WiFi LED, active low, not visible on the faceplate
constexpr uint8_t PIN_BL_CF1 = 4;         // BL0937 CF1: voltage or current pulses, chosen by SEL
constexpr uint8_t PIN_BL_CF = 5;          // BL0937 CF: active power pulses
constexpr uint8_t PIN_BL_SEL = 12;        // BL0937 SEL: low = voltage on CF1, high = current
constexpr uint8_t PIN_LED_BOTTOM = 13;    // outlet 2 LED, active low
constexpr uint8_t PIN_RELAY_BOTTOM = 14;  // outlet 2 relay, active high
constexpr uint8_t PIN_RELAY_TOP = 15;     // outlet 1 relay, active high (boot strap: low at boot)

// ---- Buttons: resistor ladder on A0 (measured 2026-11-01: idle 1024, top ~754, bottom ~314) ----
constexpr int ADC_IDLE_MIN = 930;         // at or above: no button pressed
constexpr int ADC_TOP_MIN = 560;          // top button band
constexpr int ADC_TOP_MAX = 900;
constexpr int ADC_BOTTOM_MIN = 150;       // bottom button band
constexpr int ADC_BOTTOM_MAX = 480;
constexpr uint32_t BUTTON_SCAN_MS = 25;
constexpr uint8_t BUTTON_STABLE_SAMPLES = 3;  // 75 ms debounce; also skips the ladder's edge values

// ---- BL0937 scaling (Tasmota's HJL-01/BL0937 constants, so its calibration values carry over) ----
constexpr uint32_t BL_POWER_RATIO = 1362;
constexpr uint32_t BL_VOLTAGE_RATIO = 822;
constexpr uint32_t BL_CURRENT_RATIO = 3300;
constexpr uint32_t DEFAULT_POWER_CAL = 12530;
constexpr uint32_t DEFAULT_VOLTAGE_CAL = 1950;
constexpr uint32_t DEFAULT_CURRENT_CAL = 3500;

// ---- Time: US Eastern (EST/EDT) ----
#define TZ_POSIX "EST5EDT,M3.2.0,M11.1.0"
#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.nist.gov"

// ---- Network ----
constexpr uint32_t WIFI_AP_FALLBACK_MS = 60000;   // no WiFi this long: bring up the setup AP
constexpr uint32_t WIFI_AP_RETRY_MS = 60000;      // while in AP mode, retry the saved network
constexpr uint32_t WIFI_AP_ATTEMPT_MS = 20000;    // ... for this long each time
constexpr uint32_t MQTT_RETRY_MS = 10000;
