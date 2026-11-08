#pragma once

#include <cstdint>
#include <optional>

namespace glaze {

enum class NfcFault { None, Startup, Read };

class NfcHealth {
 public:
  NfcFault fault = NfcFault::None;

  void observe(bool failed, bool warning, uint32_t now) {
    if (failed) {
      fault = NfcFault::Startup;
    } else if (!warning) {
      warning_since_.reset();
      fault = NfcFault::None;
    } else {
      if (!warning_since_) warning_since_ = now;
      if (now - *warning_since_ >= 1000) fault = NfcFault::Read;
    }
  }

 private:
  std::optional<uint32_t> warning_since_;
};

inline NfcHealth nfc;
}  // namespace glaze
