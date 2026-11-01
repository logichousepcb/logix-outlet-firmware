#include "storage.h"

#include "log.h"

namespace {
bool g_ready = false;
}

namespace Storage {

bool begin() {
  // LittleFS formats the partition when it can't mount it (first boot after Tasmota).
  g_ready = LittleFS.begin();
  if (!g_ready) logf("[fs] LittleFS mount failed; nothing will be saved");
  return g_ready;
}

bool ready() { return g_ready; }

}  // namespace Storage
