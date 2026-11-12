#include "Koroai.h"
#include "ResponseJson.h"
#include <ArduinoHttpClient.h>
#include <SHA256.h>

namespace koroai {
std::string credentialScope(const char* value) {
  if (!value) return {};
  SHA256 hash;
  hash.update(value, strlen(value));
  uint8_t digest[32];
  hash.finalize(digest, sizeof(digest));
  char hex[65];
  for (unsigned i = 0; i < 32; ++i) snprintf(hex+2*i, 3, "%02x", digest[i]);
  return hex;
}
Reply HttpsTransport::send(const Pending& pending) {
  Reply reply;
  if (!apiKey_ || !validKey(apiKey_) || pending.scope != credentialScope(apiKey_) ||
      !validJan(pending.jan) || pending.qty < 1 || pending.qty > 10 ||
      !validIdempotencyKey(pending.key)) {
    reply.outcome = Outcome::Rejected;
    return reply;
  }
  WiFiSSLClient tls;  // UNO R4 WiFi bridge firmware's verified CA bundle.
  HttpClient http(tls, "api.koroai.supply", 443);
  http.setHttpResponseTimeout(15000);
  http.setTimeout(15000);
  std::string payload = "{\"jan\":\"" + pending.jan + "\",\"qty\":" + std::to_string(pending.qty) + "}";
  http.beginRequest();
  http.post("/v1/orders");
  http.sendHeader("Authorization", (std::string("Bearer ") + apiKey_).c_str());
  http.sendHeader("Content-Type", "application/json");
  http.sendHeader("Idempotency-Key", pending.key.c_str());
  http.sendHeader("Content-Length", payload.size());
  http.sendHeader("Connection", "close");
  http.beginBody();
  http.print(payload.c_str());
  http.endRequest();
  reply.httpStatus = http.responseStatusCode();
  String contentType, retryAfter;
  while (http.headerAvailable()) {
    String name = http.readHeaderName();
    String value = http.readHeaderValue();
    if (name.equalsIgnoreCase("Content-Type")) contentType = value;
    if (name.equalsIgnoreCase("Retry-After")) retryAfter = value;
  }
  if (reply.httpStatus == 429 && retryAfter.length()) {
    uint64_t seconds = 0;
    for (unsigned i = 0; i < retryAfter.length(); ++i) {
      if (retryAfter[i] < '0' || retryAfter[i] > '9') { seconds = UINT32_MAX; break; }
      seconds = seconds * 10 + retryAfter[i] - '0';
      if (seconds >= UINT32_MAX) { seconds = UINT32_MAX; break; }
    }
    reply.retryAfterSeconds = seconds;
  } else if (reply.httpStatus == 201) {
    char body[4098] = {};
    size_t count = http.readBytes(body, 4097);
    if (count <= 4096 && contentType.startsWith("application/json") && http.endOfBodyReached()) {
      reply = parseAccepted(body, pending, strncmp(apiKey_, "kr_test_", 8) == 0);
    } else reply.outcome = Outcome::InvalidResponse;
  } else if (reply.httpStatus > 0 && reply.httpStatus < 500 &&
             reply.httpStatus != 408 && reply.httpStatus != 429) {
    reply.outcome = Outcome::Rejected;
  }
  http.stop();
  reply.completedAtSeconds=WiFi.getTime();
  return reply;
}
}  // namespace koroai
