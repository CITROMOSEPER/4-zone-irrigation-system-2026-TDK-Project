#pragma once
#include <Arduino.h>
#include <map>
#include <memory>
#include <vector>
#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"
namespace host { std::map<std::string, std::string>& files(); }
// In-memory file: reads copy, writes go straight to the map. A directory lists the files under it.
class File : public Stream {
public:
  File() {}
  File(const std::string& path, bool write, bool append = true) : _path(path), _open(true), _write(write), _append(append) {}
  static File dir(const std::string& path) {
    File d(path, false);
    d._dir = true;
    std::string pre = path + "/";
    for (auto& kv : host::files()) if (kv.first.rfind(pre, 0) == 0) d._list.push_back(kv.first);
    return d;
  }
  explicit operator bool() const { return _open; }
  size_t size() const { return _dir ? 0 : data().size(); }
  bool seek(size_t p) { _pos = p; return true; }
  int available() override { return _open && !_dir && _pos < data().size() ? (int)(data().size() - _pos) : 0; }
  int read() override { return available() ? (uint8_t)data()[_pos++] : -1; }
  size_t read(uint8_t* b, size_t n) { size_t i = 0; while (i < n && available()) b[i++] = (uint8_t)data()[_pos++]; return i; }
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* b, size_t n) override {
    std::string& d = host::files()[_path];
    if (_append) { d.append((const char*)b, n); return n; }
    if (d.size() < _pos + n) d.resize(_pos + n);           // "r+" / "w": write at the position
    memcpy(&d[_pos], b, n);
    _pos += n;
    return n;
  }
  void close() { _open = false; }
  bool isDirectory() const { return _dir; }
  const char* name() const { return _path.c_str(); }
  File openNextFile() {
    if (!_dir || _li >= _list.size()) return File();
    return File(_list[_li++], false);
  }
private:
  const std::string& data() const { return host::files()[_path]; }
  std::string _path;
  bool _open = false, _write = false, _append = true, _dir = false;
  size_t _pos = 0, _li = 0;
  std::vector<std::string> _list;
};
