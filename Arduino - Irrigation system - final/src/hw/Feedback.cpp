#include "hw/Feedback.h"
#include <math.h>

// ---- patterns (ms, ON first) ----
namespace {
const uint16_t P150[]       = {150};
const uint16_t P250[]       = {250};
const uint16_t P750[]       = {750};
const uint16_t TWO_150[]    = {150, 300, 150};            // two beeps / two flashes
const uint16_t THREE_100[]  = {100, 100, 100, 100, 100};  // three quick flashes
const uint16_t WINTER_OFF[] = {750, 300, 150};
const uint16_t WINTER_ON[]  = {150, 300, 150, 300, 1000};
const uint16_t ERR_BUZ[]    = {150, 300, 150, 300, 150, 3000};   // repeating, ends with OFF
const uint16_t ERR_LED[]    = {300, 200};                        // repeating
const uint16_t WINTER_LED[] = {1500, 3500};                      // 1500 on every 5 s

template <size_t N> Pattern pat(const uint16_t (&a)[N]) { return Pattern{a, (uint8_t)N}; }

constexpr uint8_t  LED_CH   = 0;
constexpr uint32_t SINE_MS  = 1200;

// Level of a repeating pattern at time 'ms' since its start
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
}  // namespace

// ---------------- PatternPlayer ----------------
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

// ---------------- Feedback ----------------
void Feedback::begin(uint8_t ledPin, uint8_t buzzerPin) {
  _ledPin = ledPin;
  _buzPin = buzzerPin;
  pinMode(_buzPin, OUTPUT);
  digitalWrite(_buzPin, LOW);
  ledcSetup(LED_CH, 5000, 8);
  ledcAttachPin(_ledPin, LED_CH);
  ledWrite(0);
  _bgT0 = millis();
}

void Feedback::setState(uint8_t errorMask, bool setupAp, bool winter) {
  if (errorMask & ~_errMask) _errMuted = false;   // a new error beeps again
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
  bool b = _buz.update(now, running);
  if (!running) b = errorSounding() && repeatLevel(ERR_BUZ, 6, bg);
  digitalWrite(_buzPin, b ? HIGH : LOW);
}

void Feedback::ledWrite(uint8_t duty) {
  if (duty == _lastDuty) return;
  _lastDuty = duty;
  ledcWrite(LED_CH, duty);
}

void Feedback::press()         { _led.clear(); _buz.clear(); flash(pat(P150)); beep(pat(P150)); }
void Feedback::noDeficit()     { flash(pat(TWO_150)); beep(pat(TWO_150)); }
void Feedback::refused()       { flash(pat(THREE_100)); beep(pat(P750)); }
void Feedback::winterOff()     { beep(pat(WINTER_OFF)); }
void Feedback::longConfirm()   { flash(pat(TWO_150)); beep(pat(TWO_150)); }
void Feedback::winterOn()      { beep(pat(WINTER_ON)); }
void Feedback::cycleStart()    { beep(pat(P250)); }
void Feedback::cycleEnd()      { beep(pat(P250)); }
void Feedback::wifiConnected() { beep(pat(P250)); }
void Feedback::testBuzzer()    { _buz.play(pat(TWO_150)); }
