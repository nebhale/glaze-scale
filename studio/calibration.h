#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdint.h>

namespace glaze {

// Platform zero and counts per gram share one ESPHome restoring global.
using Calibration = std::array<float, 2>;

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
    if (!std::isfinite(factor) || factor == 0 || !settled(now, factor) || empty_range_ > std::abs(factor)) return false;
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
