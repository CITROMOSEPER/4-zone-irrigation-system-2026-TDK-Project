#!/bin/sh
# Compile the firmware for the PC against the stub headers, plus the tests, and run them.
#   test/host/build.sh <path to ArduinoJson/src>
set -e
AJ=${1:?usage: build.sh <ArduinoJson src dir>}
OUT=${OUT:-/tmp/irrigation-host}
mkdir -p "$OUT"
FLAGS="-std=gnu++17 -g -O0 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
  -DARDUINO=10819 -DARDUINOJSON_ENABLE_PROGMEM=0 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0
  -Itest/host/stubs -Itest/host/gen -Iinclude -Isrc -I$AJ"
OBJS=""
for f in $(find src -name '*.cpp' ! -name main.cpp) test/host/host_runtime.cpp test/host/tests.cpp; do
  o="$OUT/$(echo "$f" | tr / _).o"
  g++ $FLAGS -c "$f" -o "$o"
  OBJS="$OBJS $o"
done
g++ $OBJS -o "$OUT/tests"
"$OUT/tests"
