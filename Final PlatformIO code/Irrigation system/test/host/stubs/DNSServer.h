#pragma once
#include <Arduino.h>
enum class DNSReplyCode { NoError };
class DNSServer { public: void setErrorReplyCode(DNSReplyCode) {} void start(int, const char*, const IPAddress&) {} void processNextRequest() {} void stop() {} };
