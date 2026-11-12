#pragma once
#include <Arduino.h>
#include <WiFiS3.h>
#include <EEPROM.h>
#include "Order.h"
#include "Button.h"
#include "DurableStore.h"

namespace koroai {
class EepromBytes : public Bytes {
 public:
  uint8_t read(unsigned address) override { return EEPROM.read(address); }
  bool write(unsigned address, uint8_t value) override {
    if (address >= static_cast<unsigned>(EEPROM.length())) return false;
    EEPROM.update(address, value);
    return EEPROM.read(address) == value;
  }
};
class HttpsTransport : public Transport {
 public:
  explicit HttpsTransport(const char* apiKey) : apiKey_(apiKey) {}
  Reply send(const Pending& pending) override;
 private:
  const char* apiKey_;
};
std::string credentialScope(const char* value);
}  // namespace koroai
