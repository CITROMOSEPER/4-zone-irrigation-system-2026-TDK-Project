#pragma once
// Small helpers shared by every layer.
#include <Arduino.h>
#include <stdarg.h>

// True once spanMs has passed since 'since' (safe across the millis() rollover)
inline bool elapsed(uint32_t since, uint32_t spanMs) {
  return (uint32_t)(millis() - since) >= spanMs;
}

template <typename T>
inline T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// printf-style append to a String
inline void appendf(String& s, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
inline void appendf(String& s, const char* fmt, ...) {
  char buf[384];                                  // a health line with a 96-byte zone name fits
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  s += buf;
}

// Copy a C string into a fixed buffer, always terminated
inline void copyStr(char* dst, size_t n, const char* src) {
  if (!n) return;
  strncpy(dst, src ? src : "", n - 1);
  dst[n - 1] = 0;
}

// ---- UTF-8 ----
// Zone names may hold any text, emoji included: the limit counts code points, the buffer bytes.

// Copy at most maxCp code points and maxBytes-1 bytes of src, never cutting a character in half.
// Invalid sequences are dropped. Returns the number of code points copied.
inline size_t utf8Copy(char* dst, size_t maxBytes, const char* src, size_t maxCp) {
  size_t o = 0, cp = 0;
  const uint8_t* p = (const uint8_t*)src;
  while (*p && cp < maxCp) {
    uint8_t c = *p;
    size_t len = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF) ? 2 : ((c & 0xF0) == 0xE0) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
    bool ok = len > 0;
    for (size_t j = 1; ok && j < len; j++) ok = (p[j] & 0xC0) == 0x80;
    if (!ok) { p++; continue; }                    // skip a bad byte
    if (c < 0x20 && c != 0) { p++; continue; }     // no control characters in names
    if (o + len >= maxBytes) break;
    memcpy(dst + o, p, len);
    o += len;
    p += len;
    cp++;
  }
  dst[o] = 0;
  return cp;
}

// Trim spaces at both ends in place
// Hostname rule shared by the UI and the firmware: 1-24 of A-Z a-z 0-9 -, no leading or trailing '-'
inline bool validHostname(const char* s) {
  size_t n = strlen(s);
  if (n < 1 || n > 24 || s[0] == '-' || s[n - 1] == '-') return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
  }
  return true;
}

// Split a CSV row (no quoting: our fields never contain commas) into at most maxF fields.
// Modifies 'row'. Returns the number of fields.
inline uint8_t splitCsv(char* row, char** f, uint8_t maxF) {
  uint8_t n = 0;
  char* p = row;
  while (n < maxF) {
    f[n++] = p;
    char* c = strchr(p, ',');
    if (!c) break;
    *c = 0;
    p = c + 1;
  }
  return n;
}

// A float rounded to 'dec' decimals, for JSON (so 12.399999 prints as 12.4)
inline float roundTo(float v, int dec) {
  float m = dec == 0 ? 1.0f : dec == 1 ? 10.0f : dec == 2 ? 100.0f : 1000.0f;
  return roundf(v * m) / m;
}
