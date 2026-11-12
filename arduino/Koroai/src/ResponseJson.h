#pragma once
#include "Order.h"
#include <ArduinoJson.h>
#include <cstring>
namespace koroai {
inline bool validUuid(const char* value) {
  if (!value || strlen(value) != 36) return false;
  for (unsigned i = 0; i < 36; ++i) {
    char c = value[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; }
    else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}
inline Reply parseAccepted(const char* body, const Pending& pending, bool sandbox) {
  Reply reply;
  reply.httpStatus = 201;
  reply.outcome = Outcome::InvalidResponse;
  JsonDocument doc;
  if (!body || strlen(body) > 4096 || deserializeJson(doc, body)) return reply;
  JsonVariant data = doc["data"];
  if (!data["id"].is<const char*>() || !validUuid(data["id"].as<const char*>()) ||
      !(data["mode"] == (sandbox ? "sandbox" : "live")) || !(data["jan"] == pending.jan) ||
      !data["qty"].is<unsigned>() || data["qty"].as<unsigned>() != pending.qty ||
      !(data["status"] == (sandbox ? "simulated" : "accepted") || data["status"] == "canceled")) return reply;
  reply.outcome = Outcome::Accepted;
  reply.orderId = data["id"].as<std::string>();
  return reply;
}
}  // namespace koroai
