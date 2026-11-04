#pragma once
#include <stdint.h>

// Sample independently of OLED transfers and audio commands. One event per
// stable press; a stable release is required before the next event.
struct DebouncedButton {
  bool lastDown = false;
  bool stableDown = false;
  bool armed = false;
  uint32_t changedAt = 0;

  bool sample(bool down, uint32_t now) {
    if (down != lastDown) {
      lastDown = down;
      changedAt = now;
    }
    if (now - changedAt < 30) return false;
    if (!armed) {
      stableDown = down;
      if (!down) armed = true; // Suppress a held wake/startup button.
      return false;
    }
    if (down == stableDown) return false;
    stableDown = down;
    return down;
  }
};

// Pure input routing, exercised on the host as well as by the 5 ms sampler.
// Normal volume taps commit on RELEASE so even a staggered 5-second chord
// cannot change the volume. All buttons must release after a mode transition.
struct GameButtonRouter {
  static constexpr uint8_t ENTER = 64, EXIT = 65, RESTART = 128;
  static constexpr uint8_t VOLUMES = (1 << 3) | (1 << 4);
  struct Result {
    uint8_t events[6] = {};
    uint8_t count = 0;
    uint8_t held = 0; // bit 7 freezes movement during a chord/release barrier.
    bool toggled = false;
    void emit(uint8_t event) { events[count++] = event; }
  };
  bool enabled = true, active = false, chord = false, volumeBlocked = false;
  bool releaseBarrier = false;
  uint8_t previous = 0, pendingVolume = 0;
  uint32_t chordStarted = 0;

  Result sample(uint8_t down, uint8_t pressed, uint32_t now) {
    Result result;
    result.held = down;
    if (!enabled) {
      for (uint8_t i = 0; i < 5; ++i) if (pressed & (1 << i)) result.emit(i);
      previous = down;
      return result;
    }
    if (releaseBarrier) {
      result.held = 0x80;
      if (!down) {
        releaseBarrier = false; volumeBlocked = false; pendingVolume = 0;
        result.held = 0;
      }
      previous = down;
      return result;
    }
    const bool both = (down & VOLUMES) == VOLUMES;
    if (both) {
      if (!chord) { chord = true; chordStarted = now; }
      volumeBlocked = true;
      pendingVolume = 0;
      result.held = 0x80;
      if (now - chordStarted >= 5000) {
        active = !active;
        result.emit(active ? ENTER : EXIT);
        result.toggled = true;
        releaseBarrier = true;
        chord = false;
      }
    } else {
      chord = false;
      if (volumeBlocked) {
        // Consume both halves even when a chord is cancelled early.
        if (!(down & VOLUMES)) volumeBlocked = false;
      }
      if (active) {
        if (pressed & 1) result.emit(RESTART);
      } else {
        for (uint8_t i = 0; i < 3; ++i) if (pressed & (1 << i)) result.emit(i);
        if (!volumeBlocked) {
          pendingVolume |= pressed & VOLUMES;
          const uint8_t released = previous & ~down & pendingVolume;
          for (uint8_t i = 3; i < 5; ++i) if (released & (1 << i)) result.emit(i);
          pendingVolume &= ~released;
        }
      }
    }
    previous = down;
    return result;
  }
};
