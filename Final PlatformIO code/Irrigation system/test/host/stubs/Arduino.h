#pragma once
// Host-side stand-in for the Arduino core: enough to compile the firmware with g++ and run the
// logic tests on a PC. Pins do nothing; millis() and the clock are under the test's control.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string>
#include <algorithm>
#include <functional>
#include <initializer_list>

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define PROGMEM
#define PGM_P const char*
#define RTC_NOINIT_ATTR
#define ADC_11db 3
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_word(p) (*(const uint16_t*)(p))
#define pgm_read_dword(p) (*(const uint32_t*)(p))
#define pgm_read_float(p) (*(const float*)(p))
#define pgm_read_ptr(p) (*(void* const*)(p))

using std::min;
using std::max;
template <typename T, typename L, typename H> T constrain(T v, L lo, H hi) { return v < lo ? (T)lo : (v > hi ? (T)hi : v); }

// ---- time and pins (the test drives these) ----
namespace host {
extern uint32_t ms;
extern int pinLevel[40];
extern int analogValue[40];
extern bool pinOutput[40];
extern unsigned long pulseUs;
}  // namespace host
inline uint32_t millis() { return host::ms; }
inline uint32_t micros() { return host::ms * 1000; }
inline void delay(uint32_t d) { host::ms += d; }
inline void delayMicroseconds(uint32_t) {}
inline void pinMode(uint8_t p, uint8_t m) { host::pinOutput[p] = m == OUTPUT; }
inline void digitalWrite(uint8_t p, uint8_t v) { host::pinLevel[p] = v; }
inline int digitalRead(uint8_t p) { return host::pinLevel[p]; }
inline int analogRead(uint8_t p) { return host::analogValue[p]; }
inline void analogSetPinAttenuation(uint8_t, int) {}
inline unsigned long pulseIn(uint8_t, uint8_t, uint32_t) { return host::pulseUs; }
inline void ledcSetup(uint8_t, uint32_t, uint8_t) {}
inline void ledcAttachPin(uint8_t, uint8_t) {}
inline void ledcWrite(uint8_t, uint32_t) {}
inline double ledcWriteTone(uint8_t, double f) { return f; }

// ---- String ----
class String {
public:
  String() {}
  String(const char* s) : _s(s ? s : "") {}
  String(const std::string& s) : _s(s) {}
  String(char c) : _s(1, c) {}
  String(int v) : _s(std::to_string(v)) {}
  String(unsigned v) : _s(std::to_string(v)) {}
  String(long v) : _s(std::to_string(v)) {}
  String(unsigned long v) : _s(std::to_string(v)) {}
  String& operator=(const char* s) { _s = s ? s : ""; return *this; }
  const char* c_str() const { return _s.c_str(); }
  size_t length() const { return _s.size(); }
  bool concat(const char* s) { _s += s; return true; }
  bool concat(char c) { _s += c; return true; }
  String& operator+=(const String& o) { _s += o._s; return *this; }
  String& operator+=(const char* o) { _s += o; return *this; }
  String& operator+=(char c) { _s += c; return *this; }
  String& operator+=(int v) { _s += std::to_string(v); return *this; }
  friend String operator+(const String& a, const String& b) { return String(a._s + b._s); }
  friend String operator+(const String& a, const char* b) { return String(a._s + b); }
  bool operator==(const String& o) const { return _s == o._s; }
  bool operator==(const char* o) const { return _s == o; }
  bool operator!=(const String& o) const { return _s != o._s; }
  bool operator!=(const char* o) const { return _s != o; }
  char operator[](size_t i) const { return _s[i]; }
  long toInt() const { return strtol(_s.c_str(), nullptr, 10); }
  float toFloat() const { return strtof(_s.c_str(), nullptr); }
  void trim() {
    size_t a = _s.find_first_not_of(" \t\r\n"), b = _s.find_last_not_of(" \t\r\n");
    _s = a == std::string::npos ? "" : _s.substr(a, b - a + 1);
  }
  void replace(char a, char b) { std::replace(_s.begin(), _s.end(), a, b); }
  int indexOf(char c) const { size_t p = _s.find(c); return p == std::string::npos ? -1 : (int)p; }
  String substring(size_t a) const { return String(_s.substr(a)); }
  void reserve(size_t n) { _s.reserve(n); }
private:
  std::string _s;
};

// ---- Print / Stream ----
class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* b, size_t n) { for (size_t i = 0; i < n; i++) write(b[i]); return n; }
  size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t print(const String& s) { return print(s.c_str()); }
  size_t println(const char* s = "") { size_t n = print(s); return n + print("\n"); }
  size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
};
class Stream : public Print {
public:
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  virtual int peek() { return -1; }
  size_t readBytes(char* b, size_t n) { size_t i = 0; int c; while (i < n && (c = read()) >= 0) b[i++] = (char)c; return i; }
  size_t readBytesUntil(char t, char* b, size_t n) { size_t i = 0; int c; while (i < n && (c = read()) >= 0 && c != t) b[i++] = (char)c; return i; }
  String readStringUntil(char t) { std::string s; int c; while ((c = read()) >= 0 && c != t) s += (char)c; return String(s); }
  void setTimeout(uint32_t) {}
};

class HardwareSerial : public Stream {
public:
  void begin(unsigned long) {}
  size_t write(uint8_t c) override { if (echo) fputc(c, stdout); return 1; }
  bool echo = false;
};
extern HardwareSerial Serial;

struct EspClass {
  uint32_t getFreeHeap() { return 200000; }
  uint32_t getMinFreeHeap() { return 150000; }
  uint32_t getMaxAllocHeap() { return 110000; }
  void restart() { restarted = true; }
  bool restarted = false;
};
extern EspClass ESP;

class IPAddress {
public:
  IPAddress(uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint8_t d = 0) : _a{a, b, c, d} {}
  String toString() const { char b[16]; snprintf(b, 16, "%u.%u.%u.%u", _a[0], _a[1], _a[2], _a[3]); return String(b); }
  operator uint32_t() const { return (uint32_t)_a[0] | (uint32_t)_a[1] << 8 | (uint32_t)_a[2] << 16 | (uint32_t)_a[3] << 24; }
private:
  uint8_t _a[4];
};

inline void configTzTime(const char*, const char*, const char* = nullptr, const char* = nullptr) {}
class Printable { public: virtual ~Printable() {} virtual size_t printTo(Print& p) const = 0; };

// SNTP never answers on the host: the simulated RTC stays in charge
#include <time.h>
inline time_t host_time(time_t*) { return 0; }
#define time(x) host_time(x)
