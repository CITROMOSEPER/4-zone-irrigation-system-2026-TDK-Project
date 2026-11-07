#pragma once
#include <Arduino.h>
#include <FS.h>
#include <map>
#include <vector>
enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST };
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
// Records routes; a test calls request() and reads the answer
class WebServer {
public:
  explicit WebServer(int) {}
  void begin() {}
  void handleClient() {}
  void on(const char* p, HTTPMethod m, std::function<void()> fn) { _routes.push_back({p, m, fn}); }
  void onNotFound(std::function<void()> fn) { _nf = fn; }
  bool hasArg(const char* k) { return _args.count(k) > 0; }
  String arg(const char* k) { auto it = _args.find(k); return it == _args.end() ? String("") : String(it->second); }
  void sendHeader(const char*, const String&, bool = false) {}
  void setContentLength(size_t) {}
  void send(int code, const char* = "", const String& body = String()) { status = code; this->body = body.c_str(); }
  void send(int code, const char* t, const char* body) { send(code, t, String(body)); }
  void send_P(int code, const char*, const char* d, size_t n) { status = code; body.assign(d, n); }
  void sendContent(const char* b, size_t n) { body.append(b, n); }
  void sendContent(const char* b) { body += b; }
  void sendContent(const String& b) { body += b.c_str(); }
  size_t streamFile(File& f, const char*) { status = 200; body.clear(); int c; while ((c = f.read()) >= 0) body += (char)c; return body.size(); }
  String hostHeader() { return String("x"); }
  String uri() { return String("/"); }

  // ---- test side ----
  std::string request(HTTPMethod m, const std::string& path, std::map<std::string, std::string> args = {}) {
    _args = args; status = 0; body.clear();
    for (auto& r : _routes) if (r.path == path && (r.m == m || r.m == HTTP_ANY)) { r.fn(); return body; }
    status = 404; return "";
  }
  int status = 0;
  std::string body;
private:
  struct Route { std::string path; HTTPMethod m; std::function<void()> fn; };
  std::vector<Route> _routes;
  std::function<void()> _nf;
  std::map<std::string, std::string> _args;
};
