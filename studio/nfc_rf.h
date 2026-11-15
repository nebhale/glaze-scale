#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#ifdef USE_ESP32
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/hal.h"
#endif

namespace glaze {

// Configure the PN532 before its first poll, while no tag read is in flight.
// UM0701-02, section 7.3.1/table 19: 106 kbit/s Type A analog settings.
// Only CWGsP changes; all other values retain NXP's documented defaults.
template<typename Transport> bool write_nfc_drive(Transport &transport, int drive) {
  if (drive < 1 || drive > 63) return false;
  std::array<uint8_t, 21> command{0x00, 0x00, 0xFF, 0x0E, 0xF2, 0xD4, 0x32, 0x0A,
      0x59, 0xF4, static_cast<uint8_t>(drive), 0x11, 0x4D, 0x85, 0x61, 0x6F, 0x26, 0x62, 0x87, 0x00, 0x00};
  for (size_t i = 5; i < 19; ++i) command[19] -= command[i];
  if (!transport.write(command.data(), command.size())) return false;

  auto receive = [&](const auto &expected) {
    const uint32_t started = transport.now_ms();
    uint8_t status = 0;
    do {
      if (!transport.read(&status, 1)) return false;
      if (status & 1) {
        // Every I2C read starts with a status byte, followed by the PN532 frame.
        std::array<uint8_t, 10> data{};
        return transport.read(data.data(), expected.size() + 1) && (data[0] & 1) &&
               std::equal(expected.begin(), expected.end(), data.begin() + 1);
      }
      transport.pause_ms(1);
    } while (static_cast<uint32_t>(transport.now_ms() - started) < 100);
    return false;
  };
  constexpr std::array<uint8_t, 6> ack{0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
  constexpr std::array<uint8_t, 9> reply{0x00, 0x00, 0xFF, 0x02, 0xFE, 0xD5, 0x33, 0xF8, 0x00};
  return receive(ack) && receive(reply);
}

#ifdef USE_ESP32
inline bool configure_nfc_drive(esphome::i2c::I2CDevice &reader, int drive) {
  struct Transport {
    esphome::i2c::I2CDevice &reader;
    bool write(const uint8_t *data, size_t size) { return reader.write(data, size) == esphome::i2c::ERROR_OK; }
    bool read(uint8_t *data, size_t size) { return reader.read(data, size) == esphome::i2c::ERROR_OK; }
    uint32_t now_ms() { return esphome::millis(); }
    void pause_ms(uint32_t ms) { esphome::delay(ms); }
  } transport{reader};
  return write_nfc_drive(transport, drive);
}
#endif

}  // namespace glaze
