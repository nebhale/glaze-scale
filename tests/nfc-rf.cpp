#include "../studio/nfc_rf.h"

#include <cassert>
#include <iostream>
#include <vector>

struct Transport {
  std::vector<std::vector<uint8_t>> replies{
      {1, 0, 0, 0xFF, 0, 0xFF, 0},
      {1, 0, 0, 0xFF, 2, 0xFE, 0xD5, 0x33, 0xF8, 0}};
  std::vector<uint8_t> command;
  uint32_t now = 0, ready_after = 0;
  size_t reads = 0;
  bool write_ok = true, read_ok = true;

  bool write(const uint8_t *data, size_t size) {
    command.assign(data, data + size);
    return write_ok;
  }
  bool read(uint8_t *data, size_t size) {
    ++reads;
    if (!read_ok) return false;
    if (size == 1) { data[0] = now >= ready_after ? 1 : 0; return true; }
    if (replies.empty() || replies.front().size() != size) return false;
    std::copy(replies.front().begin(), replies.front().end(), data);
    replies.erase(replies.begin());
    return true;
  }
  uint32_t now_ms() { return now; }
  void pause_ms(uint32_t ms) { now += ms; }
};

int main() {
  {
    Transport bus;
    bus.ready_after = 3;
    assert(glaze::write_nfc_drive(bus, 7));
    assert(bus.replies.empty() && bus.now == 3);
    assert(bus.command.size() == 21 && bus.command[3] == 14);
    assert(static_cast<uint8_t>(bus.command[3] + bus.command[4]) == 0);
    uint8_t checksum = 0;
    for (size_t i = 5; i < 20; ++i) checksum += bus.command[i];
    assert(checksum == 0);
    // RFConfiguration/106 kbit/s Type A; the P-driver uses the tested value.
    assert(bus.command[5] == 0xD4 && bus.command[6] == 0x32 && bus.command[7] == 0x0A);
    assert(bus.command[8] == 0x59 && bus.command[9] == 0xF4 && bus.command[10] == 7);
  }
  for (int drive : {-1, 0, 64, 256}) {
    Transport bus;
    assert(!glaze::write_nfc_drive(bus, drive));
    assert(bus.command.empty() && bus.reads == 0);
  }
  for (int drive : {1, 63}) {
    Transport bus;
    assert(glaze::write_nfc_drive(bus, drive));
    assert(bus.command[10] == drive);
  }
  {
    Transport bus;
    bus.write_ok = false;
    assert(!glaze::write_nfc_drive(bus, 7) && bus.reads == 0);
  }
  {
    Transport bus;
    bus.read_ok = false;
    assert(!glaze::write_nfc_drive(bus, 7) && bus.reads == 1);
  }
  {
    Transport bus;
    bus.ready_after = 200;
    assert(!glaze::write_nfc_drive(bus, 7) && bus.now == 100);
  }
  for (size_t phase : {0u, 1u}) {
    for (size_t byte = 0; byte < Transport{}.replies[phase].size(); ++byte) {
      Transport bus;
      // Reject a not-ready status or any damaged ACK/response byte.
      bus.replies[phase][byte] ^= 1;
      assert(!glaze::write_nfc_drive(bus, 7));
    }
    Transport bus;
    bus.replies[phase].pop_back();
    assert(!glaze::write_nfc_drive(bus, 7));
  }
  {
    Transport bus;
    bus.replies.pop_back();
    assert(!glaze::write_nfc_drive(bus, 7));
  }
  {
    Transport bus;
    bus.now = UINT32_MAX - 50;
    bus.read_ok = true;
    bus.ready_after = UINT32_MAX;
    assert(glaze::write_nfc_drive(bus, 7));
  }
  std::cout << "Verified NFC drive framing, acknowledgments, bounds, I2C failures and timeout.\n";
}
