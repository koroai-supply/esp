#pragma once
#include "Order.h"
#include <ArduinoJson.h>
namespace koroai {
inline bool decodeJournal(const char* value, Journal& journal) {
  JsonDocument doc;
  if (deserializeJson(doc, value) || !doc["version"].is<unsigned>() ||
      doc["version"].as<unsigned>() != 1 || !doc["installation"].is<const char*>() ||
      !doc["sequence"].is<uint64_t>() || !doc["active"].is<bool>() ||
      !doc["blocked"].is<bool>() || !doc["retry_at"].is<uint64_t>()) return false;
  journal.installationId = doc["installation"].as<std::string>();
  journal.sequence = doc["sequence"].as<uint64_t>();
  journal.hasPending = doc["active"].as<bool>();
  journal.blocked = doc["blocked"].as<bool>();
  journal.retryAt = doc["retry_at"].as<uint64_t>();
  if (journal.hasPending) {
    if (!doc["scope"].is<const char*>() || !doc["key"].is<const char*>() ||
        !doc["jan"].is<const char*>() || !doc["qty"].is<unsigned>()) return false;
    journal.pending.scope = doc["scope"].as<std::string>();
    journal.pending.key = doc["key"].as<std::string>();
    journal.pending.jan = doc["jan"].as<std::string>();
    journal.pending.qty = doc["qty"].as<unsigned>();
  }
  return true;
}
inline std::string encodeJournal(const Journal& journal) {
  JsonDocument doc;
  doc["version"] = 1;
  doc["installation"] = journal.installationId;
  doc["sequence"] = journal.sequence;
  doc["active"] = journal.hasPending;
  doc["blocked"] = journal.blocked;
  doc["retry_at"] = journal.retryAt;
  if (journal.hasPending) {
    doc["scope"] = journal.pending.scope;
    doc["key"] = journal.pending.key;
    doc["jan"] = journal.pending.jan;
    doc["qty"] = journal.pending.qty;
  }
  std::string value;
  if (doc.overflowed() || serializeJson(doc, value) == 0 || value.size() >= 1024) return {};
  return value;
}
}  // namespace koroai
