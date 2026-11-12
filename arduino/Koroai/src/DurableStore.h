#pragma once
#include "JournalJson.h"
#include <cstring>

namespace koroai {
class Bytes {
 public:
  virtual ~Bytes() = default;
  virtual uint8_t read(unsigned address) = 0;
  virtual bool write(unsigned address, uint8_t value) = 0;
  virtual bool writeBlock(unsigned address, const uint8_t* data, unsigned size) {
    for (unsigned i = 0; i < size; ++i) {
      if (!write(address+i,data[i]) || read(address+i) != data[i]) return false;
    }
    return true;
  }
};
// Two slots must occupy separate erase units. Marker is written last.
class DurableStore : public Store {
 public:
  explicit DurableStore(Bytes& bytes, unsigned stride = 4096) : bytes_(bytes), stride_(stride) {}
  Load load(Journal& journal) override {
    bool found = false, untouched = true;
    for (int slot = 0; slot < 2; ++slot) {
      unsigned base = slot * stride_;
      uint8_t marker = bytes_.read(base);
      if (marker == 0xff) continue;
      untouched = false;
      if (marker == 0) continue;  // interrupted write, never sent from this record
      if (marker != 0xa5) return Load::Error;
      uint8_t record[1024] = {};
      for (unsigned i = 1; i < 15; ++i) record[i] = bytes_.read(base + i);
      uint64_t generation = 0;
      for (unsigned i = 0; i < 8; ++i) generation |= uint64_t(record[1+i]) << (8*i);
      unsigned length = record[9] | (unsigned(record[10]) << 8);
      if (length == 0 || length > 1008 || generation == 0) return Load::Error;
      for (unsigned i = 0; i < length; ++i) record[15+i] = bytes_.read(base+15+i);
      uint32_t expected = 0;
      for (unsigned i = 0; i < 4; ++i) expected |= uint32_t(record[11+i]) << (8*i);
      if (crc(record, length) != expected) return Load::Error;
      Journal decoded;
      if (!decodeJournal(reinterpret_cast<char*>(record+15), decoded)) return Load::Error;
      if (!found || generation > generation_) {
        found = true; generation_ = generation; active_ = slot; journal = decoded;
      }
    }
    return found ? Load::Found : (untouched ? Load::Empty : Load::Error);
  }
  bool save(const Journal& journal) override {
    if (generation_ == UINT64_MAX) return false;
    std::string value = encodeJournal(journal);
    if (value.empty() || value.size() > 1008) return false;
    unsigned slot = active_ == 0 ? 1 : 0, base = slot * stride_;
    uint8_t record[1024] = {};
    uint64_t next = generation_ + 1;
    for (unsigned i = 0; i < 8; ++i) record[1+i] = next >> (8*i);
    record[9] = value.size() & 255; record[10] = value.size() >> 8;
    memcpy(record+15, value.data(), value.size());
    uint32_t checksum = crc(record, value.size());
    for (unsigned i = 0; i < 4; ++i) record[11+i] = checksum >> (8*i);
    if (!bytes_.write(base, 0) || bytes_.read(base) != 0) return false;
    if (!bytes_.writeBlock(base+1, record+1, 14+value.size())) return false;
    for (unsigned i = 1; i < 15+value.size(); ++i) {
      if (bytes_.read(base+i) != record[i]) return false;
    }
    if (!bytes_.write(base, 0xa5) || bytes_.read(base) != 0xa5) return false;
    active_ = slot; generation_ = next;
    return true;
  }
 private:
  static uint32_t crc(const uint8_t* record, unsigned length) {
    uint32_t sum = 0xffffffff;
    for (unsigned i = 1; i < 15 + length; ++i) {
      if (i >= 11 && i < 15) continue;
      sum ^= record[i];
      for (unsigned bit = 0; bit < 8; ++bit) sum = (sum >> 1) ^ (0xedb88320u & (0u - (sum & 1)));
    }
    return ~sum;
  }
  Bytes& bytes_;
  unsigned stride_;
  int active_ = -1;
  uint64_t generation_ = 0;
};
}  // namespace koroai
