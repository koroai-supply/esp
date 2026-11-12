#pragma once
#include <cstdint>
#include <string>

namespace koroai {
// Inline so the header-only JSON codec can check a quick order's returned JAN.
inline bool validJan(const std::string& jan) {
  if (jan.size() != 8 && jan.size() != 13) return false;
  unsigned sum = 0;
  for (size_t i = 0; i < jan.size(); ++i) {
    if (jan[i] < '0' || jan[i] > '9') return false;
    sum += (jan[i] - '0') * ((jan.size() - 1 - i) % 2 ? 3 : 1);
  }
  return sum % 10 == 0;
}
bool validKey(const std::string& key);
bool validIdempotencyKey(const std::string& key);

struct Pending {
  std::string scope;  // SHA-256 of the credential, never the credential itself.
  std::string key;
  std::string jan;    // Empty for a quick order: the server picks the product.
  unsigned qty = 1;   // Always 1 for a quick order.
  bool quick = false; // POST /v1/quick-order instead of /v1/orders.
};
bool validPending(const Pending& pending);
struct Journal {
  std::string installationId;
  uint64_t sequence = 0;
  bool hasPending = false;
  bool blocked = false;
  uint64_t retryAt = 0;
  uint32_t retryDelaySeconds = 60;
  Pending pending;
};
enum class Load { Empty, Found, Error };
class Store {
 public:
  virtual ~Store() = default;
  virtual Load load(Journal& journal) = 0;
  virtual bool save(const Journal& journal) = 0;
};
// NotSet: quick_order_not_set (422). No order was created; the event is dropped.
enum class Outcome { Accepted, Retry, Rejected, InvalidResponse, NotSet };
struct Reply {
  Outcome outcome = Outcome::Retry;
  int httpStatus = 0;
  uint32_t retryAfterSeconds = 60;
  uint64_t completedAtSeconds = 0;
  std::string orderId;
  std::string jan;  // JAN of the accepted order, including quick orders.
};
class Transport {
 public:
  virtual ~Transport() = default;
  virtual Reply send(const Pending& pending) = 0;
};
enum class State { Uninitialized, Idle, Pending, Accepted, Blocked, StorageError };
class Order {
 public:
  Order(Store& store, Transport& transport) : store_(store), transport_(transport) {}
  bool begin(const std::string& scope, const std::string& newInstallationId);
  bool start(const std::string& jan, unsigned qty = 1);
  // One package of the home's quick-order product, chosen by the server.
  bool startQuick();
  bool attempt(uint64_t nowSeconds);
  bool acknowledge();
  State state() const { return state_; }
  const Reply& reply() const { return reply_; }
  const Pending& pending() const { return journal_.pending; }
 private:
  Store& store_;
  Transport& transport_;
  State state_ = State::Uninitialized;
  std::string scope_;
  bool startPending(const Pending& pending);
  Journal journal_;
  Reply reply_;
};
}  // namespace koroai
