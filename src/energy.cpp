#include "energy.h"

#include <math.h>
#include <time.h>

#include "config.h"
#include "log.h"
#include "outlets.h"
#include "storage.h"

namespace {

const uint32_t ENERGY_MAGIC = 0x4C4F5545;  // "LOUE"
const char *const ENERGY_PATH = "/energy.bin";
const uint32_t RTC_BLOCK = 32;  // RTC user memory offset, in 4-byte blocks

const uint32_t TICK_MS = 1000;
const uint32_t CF1_PHASE_MS = 2000;            // voltage, then current, on CF1
const uint32_t NO_LOAD_S = 10;                 // no power pulse this long: 0 W (as Tasmota)
const uint32_t MAX_PERIOD_US = 20000000;
const uint32_t RTC_SAVE_MS = 10000;
const uint32_t FLASH_SAVE_MS = 15UL * 60 * 1000;

struct __attribute__((packed)) StoredEnergy {
  uint32_t magic;
  double totalWh;
  double todayWh;
  double yesterdayWh;
  uint32_t dayKey;      // local date "today" belongs to, yyyymmdd; 0 = clock not set yet
  uint32_t totalStart;  // epoch seconds
  uint32_t powerCal;
  uint32_t voltageCal;
  uint32_t currentCal;
  uint32_t checksum;
};
static_assert(sizeof(StoredEnergy) % 4 == 0, "RTC memory is written in 4-byte blocks");

StoredEnergy g_e = {};
Energy::Reading g_r = {};

// ---- pulse capture ----
volatile uint32_t g_cfCount = 0;
volatile uint32_t g_cfLastUs = 0;
volatile uint32_t g_cfPeriodSum = 0;
volatile uint32_t g_cfPeriodN = 0;

volatile uint32_t g_cf1LastUs = 0;
volatile uint32_t g_cf1Sum = 0;
volatile uint32_t g_cf1N = 0;
volatile uint8_t g_cf1Skip = 0;

IRAM_ATTR void onCf() {
  uint32_t now = micros();
  uint32_t dt = now - g_cfLastUs;
  g_cfLastUs = now;
  g_cfCount = g_cfCount + 1;
  if (dt < MAX_PERIOD_US) {
    g_cfPeriodSum = g_cfPeriodSum + dt;
    g_cfPeriodN = g_cfPeriodN + 1;
  }
}

IRAM_ATTR void onCf1() {
  uint32_t now = micros();
  uint32_t dt = now - g_cf1LastUs;
  g_cf1LastUs = now;
  // The first pulses after SEL switches span both modes and the chip settling; drop them.
  if (g_cf1Skip) {
    g_cf1Skip = g_cf1Skip - 1;
    return;
  }
  g_cf1Sum = g_cf1Sum + dt;
  g_cf1N = g_cf1N + 1;
}

// ---- measurement state ----
uint32_t g_lastCount = 0;
uint32_t g_idleSeconds = NO_LOAD_S;
uint32_t g_lastPeriodUs = 0;
bool g_selVoltage = true;
uint32_t g_phaseStart = 0;
uint32_t g_tick = 0;
uint32_t g_rtcSavedAt = 0;
uint32_t g_flashSavedAt = 0;
bool g_dirty = false;  // changed since the last flash save
uint32_t g_voltPeriodUs = 0, g_currPeriodUs = 0;  // last averages, for diagnostics

bool elapsed(uint32_t since, uint32_t ms) { return (uint32_t)(millis() - since) >= ms; }

void setSel(bool voltage) {
  g_selVoltage = voltage;
  noInterrupts();
  g_cf1Sum = 0;
  g_cf1N = 0;
  g_cf1Skip = 2;
  interrupts();
  digitalWrite(PIN_BL_SEL, voltage ? HIGH : LOW);
  g_phaseStart = millis();
}

double whPerPulse() { return (double)BL_POWER_RATIO * g_e.powerCal / 3.6e10; }

uint32_t localDayKey() {
  time_t now = time(nullptr);
  if (now < 1700000000) return 0;  // clock not set yet
  struct tm t;
  localtime_r(&now, &t);
  return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
}

void defaults() {
  g_e = {};
  g_e.powerCal = DEFAULT_POWER_CAL;
  g_e.voltageCal = DEFAULT_VOLTAGE_CAL;
  g_e.currentCal = DEFAULT_CURRENT_CAL;
}

void saveRtc() {
  g_e.magic = ENERGY_MAGIC;
  g_e.checksum = Storage::checksum(g_e);
  uint32_t words[sizeof(g_e) / 4];  // RTC memory is word-addressed; the packed struct may not be aligned
  memcpy(words, &g_e, sizeof(g_e));
  ESP.rtcUserMemoryWrite(RTC_BLOCK, words, sizeof(words));
  g_rtcSavedAt = millis();
}

void saveFlash() {
  if (Storage::save(ENERGY_PATH, g_e, ENERGY_MAGIC)) {
    g_dirty = false;
  } else {
    logf("[energy] failed to save totals");
  }
  g_flashSavedAt = millis();
  saveRtc();
}

void rollDay() {
  uint32_t key = localDayKey();
  if (key == 0 || key == g_e.dayKey) return;
  if (g_e.dayKey != 0) {
    g_e.yesterdayWh = g_e.todayWh;
    g_e.todayWh = 0;
    logf("[energy] new day %lu, yesterday %.3f kWh", (unsigned long)key, g_e.yesterdayWh / 1000);
  }
  g_e.dayKey = key;
  if (g_e.totalStart == 0) g_e.totalStart = time(nullptr);
  saveFlash();
}

void measurePower() {
  noInterrupts();
  uint32_t count = g_cfCount;
  uint32_t sum = g_cfPeriodSum;
  uint32_t n = g_cfPeriodN;
  g_cfPeriodSum = 0;
  g_cfPeriodN = 0;
  interrupts();

  uint32_t pulses = count - g_lastCount;
  g_lastCount = count;
  if (pulses) {
    double wh = pulses * whPerPulse();
    g_e.totalWh += wh;
    g_e.todayWh += wh;
    g_dirty = true;
  }

  double scale = (double)BL_POWER_RATIO * g_e.powerCal / 10.0;  // W * period in us
  if (n) {
    g_idleSeconds = 0;
    g_lastPeriodUs = sum / n;
    g_r.power = scale / g_lastPeriodUs;
  } else if (++g_idleSeconds >= NO_LOAD_S || g_lastPeriodUs == 0) {
    g_r.power = 0;
  } else if (g_idleSeconds * 1000000UL > g_lastPeriodUs) {
    // No pulse yet in a longer time than the last period: the load dropped. Show the most it
    // can be until the next pulse arrives.
    g_r.power = scale / (g_idleSeconds * 1000000.0);
  }
  if (!Outlets::anyOn()) g_r.power = 0;  // both relays open: no load is possible
}

void measureCf1() {
  if (!elapsed(g_phaseStart, CF1_PHASE_MS)) return;
  noInterrupts();
  uint32_t sum = g_cf1Sum;
  uint32_t n = g_cf1N;
  interrupts();
  uint32_t period = n ? sum / n : 0;

  if (g_selVoltage) {
    g_voltPeriodUs = period;
    g_r.voltage = period ? (double)BL_VOLTAGE_RATIO * g_e.voltageCal / (10.0 * period) : 0;
  } else {
    g_currPeriodUs = period;
    g_r.current = period ? (double)BL_CURRENT_RATIO * g_e.currentCal / (1000.0 * period) : 0;
  }
  setSel(!g_selVoltage);
}

void derive() {
  if (g_r.power <= 0) {
    g_r.power = 0;
    g_r.current = 0;
    g_r.apparent = g_r.reactive = g_r.factor = 0;
    return;
  }
  // Very small currents may give no CF1 pulse in a whole phase; fall back to P / V.
  if (g_r.current <= 0 && g_r.voltage > 0) g_r.current = g_r.power / g_r.voltage;
  g_r.apparent = g_r.voltage * g_r.current;
  if (g_r.apparent < g_r.power) g_r.apparent = g_r.power;
  g_r.reactive = sqrtf(g_r.apparent * g_r.apparent - g_r.power * g_r.power);
  g_r.factor = g_r.apparent > 0 ? g_r.power / g_r.apparent : 0;
}

String fmt(double v, int decimals) { return String(v, decimals); }

String isoLocal(uint32_t epoch) {
  if (epoch == 0) return "";
  time_t t = epoch;
  struct tm tm;
  localtime_r(&t, &tm);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
  return buf;
}

}  // namespace

namespace Energy {

void begin() {
  StoredEnergy flash;
  bool haveFlash = Storage::load(ENERGY_PATH, flash, ENERGY_MAGIC);
  StoredEnergy rtc;
  uint32_t words[sizeof(rtc) / 4];
  ESP.rtcUserMemoryRead(RTC_BLOCK, words, sizeof(words));
  memcpy(&rtc, words, sizeof(rtc));
  bool haveRtc = Storage::valid(rtc, ENERGY_MAGIC);

  // RTC memory survives a restart but not a power cut, and is newer than flash when present.
  if (haveRtc && (!haveFlash || rtc.totalWh >= flash.totalWh)) {
    g_e = rtc;
    g_dirty = true;
  } else if (haveFlash) {
    g_e = flash;
  } else {
    defaults();
  }
  logf("[energy] total %.3f kWh, today %.3f kWh (%s)", g_e.totalWh / 1000, g_e.todayWh / 1000,
       haveRtc && g_dirty ? "RTC" : haveFlash ? "flash" : "new");

  pinMode(PIN_BL_CF, INPUT);
  pinMode(PIN_BL_CF1, INPUT);
  pinMode(PIN_BL_SEL, OUTPUT);
  setSel(true);
  attachInterrupt(digitalPinToInterrupt(PIN_BL_CF), onCf, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BL_CF1), onCf1, FALLING);
  g_tick = g_flashSavedAt = g_rtcSavedAt = millis();
}

void loop() {
  measureCf1();
  if (!elapsed(g_tick, TICK_MS)) return;
  g_tick += TICK_MS;
  if (elapsed(g_tick, TICK_MS)) g_tick = millis();  // fell behind (long blocking call)

  measurePower();
  derive();
  rollDay();
  if (g_dirty && elapsed(g_flashSavedAt, FLASH_SAVE_MS)) {
    saveFlash();
  } else if (elapsed(g_rtcSavedAt, RTC_SAVE_MS)) {
    saveRtc();
  }
}

const Reading &reading() { return g_r; }
double todayKWh() { return g_e.todayWh / 1000; }
double yesterdayKWh() { return g_e.yesterdayWh / 1000; }
double totalKWh() { return g_e.totalWh / 1000; }
uint32_t totalStartTime() { return g_e.totalStart; }
Calibration calibration() { return {g_e.powerCal, g_e.voltageCal, g_e.currentCal}; }

String json() {
  String j = "{";
  j += "\"voltage\":" + fmt(g_r.voltage, 1);
  j += ",\"current\":" + fmt(g_r.current, 3);
  j += ",\"power\":" + fmt(g_r.power, 1);
  j += ",\"apparentPower\":" + fmt(g_r.apparent, 1);
  j += ",\"reactivePower\":" + fmt(g_r.reactive, 1);
  j += ",\"factor\":" + fmt(g_r.factor, 2);
  j += ",\"today\":" + fmt(todayKWh(), 3);
  j += ",\"yesterday\":" + fmt(yesterdayKWh(), 3);
  j += ",\"total\":" + fmt(totalKWh(), 3);
  j += ",\"totalStartTime\":\"" + isoLocal(g_e.totalStart) + "\"";
  j += "}";
  return j;
}

String debugJson() {
  String j = "{";
  j += "\"powerCal\":" + String(g_e.powerCal);
  j += ",\"voltageCal\":" + String(g_e.voltageCal);
  j += ",\"currentCal\":" + String(g_e.currentCal);
  j += ",\"cfPulses\":" + String(g_cfCount);
  j += ",\"cfPeriodUs\":" + String(g_lastPeriodUs);
  j += ",\"cf1VoltagePeriodUs\":" + String(g_voltPeriodUs);
  j += ",\"cf1CurrentPeriodUs\":" + String(g_currPeriodUs);
  j += ",\"dayKey\":" + String(g_e.dayKey);
  j += "}";
  return j;
}

String calibrateTo(float voltage, float power, float current) {
  if (voltage > 0 && g_r.voltage <= 0) return "No voltage reading to calibrate against.";
  if (power > 0 && g_r.power <= 0) return "No power reading: switch an outlet on with a load.";
  if (current > 0 && g_r.current <= 0) return "No current reading: switch an outlet on with a load.";
  if (voltage > 0) g_e.voltageCal = lround(g_e.voltageCal * voltage / g_r.voltage);
  if (power > 0) g_e.powerCal = lround(g_e.powerCal * power / g_r.power);
  if (current > 0) g_e.currentCal = lround(g_e.currentCal * current / g_r.current);
  logf("[energy] calibration power=%lu voltage=%lu current=%lu", (unsigned long)g_e.powerCal,
       (unsigned long)g_e.voltageCal, (unsigned long)g_e.currentCal);
  saveFlash();
  return "";
}

void setCalibration(const Calibration &cal) {
  if (cal.power) g_e.powerCal = cal.power;
  if (cal.voltage) g_e.voltageCal = cal.voltage;
  if (cal.current) g_e.currentCal = cal.current;
  saveFlash();
}

void setTotals(double total, double today, double yesterday, uint32_t startTime) {
  if (total >= 0) g_e.totalWh = total * 1000;
  if (today >= 0) g_e.todayWh = today * 1000;
  if (yesterday >= 0) g_e.yesterdayWh = yesterday * 1000;
  if (startTime) g_e.totalStart = startTime;
  saveFlash();
}

void reset() {
  g_e.totalWh = g_e.todayWh = g_e.yesterdayWh = 0;
  time_t now = time(nullptr);
  g_e.totalStart = now > 1700000000 ? now : 0;
  saveFlash();
}

void save() { saveFlash(); }

}  // namespace Energy
