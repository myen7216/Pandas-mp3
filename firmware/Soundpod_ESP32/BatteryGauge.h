#pragma once
#include <stdint.h>
#include <math.h>

// Shared layout: the entire right-hand region is reserved, even for '--%'.
constexpr int BATTERY_REGION_X = 108;
constexpr int PANDA_SPRITE_WIDTH = 13;
constexpr int PANDA_BATTERY_GAP = 2;
constexpr int PANDA_MAX_X = BATTERY_REGION_X - PANDA_BATTERY_GAP - PANDA_SPRITE_WIDTH;

struct BatteryGauge {
  bool valid = false;
  bool low = false;
  int percent = -1;
  float millivolts = 0;

  // Approximate generic single-cell 4.2 V LiPo curve, NOT measured capacity.
  // Tune against the actual cell under this player's load after calibration.
  static int estimatePercent(float mv) {
    const int volts[] = {3300, 3500, 3600, 3700, 3750, 3800, 3850, 3900, 4000, 4100, 4200};
    const int levels[] = {   0,    5,   10,   20,   30,   40,   50,   60,   80,   90,  100};
    if (mv <= volts[0]) return 0;
    for (unsigned i = 1; i < sizeof(volts) / sizeof(volts[0]); ++i) {
      if (mv <= volts[i]) {
        return (int)lroundf(levels[i-1] + (mv - volts[i-1]) *
                           (levels[i] - levels[i-1]) / (volts[i] - volts[i-1]));
      }
    }
    return 100;
  }

  // Return whether the visible state changed. No charging inference or shutdown.
  bool accept(float batteryMv) {
    const int oldPercent = percent;
    const bool oldLow = low;
    const bool oldValid = valid;
    if (!isfinite(batteryMv) || batteryMv < 2800 || batteryMv > 4300) {
      valid = false;
      percent = -1;
      low = false;
      millivolts = 0;
    } else {
      millivolts = valid ? 0.75f * millivolts + 0.25f * batteryMv : batteryMv;
      valid = true;
      percent = estimatePercent(millivolts);
      if (percent <= 10) low = true;
      else if (percent >= 15) low = false;
    }
    return oldPercent != percent || oldLow != low || oldValid != valid;
  }
};
