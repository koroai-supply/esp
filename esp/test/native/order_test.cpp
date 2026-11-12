#include "Order.h"
#include "Button.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace koroai;
const std::string scope(64, 'a'), installation(32, 'b');
const std::string jan = "4901234567894";
struct Memory : Store {
  Load result = Load::Empty;
  Journal disk;
  bool fail = false;
  Load load(Journal& value) override { value = disk; return result; }
  bool save(const Journal& value) override {
    if (fail) return false;
    disk = value;
    result = Load::Found;
    return true;
  }
};
struct Server : Transport {
  Memory& memory;
  Reply response;
  std::vector<Pending> requests;
  explicit Server(Memory& m) : memory(m) {}
  Reply send(const Pending& p) override {
    assert(memory.disk.hasPending);
    assert(memory.disk.pending.key == p.key);
    assert(memory.disk.pending.jan == p.jan);
    assert(memory.disk.pending.qty == p.qty);
    requests.push_back(p);
    return response;
  }
};
void validation() {
  assert(validJan(jan));
  assert(validJan("96385074"));
  assert(validJan("01234565"));
  assert(!validJan("4901234567893"));
  assert(!validJan("4901234567894\r\n"));
  assert(!validJan("1234x678"));
  assert(!validJan(""));
  assert(validKey("kr_test_" + std::string(43, 'a')));
  assert(validKey("kr_live_" + std::string(43, '_')));
  assert(!validKey("kr_test_" + std::string(42, 'a')));
  assert(!validKey("kr_test_" + std::string(42, 'a') + "\n"));
  assert(!validIdempotencyKey("key\r\nInjected: true"));
}
void logicalIdentityAndRestart() {
  Memory m; Server t(m); Order first(m,t);
  assert(first.begin(scope, installation));
  assert(!first.start("invalid"));
  assert(!first.start(jan,0));
  assert(!first.start(jan,11));
  assert(first.start(jan));
  auto key = first.pending().key;
  assert(key == "dash-" + installation + "-1");
  assert(!first.start(jan));
  assert(first.attempt(100));  // lost response, retained
  assert(!first.attempt(159));
  Order reboot(m,t);
  assert(reboot.begin(scope, std::string(32,'c')));
  assert(reboot.pending().key == key);
  assert(!reboot.attempt(159));  // persisted backoff
  t.response.outcome = Outcome::Accepted;
  t.response.httpStatus = 201;
  assert(reboot.attempt(160));
  assert(!reboot.start(jan));
  Order beforeAck(m,t);
  assert(beforeAck.begin(scope, installation));
  assert(beforeAck.pending().key == key);
  assert(beforeAck.attempt(220));  // restart after accepted response: replay
  assert(beforeAck.acknowledge());
  Order afterAck(m,t);
  assert(afterAck.begin(scope, installation));
  assert(afterAck.state() == State::Idle);
  assert(afterAck.start(jan));
  assert(afterAck.pending().key == "dash-" + installation + "-2");
  assert(t.requests.size() == 3);
  for (const auto& p : t.requests) assert(p.key == key && p.jan == jan && p.qty == 1);
}
void storageFailures() {
  Memory m; Server t(m); Order o(m,t);
  assert(o.begin(scope,installation));
  m.fail = true;
  assert(!o.start(jan));
  assert(!o.attempt(100));
  assert(o.state() == State::StorageError && t.requests.empty());
  Memory n; Server s(n); Order p(n,s);
  assert(p.begin(scope,installation) && p.start(jan));
  n.fail = true;
  assert(!p.attempt(100));  // fence cannot be stored: never send
  assert(s.requests.empty());
  n.fail = false;
  Order recovered(n,s);
  assert(recovered.begin(scope,installation));
  s.response.outcome = Outcome::Accepted;
  assert(recovered.attempt(100));
  n.fail = true;
  assert(!recovered.acknowledge());
  assert(recovered.state() == State::StorageError);
  n.fail = false;
  Order again(n,s);
  assert(again.begin(scope,installation));
  assert(again.pending().key == s.requests[0].key);
}
void scopeAndCorruption() {
  Memory m; Server t(m); Order o(m,t);
  assert(o.begin(scope,installation) && o.start(jan));
  Order rotated(m,t);
  assert(!rotated.begin(std::string(64,'c'),installation));
  assert(!rotated.attempt(100));
  m.disk.pending.key = "different-event";
  Order corrupt(m,t);
  assert(!corrupt.begin(scope,installation));
  m.result = Load::Error;
  Order unreadable(m,t);
  assert(!unreadable.begin(scope,installation));
  assert(unreadable.state() == State::StorageError);
}
void retriesAndRejection() {
  Memory m; Server t(m); Order o(m,t);
  assert(o.begin(scope,installation) && o.start(jan,10));
  t.response.httpStatus = 429;
  t.response.retryAfterSeconds = 3600;
  assert(o.attempt(100));
  assert(!o.attempt(3699));
  Order restart(m,t);
  assert(restart.begin(scope,installation));
  assert(!restart.attempt(3699));
  t.response.outcome = Outcome::InvalidResponse;
  t.response.retryAfterSeconds = 60;
  assert(restart.attempt(3700));
  assert(!restart.acknowledge());
  t.response.outcome = Outcome::Rejected;
  t.response.httpStatus = 409;
  assert(restart.attempt(3760));
  assert(restart.state() == State::Blocked);
  assert(!restart.start(jan));
  Order rejected(m,t);
  assert(!rejected.begin(scope,installation));
  assert(!rejected.attempt(5000));
  assert(m.disk.hasPending);
}
void buttonEdges() {
  Button held;
  assert(!held.pressed(true, 0));
  assert(!held.pressed(true, 5000));
  assert(!held.pressed(false, 5010));
  assert(!held.pressed(false, 5060));
  assert(!held.pressed(true, 5100));
  assert(!held.pressed(false, 5120));  // bounce
  assert(!held.pressed(true, 5130));
  assert(!held.pressed(true, 5179));
  assert(held.pressed(true, 5180));
  assert(!held.pressed(true, 10000));  // long hold
  assert(!held.pressed(false, 10010));
  assert(!held.pressed(false, 10060));
  assert(!held.pressed(true, 10100));
  assert(held.pressed(true, 10150));
  Button wrap;
  assert(!wrap.pressed(false, UINT32_MAX - 100));
  assert(!wrap.pressed(true, UINT32_MAX - 20));
  assert(wrap.pressed(true, 30));
}
int main() {
  buttonEdges();
  validation(); logicalIdentityAndRestart(); storageFailures();
  scopeAndCorruption(); retriesAndRejection();
  std::cout << "注文の冪等性・復旧・保存失敗・再送制限のテスト成功\n";
}
