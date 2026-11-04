# Battery power, charging and percentage

## Why there is a boost converter

A single-cell LiPo is nominally 3.7 V, reaches about 4.2 V fully charged and falls as it discharges. The TP4056 charges the cell; it does not create a regulated 5 V rail. The boost supplies approximately 5 V to ESP32 VIN and DFPlayer VCC. The ESP32's regulator supplies 3.3 V to the OLED.

Use the protected OUT+/OUT− terminals for the player. Do not bypass the protection by wiring the player's ground to B−. Charge through the TP4056 with the master switch off. This circuit does not include a load-sharing power path.

## Measuring with the multimeter

For DC voltage, black lead goes in COM, red in V/Ω, dial on DC V (20 V range for a manual meter). Measure in parallel across the points below; do not move the red lead to the current socket.

| Black probe | Red probe | Typical expected reading |
|---|---|---:|
| TP4056 B− | TP4056 B+ | About 3.0–4.2 V |
| TP4056 OUT− | TP4056 OUT+ | Approximately battery voltage |
| Boost GND | Boost VI, switch on | Approximately battery voltage |
| Boost GND | Boost VO | Approximately 5 V |
| ESP32 GND | ESP32 VIN | Approximately 5 V on battery power |
| Common GND | GPIO34 sense node | About half raw battery voltage |

Voltage disappearing between two stages points to the intervening wire, switch or solder joint. A probe making the player turn on suggests a mechanical connection problem; the meter should not be required to complete the circuit.

Resistance and continuity checks require **battery and USB disconnected**. Touch probes together first to learn the lead resistance. A direct ground-to-ground connection should be close to that value, generally 0–2 Ω. A stable reading around 1800 Ω is not a good common-ground connection. Capacitors can cause temporary changing readings across power rails; a persistent near-zero power-to-ground resistance needs investigation before powering up.

## Divider and percentage

Use two equal 100 kΩ resistors and a 100 nF capacitor:

```text
After power switch, before boost VI
            │
          100 kΩ
            │
            ├──── GPIO34
            ├──── 100 nF ─── GND
          100 kΩ
            │
           GND
```

At 4.2 V the ADC sees about 2.1 V; at 3.7 V it sees about 1.85 V. The divider draws approximately 21 µA at full charge and switches off with the master switch. Never measure battery percentage from the boosted 5 V rail; it stays regulated while battery charge changes.

The final sketch has `BATTERY_SENSE_CONNECTED = true`. Disable that setting until the divider is installed. The firmware samples 32 times per batch, starts a new batch about every 30 seconds, smooths readings and applies a generic LiPo voltage curve. An invalid/disabled reading displays `--%`. A low percentage adds a warning symbol but does not itself turn off power.

## Calibration

1. Run the player from battery with the charger disconnected.
2. Measure the switched raw battery voltage against common ground.
3. Observe `Battery: raw=... mV` in Serial Monitor using an appropriate power-isolated setup; do not attach computer USB to an independently boosted 5 V rail. If USB isolation is unavailable, take comparable battery/ADC readings in separate controlled tests rather than paralleling the supplies.
4. Use the same units and calculate `BATTERY_CALIBRATION = meter_mV / raw_mV`. The logged raw value already includes the divider factor of two.
5. Update the constant, upload with the boost isolated, reconnect battery power and compare at several charge levels.

Calibrating voltage does not calibrate remaining capacity. Voltage drops under load and recovers when playback stops. Temperature, battery age and a generic discharge curve also affect percentage. A change from 85% to 92% need not mean the battery gained energy.

## Charging verification and USB-C

With the player switched off, record B+ to B−, plug a suitable 5 V supply into the TP4056 and check its charging LED. Unless nearly full, battery voltage should generally rise over time. Near 4.2 V, charging tapers and the complete indicator replaces the charging indicator. LED colours depend on the board; the build's red LED indicated charging.

The build's board charged with USB-A-to-C. Some low-cost USB-C charger boards omit the separate CC1/CC2 sink resistors required for a USB-C source to enable power. That is a likely explanation for C-to-C not working, not a measured diagnosis of this specific connector.

Keep the battery secured and protected from solder points. Stop using a damaged, swollen, unusually hot or leaking cell. Do not short battery leads or solder with power connected.

## What the percentage does not tell you

The OLED has no TP4056 charge-status input, so it cannot show a verified charging bolt or determine charging completion. There is no measured battery runtime or whole-player sleep-current result in this repository. The ESP32 deep-sleep specification is not the consumption of the full board, boost and DFPlayer assembly.
