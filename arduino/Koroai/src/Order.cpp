#include "Order.h"
#include <algorithm>

namespace koroai {
static bool digit(char c) { return c >= '0' && c <= '9'; }
static bool alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
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
bool validPending(const Pending& pending) {
  if (pending.quick) return pending.jan.empty() && pending.qty == 1;
  return validJan(pending.jan) && pending.qty >= 1 && pending.qty <= 10;
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
  if (pending_.scope != scope || !validPending(pending_) ||
      !validIdempotencyKey(pending_.key) || journal_.sequence == 0 ||
      pending_.key != "dash-" + journal_.installationId + "-" + std::to_string(journal_.sequence)) {
    state_ = State::Blocked;
    return false;
  }
  state_ = journal_.blocked ? State::Blocked : State::Pending;
  return !journal_.blocked;
}
bool Order::start(const std::string& jan, unsigned qty) {
  Pending pending;
  pending.jan = jan;
  pending.qty = qty;
  return startPending(pending);
}
bool Order::startQuick() {
  Pending pending;
  pending.quick = true;
  return startPending(pending);
}
bool Order::startPending(const Pending& pending) {
  if (state_ != State::Idle || !validPending(pending) ||
      journal_.sequence == UINT64_MAX) return false;
  ++journal_.sequence;
  journal_.hasPending = true;
  journal_.pending = pending;
  journal_.pending.scope = scope_;
  journal_.pending.key = "dash-" + journal_.installationId + "-" + std::to_string(journal_.sequence);
  journal_.retryAt = 0;
  journal_.retryDelaySeconds = 60;
  // A failed write can have an unknown durable outcome. Stop until reboot.
  if (!store_.save(journal_)) { state_ = State::StorageError; return false; }
  state_ = State::Pending;
  return true;
}
bool Order::attempt(uint64_t nowSeconds) {
  if (state_ != State::Pending || nowSeconds < journal_.retryAt) return false;
  // Persist the retry fence before sending; a reset must not bypass backoff.
  journal_.retryAt = nowSeconds + journal_.retryDelaySeconds;
  if (!store_.save(journal_)) { state_ = State::StorageError; return false; }
  reply_ = transport_.send(journal_.pending);
  // quick_order_not_set only means "no order" for a quick order; anything else stops.
  if (reply_.outcome == Outcome::NotSet && !journal_.pending.quick) reply_.outcome = Outcome::Rejected;
  switch (reply_.outcome) {
    case Outcome::Accepted: state_ = State::Accepted; break;
    case Outcome::Rejected: journal_.blocked = true; state_ = State::Blocked; break;
    case Outcome::NotSet:
      // The server created no order. Drop this event; the sequence is not reused.
      journal_.hasPending = false;
      journal_.pending = {};
      journal_.retryAt = 0;
      journal_.retryDelaySeconds = 60;
      state_ = State::Idle;
      break;
    case Outcome::Retry:
    case Outcome::InvalidResponse:
      journal_.retryAt = std::max(nowSeconds, reply_.completedAtSeconds) + std::max(journal_.retryDelaySeconds, reply_.retryAfterSeconds);
      journal_.retryDelaySeconds = std::min<uint32_t>(3600, journal_.retryDelaySeconds * 2);
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
