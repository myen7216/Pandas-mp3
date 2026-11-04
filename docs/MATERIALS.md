# Materials, ingredients and workshop tools

Quantities are for one player. The final design uses five buttons; a sixth spare button is not connected. A custom fabricated PCB is not required: perfboard/protoboard can carry the final soldered circuit.

## Electronics

| Quantity | Material | Specification / why it was chosen |
|---:|---|---|
| 1 | ESP32 board | Classic 30-pin ESP32-D0WD-V3 board with USB-C and CH340; use ESP32 Dev Module in Arduino IDE |
| 1 | OLED | Adafruit 938, 1.3-inch SSD1306, 128×64; unchanged I²C jumper configuration, address 0x3D |
| 1 | DFPlayer Mini | Dedicated MP3 decoder with serial control and stereo DAC outputs |
| 1 | microSD | 32 GB or smaller, FAT32; accessible after assembly |
| 5 | Tactile buttons | Normally open, GPIO to GND; internal pull-ups are used |
| 1 | Stereo headphone jack | CTIA TRRS connection for wired Apple EarPods; identify its actual contacts |
| 1 pair | Headphones | Wired 3.5 mm EarPods used for the build |
| 1 | 1 kΩ resistor | In series from GPIO17 to DFPlayer RX; not a battery-divider resistor |
| 1 | LiPo cell | Single-cell 3.7 V nominal, 2000 mAh; build label 555060P |
| 1 | TP4056 board | USB-C board with protected B+/B− and OUT+/OUT− connections |
| 1 | 5 V boost | Fixed-output ECSiNG-style VI/GND/VO module; selected listing specified up to about 480 mA, not a measured guaranteed current for this assembly |
| 1 | Master switch | Latching on/off, placed before boost VI; choose an adequate DC current rating |
| 1 | Removable power link / disconnect | Isolates boost VO from player supply before ESP32 USB programming |
| 2 | 100 kΩ resistors | 1% metal-film; divide battery voltage by two |
| 1 | 100 nF ceramic capacitor | At GPIO34 sense node to GND, often marked 104 |

## Optional audio improvements

| Quantity | Part | Connection / limitation |
|---:|---|---|
| 1 | Additional 100 nF ceramic | DFPlayer VCC to GND, close to module |
| 1 | 470 µF electrolytic, ≥10 V | DFPlayer VCC (+) to GND (striped −) |
| 2 | Identical 47–100 Ω resistors | One per DAC channel if attenuation is needed; reduces both music and hiss |

The supply-filter capacitor is separate from the battery-sense capacitor. If both filters are installed, there are two 100 nF capacitors. Optional parts are documented recommendations; their exact installation and noise reduction were not recorded as confirmed measurements.

## Construction materials

| Material | Purpose |
|---|---|
| Two solderless breadboards | Prototype without losing access to the ESP32 pins |
| Male-to-male jumpers | Temporary breadboard wiring |
| Perfboard/protoboard | Durable soldered assembly |
| Insulated hookup wire | Short power, ground and signal connections |
| Headers, sockets or connectors | Serviceable modules and battery connection |
| Heat-shrink / insulation | Cover exposed conductors and prevent shorts |
| Nonconductive mounts / standoffs | Secure boards and protect the LiPo from sharp joints |
| Suitable enclosure | Optional finishing step; leave ports, switch and card accessible |
| USB-C data cable | Program and bench-test the ESP32 |
| USB-A-to-C cable and 5 V USB supply | Charge the TP4056 board used in this build |

## Tools and software

| Tool / software | What it was used for |
|---|---|
| Soldering iron, solder and stand | Final joints and repairs |
| Wire cutters / strippers / tweezers | Prepare and route wiring |
| Multimeter | DC voltage, continuity, polarity and resistance checks |
| Computer and card reader | Arduino upload, MP3 preparation and checks |
| Arduino IDE / Arduino CLI | Compile, upload and serial diagnostics |
| Espressif ESP32 core | Board support, UART, ADC, FreeRTOS and deep sleep |
| Adafruit GFX, SSD1306 and BusIO | OLED drawing and I²C interface |
| DFRobotDFPlayerMini | Audio commands and completion messages |
| Python 3 and a C++17 compiler | Host regression checks |

No Pro Mini, Pro Micro, external serial breakout, speaker amplifier, Wi-Fi connection or Bluetooth connection is required.
