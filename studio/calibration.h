#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdint.h>

namespace glaze {

// Platform zero and counts per gram share one ESPHome restoring global.
using Calibration = std::array<float, 2>;

struct CalibrationDiagnostics {
  size_t samples;
  uint32_t age_ms;
  float mean, minimum, maximum, empty_mean, empty_range;
};

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
    if (problem(mass, now)) return false;
    float factor = (mean() - *reference_zero_) / mass;
    active = {*reference_zero_, factor};
    return true;
  }
  const char *problem(int mass, uint32_t now) const {
    if (!reference_zero_) return "Capture the empty platform first";
    if (mass <= 0 || mass > 2000) return "Use a steady weight, 1 to 2000 g";
    if (!ready(now)) return "Waiting for load-cell readings";
    float factor = (mean() - *reference_zero_) / mass;
    if (!std::isfinite(factor) || factor == 0) return "No load change. Check wiring";
    if (!settled(now, factor)) return "Weight moving or signal noisy";
    if (empty_range_ > std::abs(factor)) return "Empty reading noisy. Start again";
    return nullptr;
  }
  CalibrationDiagnostics diagnostics(uint32_t now) const {
    auto range = std::minmax_element(raw_.begin(), raw_.end());
    bool full = size_ == raw_.size();
    return {size_, now - last_, full ? mean() : NAN, full ? *range.first : NAN,
            full ? *range.second : NAN, reference_zero_.value_or(NAN), empty_range_};
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
