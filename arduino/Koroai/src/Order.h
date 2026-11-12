#pragma once
#include <cstdint>
#include <string>

namespace koroai {
bool validJan(const std::string& jan);
bool validKey(const std::string& key);
bool validIdempotencyKey(const std::string& key);

struct Pending {
  std::string scope;  // SHA-256 of the credential, never the credential itself.
  std::string key;
  std::string jan;
  unsigned qty = 1;
};
struct Journal {
  std::string installationId;
  uint64_t sequence = 0;
  bool hasPending = false;
  bool blocked = false;
  uint64_t retryAt = 0;
  Pending pending;
};
enum class Load { Empty, Found, Error };
class Store {
 public:
  virtual ~Store() = default;
  virtual Load load(Journal& journal) = 0;
  virtual bool save(const Journal& journal) = 0;
};
enum class Outcome { Accepted, Retry, Rejected, InvalidResponse };
struct Reply {
  Outcome outcome = Outcome::Retry;
  int httpStatus = 0;
  uint32_t retryAfterSeconds = 60;
  uint64_t completedAtSeconds = 0;
  std::string orderId;
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
  Journal journal_;
  Reply reply_;
};
}  // namespace koroai
