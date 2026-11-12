#pragma once
#include <Arduino.h>
#include <nvs.h>
#include "Order.h"

namespace koroai {
// Single-threaded SDK. Call only from the Arduino loop task.
class NvsStore : public Store {
 public:
  explicit NvsStore(const char* name = "koroai-dash") : name_(name) {}
  ~NvsStore() override { if (opened_) nvs_close(handle_); }
  NvsStore(const NvsStore&) = delete;
  NvsStore& operator=(const NvsStore&) = delete;
  Load load(Journal& journal) override;
  bool save(const Journal& journal) override;
 private:
  const char* name_;
  nvs_handle_t handle_ = 0;
  bool opened_ = false;
};
class HttpsTransport : public Transport {
 public:
  HttpsTransport(const char* apiKey, const char* rootCa)
      : apiKey_(apiKey), rootCa_(rootCa) {}
  Reply send(const Pending& pending) override;
 private:
  const char* apiKey_;
  const char* rootCa_;
};
std::string credentialScope(const char* apiKey);
std::string newInstallationId();
}  // namespace koroai
