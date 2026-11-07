#pragma once
#include <FS.h>
class LittleFSFS {
public:
  bool begin(bool = false) { return true; }
  File open(const char* p, const char* mode = FILE_READ) {
    auto& f = host::files();
    std::string path(p);
    if (mode[0] == 'r' && !f.count(path)) {
      std::string pre = path + "/";                       // a directory: some file lives under it
      for (auto& kv : f) if (kv.first.rfind(pre, 0) == 0) return File::dir(path);
      if (dirs().count(path)) return File::dir(path);
      return File();
    }
    if (mode[0] == 'w') f[path] = "";
    if (mode[0] == 'a' && !f.count(path)) f[path] = "";
    bool append = mode[0] == 'a';
    return File(path, mode[0] != 'r' || (mode[1] == '+'), append);
  }
  bool remove(const char* p) { return host::files().erase(p) > 0; }
  bool rename(const char* a, const char* b) { auto& f = host::files(); if (!f.count(a)) return false; f[b] = f[a]; f.erase(a); return true; }
  bool mkdir(const char* p) { dirs()[p] = 1; return true; }
  bool exists(const char* p) { return host::files().count(p) || dirs().count(p); }
  size_t totalBytes() { return 896 * 1024; }
  size_t usedBytes() { size_t n = 0; for (auto& kv : host::files()) n += kv.second.size(); return n; }
private:
  static std::map<std::string, int>& dirs() { static std::map<std::string, int> d; return d; }
};
extern LittleFSFS LittleFS;
