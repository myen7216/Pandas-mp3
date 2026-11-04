# Build guide — from loose parts to a working player

Start with the [materials](MATERIALS.md) and [pin-by-pin wiring](WIRING.md). Assemble one subsystem at a time. A successful upload proves the program reached the ESP32; it does not prove every solder joint or audio connection works.

## 1. Prepare the ESP32 and software

1. Install Arduino IDE and add **esp32 by Espressif Systems** through Boards Manager.
2. Select **ESP32 Dev Module**. This project targets a classic ESP32, not an ESP32-C3/S3.
3. Install Adafruit SSD1306, Adafruit GFX Library, Adafruit BusIO and DFRobotDFPlayerMini through Library Manager.
4. Disconnect the battery/boost power feed. Connect the ESP32 with a USB data cable.
5. Upload `firmware/01_Board_Test/01_Board_Test.ino`. Open Serial Monitor at 115200. Check that its count advances.

If no port appears, check the data cable and CH340 driver for your operating system. If upload waits at Connecting, hold BOOT while connection begins, then release when writing starts. EN resets the board.

## 2. Connect the OLED

Disconnect power before moving wires. Use 3V3 → OLED VIN, GND → GND, GPIO21 → DATA/SDA and GPIO22 → CLK/SCL. Keep the Adafruit board's original I²C jumpers. Leave reset, CS, A0/DC and 3Vo unused.

Upload `firmware/02_OLED_Test/02_OLED_Test.ino`. The expected address is **0x3D**. Other SSD1306 boards may use 0x3C; verify the actual board before changing the code. Do not change jumpers to SPI for this design.

## 3. Prepare the card

Format a card up to 32 GB as FAT32. Create `/mp3` and copy ordinary MP3 files named `0001.mp3` through `0010.mp3`. Eject the card safely and insert it into the unpowered DFPlayer. Use your own audio; the repository contains no MP3s.

`0011.mp3` is reserved for the optional startup sound. To omit the sound, comment out `startStartupIntroAudio();` in the full sketch. If copying from macOS, hidden `._` files can confuse audio modules. Review the card and use `dot_clean /Volumes/YOUR_CARD` if needed, substituting the correct card volume.

## 4. Add the DFPlayer

| DFPlayer | Connection |
|---|---|
| VCC | Verified approximately 5 V supply |
| GND | Common ground, directly connected to ESP32 GND |
| TX | GPIO16 |
| RX | GPIO17 through 1 kΩ |
| DAC_L | Headphone left |
| DAC_R | Headphone right |

For USB-only testing, first measure whether this board's VIN exposes a suitable USB-derived voltage. Do not assume every ESP32 clone has the same power circuit, and do not add a second independent 5 V source while USB is attached.

For CTIA EarPods: tip = left, ring 1 = right, ring 2 = ground, sleeve = unused microphone. For a three-contact TRS jack, sleeve is ground. Identify the socket's actual terminals; never wire SPK1/SPK2 as left/right audio.

Upload `firmware/03_Audio_Test/03_Audio_Test.ino`. Start with headphones out of your ears and confirm the first file plays. A working audio test makes later UI troubleshooting much easier.

## 5. Add controls

Wire each normally-open button between its GPIO and common ground: Play/Pause 33, Next 19, Previous 23, Volume + 4, Volume − 5. The code enables internal pull-ups. Identify the connected pairs of a four-legged button with continuity mode; do not guess from orientation.

Upload `firmware/04_Button_Test/04_Button_Test.ino` and verify each action. Keep GPIO5 released during power-up/reset because it is a strapping pin.

## 6. Upload the final program

Open `firmware/Soundpod_ESP32/Soundpod_ESP32.ino` with all four headers in the same folder. The welcome, music display, walking panda and game are already implemented. Update the three song tables together if your files differ. The startup jingle must have an ID outside the normal playlist.

Battery sensing is enabled in this final source. Set `BATTERY_SENSE_CONNECTED = false` until the divider in step 8 is fitted; otherwise a floating input may show meaningless values.

Check startup without pressing EN or Next, then pause/resume and every button. Normal volume applies on release. Hold both volume buttons five seconds to enter/exit the game and release all buttons after the mode switches. Restart after game over begins at score 0.

## 7. Add portable power

With all power disconnected, connect battery red → TP4056 B+ and black → B−. Connect OUT+ → master switch → boost VI. Connect OUT− → boost GND and common circuit ground.

Leave boost VO disconnected from the player initially. Turn on the battery supply and measure VO to GND: it should be approximately 5 V. Turn off again before attaching VO through a removable programming link to ESP32 VIN and DFPlayer VCC. OLED power stays on ESP32 3V3.

Disconnect boosted 5 V before connecting ESP32 USB. Remove USB before returning to battery power. The master switch must remain before boost VI so the converter does not drain the cell while the player is physically off.

## 8. Add and calibrate the battery divider

```text
Switched raw battery → 100 kΩ → GPIO34 → 100 kΩ → GND
                                  └── 100 nF ──→ GND
```

The raw battery node is after the power switch and before the boost, not on the 5 V output. With 4.2 V at the battery, GPIO34 should see about 2.1 V. See [battery calibration](BATTERY.md) before interpreting the percentage.

## 9. Transfer to perfboard

Photograph and label the working breadboard first. Plan component positions, install headers, then ground and power buses, then signals. Keep the boost away from the audio wires. Leave the ESP32 antenna end clear and all service ports accessible. Solder only while the battery and USB are disconnected.

Before full power, inspect both sides for bridges, verify ground continuity and power polarity, and measure the boost and divider outputs with modules disconnected when possible. Test after each stage rather than assembling everything and diagnosing all subsystems at once.

## 10. Verify and charge

Charge through the TP4056 with the player switched off. The basic board has no proper load-sharing arrangement, so playing while charging is outside this design. The build's TP4056 worked with USB-A-to-C; some inexpensive USB-C boards lack the CC circuitry needed for C-to-C.

Meter setup for voltage: black lead COM, red lead V/Ω, DC volts (20 V range if manual). Measure B+ to B− for the battery, OUT+ to OUT− for the protected output, VI to GND for boost input, and VO/VIN to GND for regulated power. Never use resistance mode on a powered circuit. Keep probes from bridging neighbouring pads.

See the [design report](DESIGN_REPORT.md) for troubleshooting examples and [validation](VALIDATION.md) for the release checks.
