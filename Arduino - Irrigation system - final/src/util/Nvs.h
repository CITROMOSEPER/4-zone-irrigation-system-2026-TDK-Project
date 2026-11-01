#pragma once
// Fixed-size structs in NVS: load only when the stored size matches, write only when changed.
#include <Preferences.h>
#include <string.h>

namespace Nvs {

template <typename T>
bool load(const char* ns, const char* key, T& out) {
  Preferences p;
  if (!p.begin(ns, true)) return false;
  bool ok = p.getBytesLength(key) == sizeof(T) && p.getBytes(key, &out, sizeof(T)) == sizeof(T);
  p.end();
  return ok;
}

// Writes and reads back. Skips the write when 'shadow' already holds the same bytes.
template <typename T>
bool save(const char* ns, const char* key, const T& v, T* shadow = nullptr) {
  if (shadow && memcmp(shadow, &v, sizeof(T)) == 0) return true;
  Preferences p;
  if (!p.begin(ns, false)) return false;
  p.putBytes(key, &v, sizeof(T));
  T check;
  bool ok = p.getBytes(key, &check, sizeof(T)) == sizeof(T) && memcmp(&check, &v, sizeof(T)) == 0;
  p.end();
  if (ok && shadow) memcpy(shadow, &v, sizeof(T));
  return ok;
}

inline void erase(const char* ns, const char* key) {
  Preferences p;
  if (!p.begin(ns, false)) return;
  p.remove(key);
  p.end();
}

}  // namespace Nvs
