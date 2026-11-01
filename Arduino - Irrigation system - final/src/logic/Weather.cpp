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
}  // namespace

void Weather::begin() {
  _mx = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(taskMain, "wx", 8192, this, 1, nullptr, 0);   // core 0, beside the Wi-Fi stack
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
  char url[300];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&daily=temperature_2m_max,temperature_2m_min,et0_fao_evapotranspiration,precipitation_sum,precipitation_probability_max"
           "&timezone=auto&past_days=1&forecast_days=2",
           sys.cfg.g.lat, sys.cfg.g.lon);

  WiFiClientSecure tls;
  tls.setInsecure();   // public, non-secret data; no certificate bundle in flash
  HTTPClient http;
  http.setTimeout(10000);
  bool ok = false;
  WxSnapshot s{};
  if (http.begin(tls, url)) {
    int code = http.GET();
    if (code == 200) {
      String body = http.getString();             // decodes chunked transfer; the answer is about 1 kB
      JsonDocument doc;
      if (!deserializeJson(doc, body)) {
        JsonObjectConst d = doc["daily"];
        JsonArrayConst time = d["time"];
        for (size_t i = 0; i < time.size() && s.n < 3; i++) {
          WxDay w;
          w.ymd = TimeUtil::parseYmd(time[i] | "");
          if (!w.ymd) continue;
          w.tmax = numOr(d["temperature_2m_max"][i], NAN);
          w.tmin = numOr(d["temperature_2m_min"][i], NAN);
          w.et0 = numOr(d["et0_fao_evapotranspiration"][i], NAN);
          w.rain = numOr(d["precipitation_sum"][i], 0);
          w.rp = d["precipitation_probability_max"][i].is<int>() ? d["precipitation_probability_max"][i].as<int>() : -1;
          s.d[s.n++] = w;
        }
        ok = s.n > 0;
      }
    }
    http.end();
  }

  Lock l(_mx);
  if (ok) {
    s.ok = true;
    s.t = sys.clock.unixNow();
    _c = s;
  } else {
    _c.ok = false;                 // keep the old days and their time
  }
  return ok;
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

bool Weather::fresh() const {
  WxSnapshot s = snapshot();
  uint32_t now = sys.clock.unixNow();
  return s.t && now >= s.t && now - s.t < WX_FRESH_S;
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
