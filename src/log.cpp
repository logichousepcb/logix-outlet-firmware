#include "log.h"

#include <stdarg.h>

namespace {
constexpr size_t LOG_SIZE = 2048;
char g_log[LOG_SIZE];
size_t g_head = 0;  // next write position
bool g_wrapped = false;

void append(const char *s) {
  for (; *s; s++) {
    g_log[g_head++] = *s;
    if (g_head == LOG_SIZE) {
      g_head = 0;
      g_wrapped = true;
    }
  }
}
}  // namespace

void logf(const char *fmt, ...) {
  char line[160];
  int n = snprintf(line, sizeof(line), "%lu.%03lu ", millis() / 1000, millis() % 1000);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line + n, sizeof(line) - n - 1, fmt, ap);
  va_end(ap);
  Serial.println(line);
  append(line);
  append("\n");
}

String logText() {
  String out;
  out.reserve(LOG_SIZE);
  if (g_wrapped) {
    // Skip the partial line at the oldest end.
    size_t i = g_head;
    while (i < LOG_SIZE && g_log[i] != '\n') i++;
    for (i++; i < LOG_SIZE; i++) out += g_log[i];
  }
  for (size_t i = 0; i < g_head; i++) out += g_log[i];
  return out;
}
