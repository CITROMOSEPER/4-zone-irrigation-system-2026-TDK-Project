#pragma once
#include <WiFiClientSecure.h>
class HTTPClient {
public:
  void setTimeout(uint32_t) {}
  bool begin(WiFiClientSecure& c, const char*) { _c = &c; return true; }
  int GET() { return -1; }
  Stream& getStream() { return *_c; }
  String getString() { return String(""); }
  void end() {}
private:
  WiFiClientSecure* _c = nullptr;
};
