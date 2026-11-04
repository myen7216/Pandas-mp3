# Hidden bamboo chase game

Implemented in the active `firmware/Soundpod_ESP32/Soundpod_ESP32.ino`, with the game in `BambooGame.h` and held-button/chord routing in `ButtonInput.h`. No rewiring is needed. Firmware compilation and host checks pass; the game has not yet been uploaded or physically tested on Michael's player.

## How to play

After startup finishes, hold **Volume + and Volume - together for five continuous seconds**. Release all buttons when the game appears.

| Button | Game action |
| --- | --- |
| Next | Walk right |
| Previous | Walk left |
| Volume + | Walk up |
| Volume - | Walk down |
| Play/Pause after game over | Restart with score 0 |
| Both volume buttons held five seconds | Return to the normal music screen |

Hold a direction to keep moving. Diagonal movement is allowed at the same overall speed; opposite directions cancel. The panda stays inside the screen below the score strip.

Collect the thin bamboo stalks with one tiny leaf. They pulse between 3x5 and 4x7 pixels. Each adds one point and appears in a new safe location. The top-left number is your score for the current round, starting at 0.

The small smiley follows the shortest direct path toward you, at a slower speed than the panda. Touching it freezes the game and displays `GAME OVER`, `Score: <final score>`, and `PLAY: RESTART`. The final score remains visible until you press Play/Pause. Restarting resets score to 0 and starts in safe positions. Leaving and re-entering preserves the current round, including its game-over screen; powering off or waking from ESP32 deep sleep starts a fresh round at 0.

Holding both volume buttons freezes the game immediately while the five-second timer runs, including the bamboo pulse. Releasing early cancels the gesture. After switching modes, all buttons must be released before another action can happen. A continued long hold cannot immediately switch back.

## Music and power

- Music keeps its playing or paused state on entry and exit. Game controls do not change volume, pause/resume music or skip tracks.
- Automatic next-song playback continues in the background, including during game over. Returning shows the current song and elapsed timer.
- On the normal playback screen, individual volume taps apply **when released**. This allows the two-button entry gesture to leave volume unchanged, even if you press one button slightly before the other. Play/Pause, Next and Previous remain press-triggered.
- Game input keeps the player awake while music is paused. After ten minutes with music paused and no game input, it enters the existing deep sleep. Play/Pause wakes it into the normal player screen.
- Score stays in RAM for this round only. It is not read from or saved to flash, including on game over, exit or sleep. Previously saved totals from older firmware are ignored.
- The game does not turn on Wi-Fi or Bluetooth. Normal playback keeps the existing walking panda and battery display.

## Code layout and reversal

The complete program is `firmware/Soundpod_ESP32/Soundpod_ESP32.ino`. Keep all four `.h` files beside it. `BambooGame.h` owns the game and `ButtonInput.h` owns input routing. Set `ENABLE_BAMBOO_GAME = false` in the sketch to hide the game and restore press-triggered volume controls.

## Verification

Arduino CLI compilation passed for `esp32:esp32:esp32` using installed ESP32 core 3.3.12. Host checks cover:

- Five-second threshold, staggered/bouncing volume buttons, cancellation, continued holds, release barriers, timer rollover and feature disable.
- Four directions, normalized diagonals, screen boundaries, direct pursuit speed/direction, bounded catch-up after serial stalls, fixed collection range, safe respawns over 999 seeds, score saturation, frozen game over, restart and mode re-entry.
- The production loop continuing automatic-next/duplicate-event protection in active and dead games, preserving audio state/timer/volume, final-score display and zero-on-restart behaviour, and active/idle sleep behaviour.
- Existing startup-audio, jingle, wake, battery, normal button, random walking panda and serial-transport regression checks.

These checks do not establish real button feel, display frame rate, physical audio output on the board. After uploading, physically check entering/exiting, directional holds, collecting, getting caught, restarting, a song ending during gameplay, a paused-song game session, a power cycle after exit, and the ten-minute paused timeout.

When uploading, use the documented programming link to disconnect the boosted 5 V feed before connecting computer USB. Reconnect battery power only after unplugging USB.
