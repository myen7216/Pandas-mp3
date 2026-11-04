# Software dependencies and notices

The repository ships the final sketch and project headers, not the external Arduino libraries or audio files. Install dependencies separately through Arduino's managers. Existing notices in the source are preserved. No new blanket license is asserted over external code, fonts or media.

| Dependency | Tested version | Source / notices |
|---|---:|---|
| Arduino-ESP32 | 3.3.12 | [Espressif](https://github.com/espressif/arduino-esp32); core/SDK components carry their respective notices |
| Adafruit GFX Library | 1.12.6 | [Adafruit](https://github.com/adafruit/Adafruit-GFX-Library); retain its BSD-style notice and applicable font notices |
| Adafruit SSD1306 | 2.5.17 | [Adafruit](https://github.com/adafruit/Adafruit_SSD1306); BSD-style license |
| Adafruit BusIO | 1.17.4 | [Adafruit](https://github.com/adafruit/Adafruit_BusIO); MIT license |
| DFRobotDFPlayerMini | 1.0.6 | [DFRobot](https://github.com/DFRobot/DFRobotDFPlayerMini); LGPL notice |
| Python 3 / C++17 compiler | Host tools | Used only for validation and optional tooling |
| Mutagen | Optional catalog helper | [Mutagen](https://mutagen.readthedocs.io/); not used by firmware |

`Wire`, `HardwareSerial`, FreeRTOS, ADC and sleep interfaces come with the ESP32 board package. Standard C++ atomics coordinate the sampled held-button state with the main loop.

The preview images are software renderings produced during development. They do not include the music or startup recording. Supply your own audio; no permissions for third-party audio recordings are conveyed by this repository.
