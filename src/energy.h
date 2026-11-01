#pragma once

// BL0937 energy meter (one meter for both outlets) and Tasmota-style energy history:
// Today, Yesterday and Total since TotalStartTime. Totals are kept in RTC memory every 10 s
// (survives restarts and OTA) and in flash every 15 min and at midnight (survives power cuts).

#include <Arduino.h>

namespace Energy {

struct Reading {
  float voltage;   // V
  float current;   // A
  float power;     // W (active)
  float apparent;  // VA
  float reactive;  // var
  float factor;    // 0-1
};

struct Calibration {
  uint32_t power, voltage, current;  // Tasmota PowerCal / VoltageCal / CurrentCal
};

void begin();
void loop();

const Reading &reading();
double todayKWh();
double yesterdayKWh();
double totalKWh();
uint32_t totalStartTime();  // epoch seconds, 0 = unknown
Calibration calibration();

// Same members as Tasmota's ENERGY block, as a JSON object.
String json();
// Raw meter values for diagnostics.
String debugJson();

// Scale calibration so the current reading matches a known value (from a meter or a known
// load). Pass <= 0 to leave a quantity alone. Returns an error message, or "" on success.
String calibrateTo(float voltage, float power, float current);
void setCalibration(const Calibration &cal);
// Set the counters (kWh). Negative leaves a value alone. startTime 0 leaves it alone.
void setTotals(double total, double today, double yesterday, uint32_t startTime);
// Zero everything and start counting from now.
void reset();
// Write the totals to flash now (before a restart or update).
void save();

}  // namespace Energy
