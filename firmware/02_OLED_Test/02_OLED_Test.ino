// Classic ESP32 (panda board) + Adafruit 938 128x64 SSD1306, factory I2C mode.
// Unplug USB before wiring. OLED VIN->3V3, GND->GND, Data->D21, Clk->D22.
// Leave 3Vo, Rst, CS and A0/DC unconnected on this auto-reset OLED.
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const uint8_t OLED_ADDRESS = 0x3D;
Adafruit_SSD1306 display(128, 64, &Wire, -1);

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  delay(500);
  Serial.println(F("Scanning I2C..."));
  bool found = false;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("Found 0x"));
      Serial.println(address, HEX);
      if (address == OLED_ADDRESS) found = true;
    }
  }
  if (!found) {
    while (true) {
      Serial.println(F("No OLED at 0x3D. Check four wires and I2C jumpers."));
      Serial.println(F("If scan found 0x3C, check address setting before changing code."));
      delay(2000);
    }
  }
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false)) {
    while (true) {
      Serial.println(F("OLED buffer allocation failed."));
      delay(2000);
    }
  }
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
}

void loop() {
  display.clearDisplay();
  display.setCursor(0, 8);
  display.println(F("Michael's MP3"));
  display.println();
  display.println(F("OLED connected"));
  display.print(F("Seconds: "));
  display.println(millis() / 1000);
  display.display();
  delay(250);
}
