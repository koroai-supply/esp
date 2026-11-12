#include "Koroai.h"
#include "../../../../arduino/Koroai/src/JournalJson.h"
#include "../../../../arduino/Koroai/src/ResponseJson.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <ctime>
#include <climits>

namespace koroai {
Load NvsStore::load(Journal& journal) {
  if (opened_ || nvs_open(name_, NVS_READWRITE, &handle_) != ESP_OK) return Load::Error;
  opened_ = true;
  size_t length = 0;
  esp_err_t result = nvs_get_str(handle_, "journal", nullptr, &length);
  if (result == ESP_ERR_NVS_NOT_FOUND) return Load::Empty;
  if (result != ESP_OK || length < 2 || length > 1024) return Load::Error;
  char value[1024];
  if (nvs_get_str(handle_, "journal", value, &length) != ESP_OK) return Load::Error;
  return decodeJournal(value, journal) ? Load::Found : Load::Error;
}
bool NvsStore::save(const Journal& journal) {
  std::string value = encodeJournal(journal);
  return opened_ && !value.empty() &&
         nvs_set_str(handle_, "journal", value.c_str()) == ESP_OK && nvs_commit(handle_) == ESP_OK;
}
std::string credentialScope(const char* apiKey) {
  unsigned char digest[32];
  if (!apiKey || mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(apiKey),
                                  strlen(apiKey), digest, 0) != 0) return {};
  char hex[65];
  for (unsigned i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
  return hex;
}
std::string newInstallationId() {
  unsigned char random[16];
  esp_fill_random(random, sizeof(random));
  char hex[33];
  for (unsigned i = 0; i < sizeof(random); ++i) snprintf(hex + i * 2, 3, "%02x", random[i]);
  return hex;
}
// HTTPClient decodes chunking before writing to this bounded sink.
class ResponseBody : public Stream {
 public:
  String body;
  bool overflow = false;
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* data, size_t length) override {
    if (length > 4096 - body.length()) { overflow = true; return 0; }
    return body.concat(reinterpret_cast<const char*>(data), length) ? length : 0;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};
static uint32_t retrySeconds(const String& value) {
  if (value.isEmpty()) return 60;
  uint64_t seconds = 0;
  for (unsigned i = 0; i < value.length(); ++i) {
    if (value[i] < '0' || value[i] > '9') return UINT32_MAX;
    seconds = seconds * 10 + (value[i] - '0');
    if (seconds >= UINT32_MAX) return UINT32_MAX;
  }
  return static_cast<uint32_t>(seconds);
}
Reply HttpsTransport::send(const Pending& pending) {
  Reply reply;
  if (!apiKey_ || !validKey(apiKey_) || !rootCa_ || !*rootCa_ ||
      pending.scope != credentialScope(apiKey_) || !validJan(pending.jan) ||
      pending.qty < 1 || pending.qty > 10 || !validIdempotencyKey(pending.key)) {
    reply.outcome = Outcome::Rejected;
    return reply;
  }
  // Do not attempt certificate validation until the clock is synchronized.
  if (time(nullptr) < 1735689600) return reply;
  WiFiClientSecure tls;
  tls.setCACert(rootCa_);
  tls.setHandshakeTimeout(15);
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(tls, "https://api.koroai.supply/v1/orders")) return reply;
  const char* headers[] = {"Retry-After", "Content-Type"};
  http.collectHeaders(headers, 2);
  http.addHeader("Authorization", String("Bearer ") + apiKey_);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Idempotency-Key", pending.key.c_str());
  String payload = String("{\"jan\":\"") + pending.jan.c_str() + "\",\"qty\":" + pending.qty + "}";
  reply.httpStatus = http.POST(payload);
  if (reply.httpStatus == 429) reply.retryAfterSeconds = retrySeconds(http.header("Retry-After"));
  if (reply.httpStatus == 201) {
    ResponseBody response;
    if (http.writeToStream(&response) >= 0 && !response.overflow &&
        http.header("Content-Type").startsWith("application/json")) {
      reply = parseAccepted(response.body.c_str(), pending, strncmp(apiKey_, "kr_test_", 8) == 0);
    } else {
      reply.outcome = Outcome::InvalidResponse;
    }
  } else if (reply.httpStatus > 0 && reply.httpStatus < 500 &&
             reply.httpStatus != 408 && reply.httpStatus != 429) {
    // Redirects, authentication errors, conflicts and invalid purchases need intervention.
    reply.outcome = Outcome::Rejected;
  }
  http.end();
  return reply;
}
}  // namespace koroai
