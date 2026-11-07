#include "hw/Feedback.h"
#include <math.h>

namespace {
// ---- LED patterns (ms, ON first) ----
const uint16_t P150[]       = {150};
const uint16_t TWO_150[]    = {150, 300, 150};            // two flashes
const uint16_t THREE_100[]  = {100, 100, 100, 100, 100};  // three quick flashes
const uint16_t ERR_LED[]    = {300, 200};                 // repeating
const uint16_t WINTER_LED[] = {1500, 3500};               // 1500 on every 5 s

// ---- sounds ----
constexpr uint16_t E6 = 1319, G6 = 1568, C7 = 2093, E7 = 2637, G7 = 3136, C8 = 4186;
const Note T_TICK[]    = {{E7, E7, 30}};
const Note T_NOTHING[] = {{E7, E7, 80}, {0, 0, 60}, {E7, E7, 80}};
const Note T_REFUSED[] = {{G6, G6, 150}, {0, 0, 30}, {E6, E6, 150}};
// Mode changes: four notes, the last one held (a passive buzzer turns a pitch slide into a cheap zip)
const Note T_WINT_ON[] = {{C8, C8, 110}, {0, 0, 25}, {G7, G7, 110}, {0, 0, 25}, {E7, E7, 110}, {0, 0, 25}, {C7, C7, 260}};
const Note T_WINT_OFF[] = {{C7, C7, 110}, {0, 0, 25}, {E7, E7, 110}, {0, 0, 25}, {G7, G7, 110}, {0, 0, 25}, {C8, C8, 260}};
const Note T_START[]   = {{C7, C7, 90}, {0, 0, 30}, {G7, G7, 90}};
const Note T_END[]     = {{G7, G7, 90}, {0, 0, 30}, {C7, C7, 90}};
const Note T_WIFI[]    = {{G7, G7, 60}, {0, 0, 25}, {C8, C8, 90}};          // a quick high pair: "online"
// Output test: every note the controller uses, low to high, so each can be heard on its own
const Note T_TEST[]    = {{E6, E6, 150}, {0, 0, 60}, {G6, G6, 150}, {0, 0, 60}, {C7, C7, 150}, {0, 0, 60},
                          {E7, E7, 150}, {0, 0, 60}, {G7, G7, 150}, {0, 0, 60}, {C8, C8, 150}};
// The alarm: G7/E6 three times, then 3 s of silence, repeating (background, not queued)
const Note T_ALARM[]   = {{G7, G7, 120}, {E6, E6, 120}, {G7, G7, 120}, {E6, E6, 120}, {G7, G7, 120}, {E6, E6, 120}, {0, 0, 3000}};

template <size_t N> Pattern pat(const uint16_t (&a)[N]) { return Pattern{a, (uint8_t)N}; }
template <size_t N> Tune tune(const Note (&a)[N]) { return Tune{a, (uint8_t)N}; }

constexpr uint8_t  LED_CH   = 0;     // timer 0
constexpr uint8_t  BUZ_CH   = 2;     // timer 1: its own frequency
constexpr uint32_t SINE_MS  = 1200;

// Level of a repeating LED pattern at time 'ms' since its start
bool repeatLevel(const uint16_t* p, uint8_t n, uint32_t ms) {
  uint32_t total = 0;
  for (uint8_t i = 0; i < n; i++) total += p[i];
  uint32_t t = ms % total;
  for (uint8_t i = 0; i < n; i++) {
    if (t < p[i]) return (i % 2) == 0;
    t -= p[i];
  }
  return false;
}

// Frequency of a note at 't' ms into it (slides: geometric, so it sounds even)
uint16_t noteFreq(const Note& n, uint32_t t) {
  if (!n.f0 || n.f0 == n.f1 || !n.ms) return n.f0;
  float k = (float)t / n.ms;
  return (uint16_t)(n.f0 * powf((float)n.f1 / n.f0, k));
}

// Frequency of a repeating tune at 'ms' since its start
uint16_t repeatTune(const Note* t, uint8_t n, uint32_t ms) {
  uint32_t total = 0;
  for (uint8_t i = 0; i < n; i++) total += t[i].ms;
  uint32_t x = ms % total;
  for (uint8_t i = 0; i < n; i++) {
    if (x < t[i].ms) return noteFreq(t[i], x);
    x -= t[i].ms;
  }
  return 0;
}
}  // namespace

// ---------------- PatternPlayer (LED) ----------------
void PatternPlayer::play(const Pattern& p) {
  if (!_active) {
    _cur = p;
    _step = 0;
    _t0 = millis();
    _active = true;
  } else if (_qn < QMAX) {
    _q[_qn++] = p;
  }
}

bool PatternPlayer::update(uint32_t now, bool& running) {
  while (_active) {
    if (_step >= _cur.n) {                      // finished: a short gap, then the next one
      if (now - _t0 < 120 && _qn) { running = true; return false; }
      if (_qn) {
        _cur = _q[0];
        for (uint8_t i = 1; i < _qn; i++) _q[i - 1] = _q[i];
        _qn--;
        _step = 0;
        _t0 = now;
        continue;
      }
      _active = false;
      break;
    }
    if (now - _t0 >= _cur.ms[_step]) {
      _t0 += _cur.ms[_step];
      _step++;
      continue;
    }
    running = true;
    return (_step % 2) == 0;
  }
  running = false;
  return false;
}

// ---------------- TunePlayer (buzzer) ----------------
void TunePlayer::play(const Tune& t) {
  if (!_active) {
    _cur = t;
    _step = 0;
    _t0 = millis();
    _active = true;
  } else if (_qn < QMAX) {
    _q[_qn++] = t;
  }
}

uint16_t TunePlayer::update(uint32_t now, bool& running) {
  while (_active) {
    if (_step >= _cur.n) {                      // finished: a short gap between two tunes
      if (_qn && now - _t0 < 60) { running = true; return 0; }
      if (_qn) {
        _cur = _q[0];
        for (uint8_t i = 1; i < _qn; i++) _q[i - 1] = _q[i];
        _qn--;
        _step = 0;
        _t0 = now;
        continue;
      }
      _active = false;
      break;
    }
    const Note& n = _cur.notes[_step];
    if (now - _t0 >= n.ms) {
      _t0 += n.ms;
      _step++;
      continue;
    }
    running = true;
    return noteFreq(n, now - _t0);
  }
  running = false;
  return 0;
}

// ---------------- Feedback ----------------
void Feedback::begin(uint8_t ledPin, uint8_t buzzerPin) {
  _ledPin = ledPin;
  _buzPin = buzzerPin;
  digitalWrite(_buzPin, LOW);                   // a passive buzzer must not sit on DC
  pinMode(_buzPin, OUTPUT);
  ledcSetup(BUZ_CH, 2000, 8);
  ledcAttachPin(_buzPin, BUZ_CH);
  tone(0);
  ledcSetup(LED_CH, 5000, 8);
  ledcAttachPin(_ledPin, LED_CH);
  ledWrite(0);
  _bgT0 = millis();
}

void Feedback::setState(uint8_t errorMask, bool setupAp, bool winter) {
  if (errorMask & ~_errMask) _errMuted = false;   // a new error sounds again
  if (!errorMask) _errMuted = false;
  if (errorMask != _errMask || setupAp != _setup || winter != _winter) _bgT0 = millis();
  _errMask = errorMask;
  _setup = setupAp;
  _winter = winter;
}

void Feedback::update() {
  uint32_t now = millis(), bg = now - _bgT0;
  bool running;

  // ---- LED ----
  bool fg = _led.update(now, running);
  uint8_t duty = 0;
  if (_testLed) duty = 255;
  else if (running) duty = fg ? 255 : 0;
  else if (_errMask) duty = repeatLevel(ERR_LED, 2, bg) ? 255 : 0;
  else if (_setup) {
    float ph = (float)(now % SINE_MS) / SINE_MS;
    float v = (1.0f - cosf(2.0f * (float)M_PI * ph)) * 0.5f;
    duty = (uint8_t)(255.0f * powf(v, 2.2f));     // gamma-corrected: looks like a smooth breath
  }
  else if (_winter) duty = repeatLevel(WINTER_LED, 2, bg) ? 255 : 0;
  ledWrite(duty);

  // ---- buzzer ----
  uint16_t f = _buz.update(now, running);
  if (!running) f = errorSounding() ? repeatTune(T_ALARM, sizeof(T_ALARM) / sizeof(T_ALARM[0]), bg) : 0;
  tone(f);
}

void Feedback::ledWrite(uint8_t duty) {
  if (duty == _lastDuty) return;
  _lastDuty = duty;
  ledcWrite(LED_CH, duty);
}

// Retuning the timer costs time and can click: only when the pitch really changes
// (a slide changes it every few ms, which is what makes it smooth)
void Feedback::tone(uint16_t f) {
  if (f == _lastFreq) return;
  if (f && _lastFreq && _lastFreq != 0xFFFF && abs((int)f - (int)_lastFreq) < 8) return;
  _lastFreq = f;
  if (f) ledcWriteTone(BUZ_CH, f);              // 50 % duty at f
  else ledcWrite(BUZ_CH, 0);                    // silent: the pin stays LOW
}

void Feedback::press()         { _led.clear(); _buz.clear(); flash(pat(P150)); sound(tune(T_TICK)); }
void Feedback::noDeficit()     { flash(pat(TWO_150)); sound(tune(T_NOTHING)); }
void Feedback::refused()       { flash(pat(THREE_100)); sound(tune(T_REFUSED)); }
void Feedback::winterOn()      { _buz.clear(); sound(tune(T_WINT_ON)); }
void Feedback::winterOff()     { _buz.clear(); sound(tune(T_WINT_OFF)); }
void Feedback::cycleStart()    { sound(tune(T_START)); }
void Feedback::cycleEnd()      { sound(tune(T_END)); }
void Feedback::wifiConnected() { sound(tune(T_WIFI)); }
void Feedback::testBuzzer()    { _buz.clear(); _buz.play(tune(T_TEST)); }
