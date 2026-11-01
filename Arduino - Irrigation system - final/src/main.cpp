// Adaptive irrigation controller, 4 zones. TDK 2026, Óbuda University.
// Everything lives in System (System.h); this file only adds the serial bench commands.
#include <Arduino.h>
#include "System.h"
#include "net/Health.h"

static void serialCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c >= '1' && c < '1' + ZONES) {
      // bench test: 1 minute, overriding DND and the cold limit (the safety rules still apply)
      WaterAnswer a = sys.irr.waterNow(c - '1', MIN_RUN_S, false, true, true);
      Serial.printf("water zone %c: %s\n", c, a.r);
    } else if (c == 's') {
      sys.irr.stopAll(StopReason::USER);
      Serial.println("stopped");
    } else if (c == 'x') {
      Serial.println(sys.water.clearFaults() ? "faults cleared" : "no fault");
    } else if (c == 'p') {
      Serial.print(healthText());
    }
  }
}

void setup() {
  sys.begin();
}

void loop() {
  sys.loop();
  serialCommands();
}
