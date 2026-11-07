#include "logic/Weather.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <math.h>
#include "Config.h"
#include "System.h"
#include "util/TimeUtil.h"
#include "util/Util.h"

namespace {
struct Lock {
  SemaphoreHandle_t m;
  explicit Lock(void* p) : m((SemaphoreHandle_t)p) { xSemaphoreTake(m, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(m); }
};
float numOr(JsonVariantConst v, float def) { return v.is<float>() ? v.as<float>() : def; }

const char* const MODEL[WX_MODELS] = {"icon_seamless", "ecmwf_ifs025"};

// One model's series: "name_model"; an answer for a single model has plain "name" (taken as model 0)
JsonArrayConst series(JsonObjectConst o, const char* name, uint8_t m) {
  char k[64];
  snprintf(k, sizeof(k), "%s_%s", name, MODEL[m]);
  JsonArrayConst a = o[k];
  if (a.isNull() && m == 0) a = o[name];
  return a;
}

float meanOf(JsonObjectConst o, const char* name, size_t i) {
  float sum = 0;
  uint8_t n = 0;
  for (uint8_t m = 0; m < WX_MODELS; m++) {
    float v = numOr(series(o, name, m)[i], NAN);
    if (!isnan(v)) { sum += v; n++; }
  }
  return n ? sum / n : NAN;
}
}  // namespace

void Weather::begin() {
  _mx = xSemaphoreCreateMutex();
  // 12 kB: the TLS handshake inside HTTPClient needs most of it (8 kB was too tight for the 1.3.2 answer)
  xTaskCreatePinnedToCore(taskMain, "wx", WX_STACK, this, 1, nullptr, 0);   // core 0, beside the Wi-Fi stack
}

void Weather::taskMain(void* self) { static_cast<Weather*>(self)->loop(); }

void Weather::loop() {
  uint32_t next = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    bool online = WiFi.status() == WL_CONNECTED && sys.clock.valid();
    if (!online) continue;
    if (!_refresh && (int32_t)(millis() - next) < 0) continue;
    _refresh = false;
    bool ok = fetch();
    next = millis() + (ok ? WX_PERIOD_MS : WX_RETRY_MS);
  }
}

bool Weather::fetch() {
  char url[512];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&models=icon_seamless,ecmwf_ifs025"
           "&daily=temperature_2m_max,temperature_2m_min,et0_fao_evapotranspiration,precipitation_sum"
           "&hourly=temperature_2m,precipitation"
           "&timezone=auto&past_days=1&forecast_days=7&past_hours=24&forecast_hours=72",
           sys.cfg.g.lat, sys.cfg.g.lon);

  WiFiClientSecure tls;
  tls.setInsecure();   // public, non-secret data; no certificate bundle in flash
  HTTPClient http;
  http.setTimeout(10000);
  bool ok = false;
  Diag dg{sys.clock.unixNow(), 0, 0, false, 0, (uint32_t)ESP.getMaxAllocHeap()};
  _n = WxSnapshot{};
  if (http.begin(tls, url)) {
    int code = http.GET();
    dg.code = code;
    if (code == 200) {
      String body = http.getString();             // decodes chunked transfer; the answer is about 5 kB
      dg.bytes = body.length();
      ok = dg.parsed = parse(body.c_str(), _n);
    }
    http.end();
  } else {
    dg.code = -100;                                // the URL was refused
  }
  dg.stackLeft = (uint32_t)uxTaskGetStackHighWaterMark(nullptr);
  Serial.printf("WX: HTTP %d, %lu B, %s, stack left %lu B, largest heap block %lu B\n", dg.code,
                (unsigned long)dg.bytes, ok ? "ok" : "FAILED", (unsigned long)dg.stackLeft, (unsigned long)dg.heapLargest);

  Lock l(_mx);
  _diag = dg;
  if (ok) {
    _n.ok = true;
    _n.t = sys.clock.unixNow();
    _c = _n;
  } else {
    _c.ok = false;                 // keep the old days and their time
  }
  return ok;
}

bool Weather::parse(const char* body, WxSnapshot& s) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  JsonObjectConst d = doc["daily"];
  JsonArrayConst time = d["time"];
  for (size_t i = 0; i < time.size() && s.n < WX_DAYS; i++) {
    WxDay w;
    w.ymd = TimeUtil::parseYmd(time[i] | "");
    if (!w.ymd) continue;
    w.tmax = meanOf(d, "temperature_2m_max", i);
    w.tmin = meanOf(d, "temperature_2m_min", i);
    w.et0 = meanOf(d, "et0_fao_evapotranspiration", i);
    w.rain = meanOf(d, "precipitation_sum", i);
    if (isnan(w.rain)) w.rain = 0;
    for (uint8_t m = 0; m < WX_MODELS; m++) w.rainM[m] = numOr(series(d, "precipitation_sum", m)[i], NAN);
    s.d[s.n++] = w;
  }
  // hourly: local times "YYYY-MM-DDTHH:MM", one per hour from the first
  JsonObjectConst h = doc["hourly"];
  JsonArrayConst ht = h["time"];
  const char* first = ht[0] | "";
  char date[11] = {0};
  strncpy(date, first, 10);
  uint32_t ymd = strlen(first) >= 16 && first[10] == 'T' ? TimeUtil::parseYmd(date) : 0;
  s.h0 = ymd ? TimeUtil::localToUnix(ymd, atoi(first + 11) * 60 + atoi(first + 14)) : 0;
  size_t nv = ht.size();                            // values follow the first time, one per hour
  for (uint8_t m = 0; m < WX_MODELS; m++) nv = max(nv, series(h, "temperature_2m", m).size());
  if (s.h0) {
    for (size_t i = 0; i < nv && s.nh < WX_HOURS; i++) {
      float v = meanOf(h, "temperature_2m", i);
      if (isnan(v)) break;                          // a gap: keep the hours before it
      s.hr[s.nh] = (int16_t)lroundf(v * 10);
      for (uint8_t m = 0; m < WX_MODELS; m++) {
        float r = numOr(series(h, "precipitation", m)[i], NAN);
        s.hp[m][s.nh] = isnan(r) ? -1 : (int16_t)lroundf(r * 10);
      }
      s.nh++;
    }
  }
  // An answer without the values we asked for (a renamed field, an unknown model) is a failed fetch: the
  // old data stays and the WX log row says so, instead of a week of zeros
  bool any = false;
  for (uint8_t i = 0; i < s.n; i++) if (!isnan(s.d[i].tmax) && !isnan(s.d[i].et0)) any = true;
  return s.n > 0 && any;
}

void Weather::inject(const WxSnapshot& s) {
  if (!_mx) _mx = xSemaphoreCreateMutex();
  Lock l(_mx);
  _c = s;
  _c.ok = true;
  _c.t = sys.clock.unixNow();
}

float Weather::tempAt(uint32_t t) const {
  if (!t || !fresh()) return NAN;
  WxSnapshot s = snapshot();
  if (!s.nh || t < s.h0) return NAN;
  uint32_t i = (t - s.h0) / 3600;
  if (i + 1 >= s.nh) return NAN;
  float f = ((t - s.h0) % 3600) / 3600.0f;
  return (s.hr[i] + (s.hr[i + 1] - s.hr[i]) * f) / 10.0f;
}

void Weather::rainWindow(uint32_t from, uint8_t hours, float mm[WX_MODELS], bool have[WX_MODELS]) const {
  WxSnapshot s = snapshot();
  for (uint8_t m = 0; m < WX_MODELS; m++) { mm[m] = 0; have[m] = false; }
  if (!s.nh || from < s.h0) return;
  uint32_t i0 = (from - s.h0) / 3600;
  if (i0 + hours > s.nh) return;
  for (uint8_t m = 0; m < WX_MODELS; m++) {
    have[m] = true;
    for (uint32_t i = i0; i < i0 + hours; i++) {
      if (s.hp[m][i] < 0) { have[m] = false; break; }
      mm[m] += s.hp[m][i] / 10.0f;
    }
  }
}

bool Weather::between(uint32_t from, uint32_t to, float& et0, float& rain) const {
  et0 = rain = 0;
  if (to <= from) return true;
  WxSnapshot s = snapshot();
  const uint32_t trust = from + WX_RAIN_TRUST_S;
  uint32_t covered = 0;
  for (uint8_t i = 0; i < s.n; i++) {
    const WxDay& w = s.d[i];
    uint32_t a = TimeUtil::localToUnix(w.ymd, 0), b = TimeUtil::localToUnix(w.ymd, 1440);
    uint32_t lo = max(a, from), hi = min(b, to);
    if (hi <= lo) continue;
    float len = (float)(b - a);
    float e = w.et0;
    if (isnan(e)) e = hargreavesEt0(w.tmin, w.tmax, sys.cfg.g.lat, TimeUtil::dayOfYear(w.ymd));
    if (isnan(e)) return false;
    et0 += e * (hi - lo) / len;
    uint32_t rhi = min(hi, trust);
    if (rhi > lo) rain += w.rain * (rhi - lo) / len;
    covered += hi - lo;
  }
  return covered + 60 >= to - from;
}

WxSnapshot Weather::snapshot() const {
  if (!_mx) return WxSnapshot{};
  Lock l(_mx);
  return _c;
}

bool Weather::day(uint32_t ymd, WxDay& out) const {
  WxSnapshot s = snapshot();
  for (uint8_t i = 0; i < s.n; i++) {
    if (s.d[i].ymd == ymd) { out = s.d[i]; return true; }
  }
  return false;
}

// Asked every second (alerts, the state): only the fetch time is read, not a copy of the whole cache
bool Weather::fresh() const {
  uint32_t t = 0;
  if (_mx) { Lock l(_mx); t = _c.t; }
  uint32_t now = sys.clock.unixNow();
  return t && now >= t && now - t < WX_FRESH_S;
}

float Weather::hargreavesEt0(float tMin, float tMax, float latDeg, int doy) {
  if (isnan(tMin) || isnan(tMax) || tMax < tMin) return NAN;
  // Extraterrestrial radiation Ra (FAO-56 eq. 21), MJ m-2 day-1
  const float phi = latDeg * (float)M_PI / 180.0f;
  const float dr = 1 + 0.033f * cosf(2 * (float)M_PI / 365 * doy);
  const float decl = 0.409f * sinf(2 * (float)M_PI / 365 * doy - 1.39f);
  const float ws = acosf(clampv(-tanf(phi) * tanf(decl), -1.0f, 1.0f));
  const float ra = 24 * 60 / (float)M_PI * 0.0820f * dr *
                   (ws * sinf(phi) * sinf(decl) + cosf(phi) * cosf(decl) * sinf(ws));
  const float tMean = (tMax + tMin) / 2;
  // FAO-56 eq. 52; 0.408 converts MJ m-2 to mm of evaporated water
  float et0 = 0.0023f * (tMean + 17.8f) * sqrtf(tMax - tMin) * 0.408f * ra;
  return et0 < 0 ? 0 : et0;
}
