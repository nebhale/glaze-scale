#pragma once

#include "storage_slot.h"
#include "esphome/core/preferences.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <stdint.h>

namespace glaze {

// Stored together so a power cycle cannot mix values from two calibrations.
struct Calibration {
  float zero = 0;
  float counts_per_gram = 0;

  bool restore() {
    std::string bytes;
    if (!slot_.access(bytes, false)) return false;
    std::array<float, 2> values;
    if (bytes.empty()) {
      // The previous restoring global used ESPHome's globals prefix XOR the
      // name hash of "load_calibration". A fresh device keeps its initial value.
      constexpr uint32_t old_key = 1944399030U ^ 2826005165U;
      if (!esphome::global_preferences->load_from_key(old_key, reinterpret_cast<uint8_t *>(values.data()), sizeof(values)))
        return true;
    } else {
      if (bytes.size() != sizeof(values)) return false;
      std::memcpy(values.data(), bytes.data(), sizeof(values));
    }
    if (!std::isfinite(values[0]) || !std::isfinite(values[1])) return false;
    *this = {values[0], values[1]};
    return true;
  }

  bool save(const Calibration &next) {
    if (!std::isfinite(next.zero) || !std::isfinite(next.counts_per_gram) || next.counts_per_gram == 0) return false;
    const std::array<float, 2> values{next.zero, next.counts_per_gram};
    std::string bytes(reinterpret_cast<const char *>(values.data()), sizeof(values));
    if (!slot_.access(bytes, true)) return false;
    *this = next;
    return true;
  }

 private:
  static constexpr StorageSlot slot_{"calibration_v1", ".calibration"};
};

#ifdef USE_HOST
inline Calibration platform_calibration{0, 1};  // The simulator starts with one count per gram.
#else
inline Calibration platform_calibration;
#endif

class Calibrator {
 public:
  void sample(float raw, uint32_t now) {
    if (now - last_ > 350 || !std::isfinite(raw)) size_ = 0;
    if (!std::isfinite(raw)) return;
    last_ = now;
    raw_[index_] = raw;
    index_ = (index_ + 1) % raw_.size();
    size_ = std::min(size_ + 1, raw_.size());
  }
  bool empty(uint32_t now) {
    reference_zero_.reset();
    if (!ready(now)) return false;
    reference_zero_ = mean();
    empty_range_ = spread();
    size_ = 0;
    return true;
  }
  bool calibrate(Calibration &active, int mass, uint32_t now) const {
    if (!reference_zero_ || !ready(now) || mass <= 0 || mass > 2000) return false;
    float factor = (mean() - *reference_zero_) / mass;
    if (factor == 0 || !settled(now, factor) || empty_range_ > std::abs(factor)) return false;
    active = {*reference_zero_, factor};
    return true;
  }
 private:
  std::array<float, 10> raw_{};
  size_t index_ = 0, size_ = 0;
  uint32_t last_ = 0;
  std::optional<float> reference_zero_;
  float empty_range_ = 0;
  float mean() const {
    double sum = 0;
    for (auto sample : raw_) sum += sample;
    return sum / raw_.size();
  }
  float spread() const {
    auto range = std::minmax_element(raw_.begin(), raw_.end());
    return *range.second - *range.first;
  }
  bool ready(uint32_t now) const { return size_ == raw_.size() && now - last_ <= 350; }
  bool settled(uint32_t now, float factor) const { return ready(now) && spread() <= std::abs(factor); }
};

inline Calibrator device_calibrator;
}  // namespace glaze
