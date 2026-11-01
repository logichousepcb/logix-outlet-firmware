#include "outlets.h"

#include <Ticker.h>

#include "config.h"
#include "log.h"
#include "storage.h"

namespace {

const uint8_t RELAY_PINS[Outlets::COUNT] = {PIN_RELAY_TOP, PIN_RELAY_BOTTOM};
const uint8_t LED_PINS[Outlets::COUNT] = {PIN_LED_TOP, PIN_LED_BOTTOM};
const char *const KEYS[Outlets::COUNT] = {"top", "bottom"};

const uint32_t STATE_MAGIC = 0x4C4F5553;  // "LOUS"
const char *const STATE_PATH = "/state.bin";

struct __attribute__((packed)) StoredState {
  uint32_t magic;
  uint8_t mask;  // bit 0 = top, bit 1 = bottom
  uint8_t reserved[3];
  uint32_t checksum;
};

// Timer callbacks only run while loop() yields, so these never change mid-update; volatile
// because they are shared between the two contexts.
volatile uint8_t g_mask = 0;
volatile uint32_t g_changes = 0;
uint32_t g_handledChanges = 0;
uint8_t g_savedMask = 0;

struct Button {
  volatile bool pressed;
  volatile uint32_t pressCount;
  volatile uint32_t actionCount;
  volatile uint32_t lastActionAt;
  volatile uint32_t eventsTaken;
};
Button g_buttons[Outlets::COUNT] = {};

enum : int8_t { BTN_IDLE = -1, BTN_UNKNOWN = -2 };

Ticker g_scan;
volatile int g_adc = 1024;
int8_t g_candidate = BTN_IDLE;
uint8_t g_stable = 0;
bool g_latched = false;  // a press was handled; ignore everything until all buttons are released

void apply(uint8_t i, bool on) {
  digitalWrite(RELAY_PINS[i], on ? HIGH : LOW);
  digitalWrite(LED_PINS[i], on ? LOW : HIGH);
  uint8_t bit = 1 << i;
  uint8_t mask = on ? (g_mask | bit) : (g_mask & ~bit);
  if (mask != g_mask) {
    g_mask = mask;
    g_changes = g_changes + 1;
  }
}

void recordAction(uint8_t i) {
  Button &b = g_buttons[i];
  b.actionCount = b.actionCount + 1;
  b.lastActionAt = millis();
}

int8_t classify(int v) {
  if (v >= ADC_IDLE_MIN) return BTN_IDLE;
  if (v >= ADC_TOP_MIN && v <= ADC_TOP_MAX) return 0;
  if (v >= ADC_BOTTOM_MIN && v <= ADC_BOTTOM_MAX) return 1;
  return BTN_UNKNOWN;
}

// Every BUTTON_SCAN_MS. A reading must hold the same band for BUTTON_STABLE_SAMPLES scans
// before it counts, which filters the in-between values the ladder passes through on press
// and release (e.g. ~830 while the bottom button lets go).
void scanTick() {
  int v = analogRead(A0);
  g_adc = v;
  int8_t c = classify(v);
  if (c == g_candidate) {
    if (g_stable < 255) g_stable++;
  } else {
    g_candidate = c;
    g_stable = 1;
  }
  if (g_stable != BUTTON_STABLE_SAMPLES) return;  // act once, when the band first becomes stable

  if (c == BTN_IDLE) {
    g_latched = false;
    for (Button &b : g_buttons) b.pressed = false;
  } else if (c >= 0 && !g_latched) {
    g_latched = true;
    Button &b = g_buttons[c];
    b.pressed = true;
    b.pressCount = b.pressCount + 1;
    recordAction(c);
    apply(c, !(g_mask & (1 << c)));
  }
}

}  // namespace

namespace Outlets {

void begin() {
  StoredState s;
  uint8_t mask = Storage::load(STATE_PATH, s, STATE_MAGIC) ? s.mask : 0;
  for (uint8_t i = 0; i < COUNT; i++) {
    bool on = mask & (1 << i);
    // Level first, then output, so the relay never glitches on.
    digitalWrite(RELAY_PINS[i], on ? HIGH : LOW);
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], on ? LOW : HIGH);
    pinMode(LED_PINS[i], OUTPUT);
  }
  g_mask = mask;
  g_savedMask = mask;
  g_handledChanges = g_changes;
  logf("[outlets] restored top=%s bottom=%s", (mask & 1) ? "on" : "off", (mask & 2) ? "on" : "off");
  g_scan.attach_ms(BUTTON_SCAN_MS, scanTick);
}

void loop() {
  uint32_t changes = g_changes;
  if (changes == g_handledChanges) return;
  g_handledChanges = changes;
  uint8_t mask = g_mask;
  logf("[outlets] top=%s bottom=%s", (mask & 1) ? "on" : "off", (mask & 2) ? "on" : "off");
  if (mask == g_savedMask) return;
  StoredState s = {};
  s.mask = mask;
  if (Storage::save(STATE_PATH, s, STATE_MAGIC)) {
    g_savedMask = mask;
  } else {
    logf("[outlets] failed to save state");
  }
}

bool isOn(uint8_t index) { return index < COUNT && (g_mask & (1 << index)); }
bool anyOn() { return g_mask != 0; }

void set(uint8_t index, bool on) {
  if (index < COUNT) apply(index, on);
}

void toggle(uint8_t index) {
  if (index < COUNT) apply(index, !isOn(index));
}

uint32_t changeCount() { return g_changes; }

const char *key(uint8_t index) { return index < COUNT ? KEYS[index] : "all"; }

int parse(const String &value) {
  String v = value;
  v.trim();
  v.toLowerCase();
  if (v == "1" || v == "top") return 0;
  if (v == "2" || v == "bottom") return 1;
  if (v == "all" || v == "0") return COUNT;
  return -1;
}

bool buttonPressed(uint8_t index) { return index < COUNT && g_buttons[index].pressed; }
uint32_t buttonPressCount(uint8_t index) { return index < COUNT ? g_buttons[index].pressCount : 0; }
uint32_t buttonActionCount(uint8_t index) { return index < COUNT ? g_buttons[index].actionCount : 0; }

int32_t buttonLastActionMsAgo(uint8_t index) {
  if (index >= COUNT || g_buttons[index].actionCount == 0) return -1;
  return (int32_t)(millis() - g_buttons[index].lastActionAt);
}

void pressButton(uint8_t index) {
  if (index >= COUNT) return;
  recordAction(index);
  toggle(index);
}

bool takeButtonEvent(uint8_t index) {
  if (index >= COUNT) return false;
  Button &b = g_buttons[index];
  if (b.eventsTaken == b.actionCount) return false;
  b.eventsTaken = b.eventsTaken + 1;
  return true;
}

int adcRaw() { return g_adc; }

}  // namespace Outlets
