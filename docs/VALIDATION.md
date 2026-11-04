# Release validation

## Status

This is the final ESP32 source package for the completed project. The builder previously confirmed the integrated breadboard player and later reported playback working after repairing the soldered DFPlayer ground. This report distinguishes that history from checks performed on the exact release firmware.

The latest game and per-round-score revisions have been compiled and simulated. They were not uploaded to a physical board during packaging. No final battery runtime, complete-system sleep current, noise measurement or battery-capacity test is claimed.

## Reproduce the checks

Install Python 3 and a C++17 compiler (`c++` on PATH). Run from the repository root:

```sh
python3 tools/check_package.py
python3 tools/run_checks.py
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/Soundpod_ESP32
```

Most host tests need only Python and C++. The installed-library transport test additionally needs DFRobotDFPlayerMini 1.0.6. It defaults to the standard macOS Arduino sketchbook library location. Set `DFPLAYER_LIBRARY_DIR` to its actual installation directory on your computer if different.

## Results recorded during packaging

| Check | Result / scope |
|---|---|
| Final firmware and four headers copied | Byte-for-byte match against the active project files |
| ESP32 compilation | Pass using ESP32 core 3.3.12 and the versions listed in the README |
| Audio startup and wake | Pass with simulated replies, timeouts and bounded reset recovery |
| Startup sound | Pass: cold-start-only command order, event draining, playlist exclusion |
| Automatic next | Pass: duplicate notifications, wraparound, manual-skip races and completion underneath the game |
| Buttons | Pass: debounce, short taps, holds, queued events during blocked UI and entry/exit chord cancellation |
| Game model | Pass: movement, normalized diagonals, direct pursuit, bounds, collection, safe respawns, collision and restart |
| Score / display | Pass: final score remains at game over; production drawing emits `Score: …`; restart returns to zero |
| Audio state in game | Pass: no game-triggered pause/resume, manual skip or volume change |
| Battery model and sampling | Pass: divider conversion, filtering, invalid readings, warning and icon layout |
| Wandering panda | Pass: bounds, battery-area exclusion and continuity across track changes |
| Sleep policy | Pass: playback protection, timeout boundary, held buttons, game inactivity and shutdown ordering |
| Installed DFPlayer transport | Pass: reproduces the ACK-related delay, validates runtime no-ACK sends and finish-event parsing |
| Optional catalog helper | Separate offline prototype; not enabled in firmware |

Eleven host suites run against production code or installed library code. They use simulated hardware and do not prove real contact quality, audio output, wake timing or OLED frame rate.

## Practical hardware acceptance checklist

| Action | Expected behaviour |
|---|---|
| Power on without pressing EN or Next | Panda/typewriter intro, then first song starts |
| Listen through both ears | Stereo DAC/jack contacts produce left and right sound |
| Pause then resume | Elapsed timer stops then continues; display brightness stays constant |
| Tap all controls | One action per discrete press; volume acts on release |
| Let a full song end | Exactly one transition, then normal playback |
| Hold both volume buttons five seconds | Enter game without changing volume |
| Collect bamboo and get caught | Score increments; game over freezes play and shows final score |
| Press Play/Pause after game over | Safe starting positions and score zero |
| Let a song end in the game | Background audio advances normally |
| Hold both volume buttons to leave | Current music screen returns with correct song/state |
| Leave paused for ten minutes | Deep sleep; Play/Pause wakes to the normal player and restarts retained song |
| Switch to battery power with USB removed | Stable 5 V rail, startup and audio |
| Measure battery/divider | GPIO34 sees approximately half the raw cell voltage |
| Charge with player switched off | Correct TP4056 indicator and battery voltage approaching full |

Actual rail voltages, runtime and final audio quality should be recorded as measured results if this build is reproduced. The repository does not fill missing measurements with estimates.

## Quiet volume and OLED orientation revision

The startup volume is 1; buttons step by 1 over 0..10, and restored wake volume is clamped to that range. The production-loop test covers reaching level 1, mute and the upper bound.

`check_display_orientation.py` exercises the production display subclass and idle maintenance with a simulated SSD1306: normal/game/game-over transfers restore software rotation 0 and hardware commands A1/C8, and idle maintenance repeats once per second without redrawing the framebuffer or changing contrast. This tests the recovery logic, not the physical cause of a flip. A loose/powered-down panel or sustained bus fault still needs an electrical repair. Physical recovery has not been verified on the board.
