#pragma once
#include "Order.h"
#include <ArduinoJson.h>
namespace koroai {
inline bool decodeJournal(const char* value, Journal& journal) {
  JsonDocument doc;
  if (deserializeJson(doc, value) || !doc["version"].is<unsigned>() ||
      doc["version"].as<unsigned>() != 1 || !doc["installation"].is<const char*>() ||
      !doc["sequence"].is<uint64_t>() || !doc["active"].is<bool>() ||
      !doc["blocked"].is<bool>() || !doc["retry_at"].is<uint64_t>() ||
      !doc["retry_delay"].is<uint32_t>() || doc["retry_delay"].as<uint32_t>() < 60 ||
      doc["retry_delay"].as<uint32_t>() > 3600) return false;
  journal.installationId = doc["installation"].as<std::string>();
  journal.sequence = doc["sequence"].as<uint64_t>();
  journal.hasPending = doc["active"].as<bool>();
  journal.blocked = doc["blocked"].as<bool>();
  journal.retryAt = doc["retry_at"].as<uint64_t>();
  journal.retryDelaySeconds = doc["retry_delay"].as<uint32_t>();
  if (journal.hasPending) {
    // "quick" is absent in journals written before quick orders existed.
    if (!doc["quick"].isNull() && !doc["quick"].is<bool>()) return false;
    const bool quick = doc["quick"] | false;
    if (!doc["scope"].is<const char*>() || !doc["key"].is<const char*>() ||
        (quick ? !doc["jan"].isNull() : !doc["jan"].is<const char*>()) ||
        !doc["qty"].is<unsigned>()) return false;
    journal.pending.scope = doc["scope"].as<std::string>();
    journal.pending.key = doc["key"].as<std::string>();
    journal.pending.quick = quick;
    journal.pending.jan = quick ? std::string() : doc["jan"].as<std::string>();
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
  doc["retry_delay"] = journal.retryDelaySeconds;
  if (journal.hasPending) {
    doc["scope"] = journal.pending.scope;
    doc["key"] = journal.pending.key;
    if (journal.pending.quick) doc["quick"] = true;
    else doc["jan"] = journal.pending.jan;
    doc["qty"] = journal.pending.qty;
  }
  std::string value;
  if (doc.overflowed() || serializeJson(doc, value) == 0 || value.size() >= 1024) return {};
  return value;
}
}  // namespace koroai
