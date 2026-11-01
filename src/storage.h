#pragma once

// Fixed-layout structs in LittleFS. Each struct must be packed, start with `uint32_t magic`
// and end with `uint32_t checksum`.

#include <Arduino.h>
#include <LittleFS.h>

namespace Storage {

// Mounts LittleFS, formatting it on first boot. False if flash is unusable.
bool begin();
bool ready();

// FNV-1a over everything before the trailing checksum field.
template <typename T>
uint32_t checksum(const T& s) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < offsetof(T, checksum); i++) {
    hash ^= p[i];
    hash *= 16777619u;
  }
  return hash;
}

template <typename T>
bool valid(const T& s, uint32_t magic) {
  return s.magic == magic && s.checksum == checksum(s);
}

template <typename T>
bool load(const char* path, T& out, uint32_t magic) {
  if (!ready()) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  bool ok = f.size() == sizeof(T) && f.read(reinterpret_cast<uint8_t*>(&out), sizeof(T)) == sizeof(T);
  f.close();
  return ok && valid(out, magic);
}

template <typename T>
bool save(const char* path, T& s, uint32_t magic) {
  if (!ready()) return false;
  s.magic = magic;
  s.checksum = checksum(s);
  String tmpPath = String(path) + ".tmp";
  File f = LittleFS.open(tmpPath, "w");
  if (!f) return false;
  bool ok = f.write(reinterpret_cast<const uint8_t*>(&s), sizeof(s)) == sizeof(s);
  f.close();
  // Write-then-rename so a power cut never leaves a half-written file.
  if (!ok || !LittleFS.rename(tmpPath, path)) {
    LittleFS.remove(tmpPath);
    return false;
  }
  return true;
}

}  // namespace Storage
