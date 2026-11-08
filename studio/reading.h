#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <optional>
#include <string>

namespace glaze {

using Grams = std::optional<double>;
constexpr double load_detection_g = 3;  // Above the platform's +/-1 g jitter.

inline std::string grams(Grams weight) {
  return weight ? std::to_string(std::lround(*weight)) + " g" : "Unknown";
}

// A new tag or a gap in samples starts a fresh stability window.
class Reading {
 public:
  void reset() { count_ = cursor_ = 0; }
  void sample(double value, uint32_t now) {
    if (!std::isfinite(value)) { reset(); return; }
    if (now - sampled_at_ > 350) reset();
    sampled_at_ = now;
    samples_[cursor_] = value;
    cursor_ = (cursor_ + 1) % samples_.size();
    count_ = std::min(count_ + 1, samples_.size());
  }
  Grams gross(uint32_t now) const {
    if (!count_ || now - sampled_at_ > 350) return {};
    return std::round(samples_[(cursor_ + samples_.size() - 1) % samples_.size()]);
  }
  bool stable(uint32_t now) const {
    if (!gross(now) || count_ < samples_.size()) return false;
    auto range = std::minmax_element(samples_.begin(), samples_.end());
    return *range.second - *range.first <= 1.0;
  }
  Grams settled_mean(uint32_t now) const {
    if (!stable(now)) return {};
    return std::accumulate(samples_.begin(), samples_.end(), 0.0) / samples_.size();
  }
 private:
  std::array<double, 8> samples_{};
  size_t count_ = 0, cursor_ = 0;
  uint32_t sampled_at_ = 0;
};

}  // namespace glaze
