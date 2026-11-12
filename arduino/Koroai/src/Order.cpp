#include "Order.h"
#include <algorithm>

namespace koroai {
static bool digit(char c) { return c >= '0' && c <= '9'; }
static bool alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool validJan(const std::string& jan) {
  if (jan.size() != 8 && jan.size() != 13) return false;
  unsigned sum = 0;
  for (size_t i = 0; i < jan.size(); ++i) {
    if (!digit(jan[i])) return false;
    sum += (jan[i] - '0') * ((jan.size() - 1 - i) % 2 ? 3 : 1);
  }
  return sum % 10 == 0;
}
bool validKey(const std::string& key) {
  if (key.size() != 51 || (key.compare(0, 8, "kr_test_") != 0 &&
                            key.compare(0, 8, "kr_live_") != 0)) return false;
  return std::all_of(key.begin() + 8, key.end(), [](char c) {
    return alpha(c) || digit(c) || c == '_' || c == '-';
  });
}
bool validIdempotencyKey(const std::string& key) {
  return !key.empty() && key.size() <= 128 &&
         std::all_of(key.begin(), key.end(), [](char c) {
           return alpha(c) || digit(c) || c == '.' || c == '_' || c == ':' || c == '-';
         });
}
static bool validInstallation(const std::string& id) {
  return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) {
    return digit(c) || (c >= 'a' && c <= 'f');
  });
}
bool Order::begin(const std::string& scope, const std::string& newInstallationId) {
  if (state_ != State::Uninitialized || scope.size() != 64 ||
      !std::all_of(scope.begin(), scope.end(), [](char c) {
        return digit(c) || (c >= 'a' && c <= 'f');
      })) return false;
  scope_ = scope;
  const auto loaded = store_.load(journal_);
  if (loaded == Load::Error) { state_ = State::StorageError; return false; }
  if (loaded == Load::Empty) {
    if (!validInstallation(newInstallationId)) return false;
    journal_.installationId = newInstallationId;
    if (!store_.save(journal_)) { state_ = State::StorageError; return false; }
  }
  if (!validInstallation(journal_.installationId)) { state_ = State::Blocked; return false; }
  if (!journal_.hasPending) { state_ = State::Idle; return true; }
  const auto& pending_ = journal_.pending;
  if (pending_.scope != scope || !validJan(pending_.jan) ||
      pending_.qty < 1 || pending_.qty > 10 || !validIdempotencyKey(pending_.key) || journal_.sequence == 0 ||
      pending_.key != "dash-" + journal_.installationId + "-" + std::to_string(journal_.sequence)) {
    state_ = State::Blocked;
    return false;
  }
  state_ = journal_.blocked ? State::Blocked : State::Pending;
  return !journal_.blocked;
}
bool Order::start(const std::string& jan, unsigned qty) {
  if (state_ != State::Idle || !validJan(jan) || qty < 1 || qty > 10 ||
      journal_.sequence == UINT64_MAX) return false;
  ++journal_.sequence;
  journal_.hasPending = true;
  journal_.pending.scope = scope_;
  journal_.pending.key = "dash-" + journal_.installationId + "-" + std::to_string(journal_.sequence);
  journal_.pending.jan = jan;
  journal_.pending.qty = qty;
  journal_.retryAt = 0;
  // A failed write can have an unknown durable outcome. Stop until reboot.
  if (!store_.save(journal_)) { state_ = State::StorageError; return false; }
  state_ = State::Pending;
  return true;
}
bool Order::attempt(uint64_t nowSeconds) {
  if (state_ != State::Pending || nowSeconds < journal_.retryAt) return false;
  // Persist the retry fence before sending; a reset must not bypass backoff.
  journal_.retryAt = nowSeconds + 60;
  if (!store_.save(journal_)) { state_ = State::StorageError; return false; }
  reply_ = transport_.send(journal_.pending);
  switch (reply_.outcome) {
    case Outcome::Accepted: state_ = State::Accepted; break;
    case Outcome::Rejected: journal_.blocked = true; state_ = State::Blocked; break;
    case Outcome::Retry:
    case Outcome::InvalidResponse:
      journal_.retryAt = std::max(nowSeconds, reply_.completedAtSeconds) + std::max<uint32_t>(60, reply_.retryAfterSeconds);
      break;
  }
  if (!store_.save(journal_)) state_ = State::StorageError;
  return true;
}
bool Order::acknowledge() {
  if (state_ != State::Accepted) return false;
  journal_.hasPending = false;
  journal_.pending = {};
  if (!store_.save(journal_)) { state_ = State::StorageError; return false; }
  state_ = State::Idle;
  return true;
}
}  // namespace koroai
