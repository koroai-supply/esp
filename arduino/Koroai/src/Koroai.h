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
  uint8_t read(unsigned address) override {
    uint8_t value=0;
    if (DataFlashBlockDevice::getInstance().read(&value,address,1) != 0) failed_=true;
    return value;
  }
  bool write(unsigned address, uint8_t value) override { return writeBlock(address,&value,1); }
  bool writeBlock(unsigned address, const uint8_t* data, unsigned size) override {
    if (failed_ || address+size > static_cast<unsigned>(EEPROM.length())) return false;
    while (size) {
      uint32_t available=0;
      auto& flash=veeprom::getInstance();
      if (flash.read_block(address,available) != ReadStatus::ALLOCATED) return false;
      unsigned count=size < available ? size : available;
      for (unsigned i=0;i<count;++i) flash.write_byte(data[i],address+i);
      if (!flash.write()) return false;
      for (unsigned i=0;i<count;++i) if (read(address+i) != data[i] || failed_) return false;
      address+=count; data+=count; size-=count;
    }
    return true;
  }
 private:
  bool failed_=false;
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
