#include "DurableStore.h"
#include "ResponseJson.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace koroai;
struct MemoryBytes : Bytes {
  std::vector<uint8_t> data = std::vector<uint8_t>(8192,0xff);
  int remaining = 100000;
  uint8_t read(unsigned address) override { return data.at(address); }
  bool write(unsigned address, uint8_t value) override {
    if (remaining-- <= 0) return false;
    data.at(address)=value;
    return true;
  }
};
int main() {
  Journal journal;
  journal.installationId=std::string(32,'a');
  journal.sequence=1;
  journal.hasPending=true;
  journal.pending.scope=std::string(64,'b');
  journal.pending.key="dash-"+journal.installationId+"-1";
  journal.pending.jan="4901234567894";
  auto encoded=encodeJournal(journal);
  Journal decoded;
  assert(decodeJournal(encoded.c_str(),decoded));
  assert(decoded.pending.key==journal.pending.key);
  assert(!decodeJournal("{}",decoded));
  assert(!decodeJournal("{broken",decoded));
  MemoryBytes baseline;
  DurableStore first(baseline);
  assert(first.load(decoded)==Load::Empty);
  assert(first.save(journal));
  Journal cleared=journal;
  cleared.hasPending=false;
  cleared.pending={};
  // Every interrupted write of an acknowledgement must recover the original
  // pending event, or the fully committed acknowledgement, never a fresh ID.
  for (int cutoff=0;cutoff<600;++cutoff) {
    MemoryBytes bytes=baseline;
    DurableStore store(bytes);
    assert(store.load(decoded)==Load::Found);
    bytes.remaining=cutoff;
    bool success=store.save(cleared);
    DurableStore reboot(bytes);
    assert(reboot.load(decoded)==Load::Found);
    assert(decoded.installationId==journal.installationId && decoded.sequence==1);
    if (!success) assert(decoded.hasPending && decoded.pending.key==journal.pending.key);
    else assert(!decoded.hasPending);
  }
  MemoryBytes broken=baseline;
  broken.data[20]^=1;
  DurableStore bad(broken);
  assert(bad.load(decoded)==Load::Error);
  const char* accepted=R"({"data":{"id":"11111111-1111-4111-8111-111111111111","mode":"sandbox","jan":"4901234567894","qty":1,"status":"simulated"}})";
  assert(parseAccepted(accepted,journal.pending,true).outcome==Outcome::Accepted);
  assert(parseAccepted(accepted,journal.pending,false).outcome==Outcome::InvalidResponse);
  Pending different=journal.pending;
  different.qty=2;
  assert(parseAccepted(accepted,different,true).outcome==Outcome::InvalidResponse);
  assert(parseAccepted("{broken",journal.pending,true).outcome==Outcome::InvalidResponse);
  assert(parseAccepted("{\"data\":{}}",journal.pending,true).outcome==Outcome::InvalidResponse);
  // Quick orders: the journal keeps no JAN, and any valid JAN is accepted in the reply.
  Journal quick=journal;
  quick.pending.quick=true;
  quick.pending.jan.clear();
  auto quickEncoded=encodeJournal(quick);
  assert(quickEncoded.find("\"jan\"")==std::string::npos);
  assert(decodeJournal(quickEncoded.c_str(),decoded));
  assert(decoded.pending.quick && decoded.pending.jan.empty() && decoded.pending.qty==1);
  assert(decodeJournal(encoded.c_str(),decoded) && !decoded.pending.quick);  // pre-quick journals
  const char* quickWithJan=R"({"version":1,"installation":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","sequence":1,"active":true,"blocked":false,"retry_at":0,"retry_delay":60,"scope":"s","key":"k","quick":true,"jan":"4901234567894","qty":1})";
  assert(!decodeJournal(quickWithJan,decoded));
  const char* badQuick=R"({"version":1,"installation":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","sequence":1,"active":true,"blocked":false,"retry_at":0,"retry_delay":60,"scope":"s","key":"k","quick":1,"qty":1})";
  assert(!decodeJournal(badQuick,decoded));
  const char* quickAccepted=R"({"data":{"id":"11111111-1111-4111-8111-111111111111","mode":"sandbox","jan":"96385074","qty":1,"status":"simulated"}})";
  auto quickReply=parseAccepted(quickAccepted,quick.pending,true);
  assert(quickReply.outcome==Outcome::Accepted && quickReply.jan=="96385074");
  assert(parseAccepted(quickAccepted,journal.pending,true).outcome==Outcome::InvalidResponse);
  assert(parseAccepted(R"({"data":{"id":"11111111-1111-4111-8111-111111111111","mode":"sandbox","jan":"96385075","qty":1,"status":"simulated"}})",quick.pending,true).outcome==Outcome::InvalidResponse);
  assert(parseAccepted(R"({"data":{"id":"11111111-1111-4111-8111-111111111111","mode":"sandbox","jan":"96385074","qty":2,"status":"simulated"}})",quick.pending,true).outcome==Outcome::InvalidResponse);
  assert(quickOrderNotSet(R"({"error":"quick_order_not_set"})"));
  assert(!quickOrderNotSet(R"({"error":"product_not_available"})"));
  assert(!quickOrderNotSet("{broken"));
  assert(!quickOrderNotSet(""));
  std::cout << "journal・EEPROM電源断600パターン・成功応答・クイック注文の検証成功\n";
}
