#pragma once
// Error LED and passive buzzer: every sound and blink of the "Button & feedback" spec.
//
// Sounds are PWM tones (LEDC channel 2, on its own timer: the LED's channel 0 keeps 5 kHz).
// The sound vocabulary (3 Nov 2026; slides replaced by notes the same day: they sounded cheap):
//   up = something starts or turns on, down = it ends or turns off;
//   high notes = OK, low notes = a problem; 2-3 short notes = an event, 4 notes with a held last
//   one = a mode change (winter); short = "got it", repeating = "come and look".
// Notes: C7 2093, E7 2637, G7 3136, C8 4186 Hz; low E6 1319, G6 1568 Hz.
//
// Each output has a short queue of one-shot patterns (foreground). When it is empty the
// background shows the system state:
//   LED:    serious error 300/200 > Wi-Fi setup sine pulse (1200 ms) > winter 1500 on every 5 s > off
//   buzzer: serious error alarm (G7/E6 x3, 3 s pause), repeating, until a button press mutes it
// buzz = 0 (setting) mutes every sound except the output test. The pin stays LOW when silent.
#include <Arduino.h>
#include "Config.h"

// LED pattern: milliseconds, alternating ON and OFF, starting with ON
struct Pattern {
  const uint16_t* ms;
  uint8_t n;
};

// One step of a sound: a tone from f0 to f1 Hz (a slide when they differ); f0 = 0 is silence
struct Note { uint16_t f0, f1, ms; };
struct Tune {
  const Note* notes;
  uint8_t n;
};

// One LED output's player: a queue of one-shot patterns over a repeating background
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

// The buzzer's player: a queue of tunes
class TunePlayer {
public:
  void play(const Tune& t);
  void clear() { _qn = 0; _active = false; }
  // Frequency now (0 = silent); 'running' false when nothing is playing
  uint16_t update(uint32_t now, bool& running);

private:
  static constexpr uint8_t QMAX = 4;
  Tune _q[QMAX];
  uint8_t _qn = 0;
  Tune _cur{nullptr, 0};
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

  // ---- events ----
  void press();          // tick + LED 150
  void noDeficit();      // two soft equal notes + 2 flashes
  void refused();        // low "bonk" + 3 short flashes
  void winterOn();       // C8 G7 E7 C7, falling
  void winterOff();      // C7 E7 G7 C8, rising
  void cycleStart();     // two rising notes
  void cycleEnd();       // two falling notes
  void wifiConnected();  // G7 C8, quick

  // ---- output test (ignores the sound setting) ----
  void testLed(bool on) { _testLed = on; }
  void testBuzzer();     // every note used, low to high

  uint16_t toneNow() const { return _lastFreq; }   // for the host tests

private:
  void sound(const Tune& t) { if (_soundOn) _buz.play(t); }
  void flash(const Pattern& p) { _led.play(p); }
  void ledWrite(uint8_t duty);
  void tone(uint16_t f);

  uint8_t _ledPin = 0, _buzPin = 0;
  PatternPlayer _led;
  TunePlayer _buz;
  uint8_t _errMask = 0;
  bool _errMuted = false, _setup = false, _winter = false, _soundOn = true, _testLed = false;
  uint32_t _bgT0 = 0;
  uint8_t _lastDuty = 255;
  uint16_t _lastFreq = 0xFFFF;
};
