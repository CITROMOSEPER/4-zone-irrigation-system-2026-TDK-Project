#pragma once
#include <stdint.h>

// Per-zone arrays: zone 1 = index 0. Their length must match ZONES in Config.h (checked in System.h).

// ---- Zones ----
// Soil sensors HW-390: ADC1 pins only (ADC2 is unusable while WiFi is on)
constexpr uint8_t PIN_SOIL[4]  = {36, 39, 34, 35};
// Valves (relay board is active-HIGH, see RELAY_ACTIVE_LOW in Config.h)
constexpr uint8_t PIN_VALVE[4] = {19, 18, 17, 16};

// ---- Pump ----
constexpr uint8_t PIN_PUMP = 13;

// ---- Rain sensor SA-28 (digital, active LOW), powered from a GPIO only while sampling ----
// DO uses the internal pull-up: no external pull-down, or it reads wet forever.
// GPIO5 is a strapping pin; the pull-up keeps it HIGH at boot, as required.
constexpr uint8_t PIN_RAIN_OUT = 5;
constexpr uint8_t PIN_RAIN_VCC = 14;

// ---- Tank sensor HC-SR04 (echo through a 1k/2k divider to 3.3 V) ----
constexpr uint8_t PIN_TANK_TRIG = 27;
constexpr uint8_t PIN_TANK_ECHO = 26;

// ---- BME280 (I2C) ----
constexpr uint8_t PIN_I2C_SDA = 21;
constexpr uint8_t PIN_I2C_SCL = 22;

// ---- DS1302 real-time clock ----
constexpr uint8_t PIN_RTC_CLK = 25;
constexpr uint8_t PIN_RTC_DAT = 33;
constexpr uint8_t PIN_RTC_RST = 32;

// ---- User interface ----
constexpr uint8_t PIN_BUTTON    = 4;    // 1k pull-down to GND, button to 3.3 V (pressed = HIGH)
constexpr uint8_t PIN_ERROR_LED = 23;   // driven by LEDC PWM (sine pulse in Wi-Fi setup)
constexpr uint8_t PIN_BUZZER    = 15;   // active buzzer; strapping pin, chirps at boot (expected)

// GPIO2 (built-in LED, strapping pin) and GPIO12 stay unused.
