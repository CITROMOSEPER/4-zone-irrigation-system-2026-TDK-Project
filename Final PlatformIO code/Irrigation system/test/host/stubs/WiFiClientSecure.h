#pragma once
#include <Arduino.h>
class WiFiClientSecure : public Stream { public: void setInsecure() {} size_t write(uint8_t) override { return 1; } };
