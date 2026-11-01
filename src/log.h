#pragma once
#include <Arduino.h>

// Prints to Serial and keeps the last few KB for GET /api/log.
void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
String logText();
