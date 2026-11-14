#include "../studio/calibration.h"

#include <cassert>
#include <cstring>
#include <limits>

int main() {
  glaze::Calibrator calibrator;
  glaze::Calibration active{42, 3};
  uint32_t now = 0;
  auto sample = [&](float raw) { calibrator.sample(raw, now += 100); };
  auto fill = [&](float raw) { for (int i = 0; i < 10; ++i) sample(raw); };
  auto rejected = [&](int mass, const char *message) {
    assert(!calibrator.calibrate(active, mass, now));
    assert((active == glaze::Calibration{42, 3}));
    assert(!strcmp(calibrator.problem(mass, now), message));
  };

  assert(std::isnan(calibrator.diagnostics(now).mean));
  rejected(597, "Capture the empty platform first");
  fill(1000);
  assert(calibrator.empty(now));
  rejected(597, "Waiting for load-cell readings");
  rejected(0, "Use a steady weight, 1 to 2000 g");
  fill(1000);
  rejected(0, "Use a steady weight, 1 to 2000 g");
  rejected(2001, "Use a steady weight, 1 to 2000 g");
  rejected(597, "No load change. Check wiring");
  fill(2194);
  sample(2200);
  rejected(597, "Weight moving or signal noisy");
  fill(2194);
  auto data = calibrator.diagnostics(now);
  assert(data.samples == 10 && data.age_ms == 0 && data.mean == 2194);
  assert(data.minimum == 2194 && data.maximum == 2194 && data.empty_mean == 1000);
  now += 351;
  rejected(597, "Waiting for load-cell readings");
  sample(2194);
  assert(calibrator.diagnostics(now).samples == 1);
  rejected(597, "Waiting for load-cell readings");
  sample(std::numeric_limits<float>::quiet_NaN());
  assert(calibrator.diagnostics(now).samples == 0);
  fill(2194);
  assert(calibrator.calibrate(active, 597, now));
  assert((active == glaze::Calibration{1000, 2}));

  // The original empty window must meet the same 1 g noise limit.
  active = {42, 3};
  fill(1000);
  sample(1006);
  assert(calibrator.empty(now));
  fill(2194.6f);
  rejected(597, "Empty reading noisy. Start again");

  // Reversing the signal wires changes the factor's sign, which is valid.
  fill(1000);
  assert(calibrator.empty(now));
  fill(-194);
  assert(calibrator.calibrate(active, 597, now));
  assert((active == glaze::Calibration{1000, -2}));
}
