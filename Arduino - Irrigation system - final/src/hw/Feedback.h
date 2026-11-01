#pragma once
// Error LED and buzzer: every sound and blink of the "Button & feedback" spec.
//
// A pattern is a list of milliseconds, alternating ON and OFF and starting with ON.
// Each output has a short queue of one-shot patterns (foreground). When it is empty the
// background shows the system state:
//   LED:    serious error 300/200 > Wi-Fi setup sine pulse (1200 ms) > winter 1500 on every 5 s > off
//   buzzer: serious error 150/300/150/300/150/3000, repeating, until a button press mutes it
// buzz = 0 (setting) mutes every sound except the output test.
#include <Arduino.h>
#include "Config.h"

struct Pattern {
  const uint16_t* ms;
  uint8_t n;
};

// One output's player: a queue of one-shot patterns over a repeating background
class PatternPlayer {
public:
  void play(const Pattern& p);                 // queued after what is playing
  void clear() { _qn = 0; _active = false; }
  bool busy() const { return _active || _qn; }
  // Level of the foreground (true = on); 'running' false when nothing is playing
  bool update(uint32_t now, bool& running);

private:
  static constexpr uint8_t QMAX = 4;
  Pattern _q[QMAX];
  uint8_t _qn = 0;
  Pattern _cur{nullptr, 0};
  uint8_t _step = 0;
  uint32_t _t0 = 0;
  bool _active = false;
};

class Feedback {
public:
  void begin(uint8_t ledPin, uint8_t buzzerPin);
  void update();

  void setSoundOn(bool on) { _soundOn = on; }

  // System state for the backgrounds. errorMask: one bit per serious error.
  void setState(uint8_t errorMask, bool setupAp, bool winter);
  bool errorSounding() const { return _errMask && !_errMuted && _soundOn; }
  void muteError() { _errMuted = true; }

  // ---- events (the spec) ----
  void press();          // LED 150 + beep 150
  void noDeficit();      // 2 short beeps + 2 flashes
  void refused();        // 750 ms beep + 3 short flashes
  void winterOff();      // 750 on, 300 off, 150 on
  void longConfirm();    // beep 150/300/150 + 2 flashes
  void winterOn();       // 150/300/150/300/1000
  void cycleStart();     // 250 ms
  void cycleEnd();       // 250 ms
  void wifiConnected();  // 250 ms

  // ---- output test (ignores the sound setting) ----
  void testLed(bool on) { _testLed = on; }
  void testBuzzer();     // two beeps

private:
  void beep(const Pattern& p) { if (_soundOn) _buz.play(p); }
  void flash(const Pattern& p) { _led.play(p); }
  void ledWrite(uint8_t duty);

  uint8_t _ledPin = 0, _buzPin = 0;
  PatternPlayer _led, _buz;
  uint8_t _errMask = 0;
  bool _errMuted = false, _setup = false, _winter = false, _soundOn = true, _testLed = false;
  uint32_t _bgT0 = 0;
  uint8_t _lastDuty = 255;
};
