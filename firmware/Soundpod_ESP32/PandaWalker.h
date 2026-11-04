#pragma once
#include <stdint.h>
#include <math.h>
#include "BatteryGauge.h"

// Persistent pet state is independent of song timers and drawing calls.
struct PandaWalker {
  float x = 1.0f;
  float target = 1.0f;
  float speed = 10.0f;
  uint32_t restMs = 0;
  uint32_t stepMs = 0;
  bool rightward = true;
  bool moving = false;

  void update(uint32_t elapsedMs, bool playing, uint32_t choice) {
    if (!playing) {
      moving = false;
      return;
    }
    // Serial commands can block during skips. Never catch up missed movement:
    // at most 50 ms of walking is applied per visible frame (under one pixel).
    const uint32_t dt = elapsedMs > 50 ? 50 : elapsedMs;
    if (dt == 0) return;
    if (restMs > 0) {
      restMs = dt >= restMs ? 0 : restMs - dt;
      moving = false;
      return;
    }
    if (fabsf(target - x) < 0.01f) {
      target = 1.0f + (choice % PANDA_MAX_X);
      if (fabsf(target - x) < 10.0f)
        target = x < (1.0f + PANDA_MAX_X) / 2.0f ? (float)PANDA_MAX_X : 1.0f;
      speed = 7.0f + ((choice >> 8) % 9); // Random, leisurely 7..15 px/sec.
      rightward = target > x;
    }
    moving = true;
    stepMs += dt;
    const float distance = speed * dt / 1000.0f;
    if (fabsf(target - x) <= distance) {
      x = target;
      restMs = 250 + ((choice >> 16) % 1001);
      moving = false;
    } else {
      x += rightward ? distance : -distance;
    }
  }
};
