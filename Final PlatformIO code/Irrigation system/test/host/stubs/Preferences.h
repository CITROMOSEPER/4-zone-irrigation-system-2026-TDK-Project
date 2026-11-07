#pragma once
#include <Arduino.h>
#include <map>
#include <vector>
// In-memory NVS
namespace host { std::map<std::string, std::vector<uint8_t>>& nvs(); }
class Preferences {
public:
  bool begin(const char* ns, bool = false) { _ns = ns; return true; }
  void end() {}
  size_t getBytesLength(const char* k) { auto it = host::nvs().find(key(k)); return it == host::nvs().end() ? 0 : it->second.size(); }
  size_t getBytes(const char* k, void* buf, size_t n) {
    auto it = host::nvs().find(key(k)); if (it == host::nvs().end()) return 0;
    size_t m = std::min(n, it->second.size()); memcpy(buf, it->second.data(), m); return m; }
  size_t putBytes(const char* k, const void* buf, size_t n) { auto& v = host::nvs()[key(k)]; v.assign((const uint8_t*)buf, (const uint8_t*)buf + n); return n; }
  String getString(const char* k, const char* def = "") { auto it = host::nvs().find(key(k)); return it == host::nvs().end() ? String(def) : String(std::string(it->second.begin(), it->second.end())); }
  size_t putString(const char* k, const String& s) { return putBytes(k, s.c_str(), s.length()); }
  uint8_t getUChar(const char* k, uint8_t def = 0) { uint8_t v = def; getBytes(k, &v, 1); return v; }
  size_t putUChar(const char* k, uint8_t v) { return putBytes(k, &v, 1); }
  bool remove(const char* k) { return host::nvs().erase(key(k)) > 0; }
  bool clear() {
    std::string pre = _ns + "/";
    for (auto it = host::nvs().begin(); it != host::nvs().end();) it = it->first.rfind(pre, 0) == 0 ? host::nvs().erase(it) : std::next(it);
    return true;
  }
private:
  std::string key(const char* k) { return _ns + "/" + k; }
  std::string _ns;
};
