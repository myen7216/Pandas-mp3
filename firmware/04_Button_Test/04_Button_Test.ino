// Classic ESP32. Connect each button between its GPIO and GND.
// D5 is a strap pin: release volume-down while powering on/resetting.
// Play/pause is D33 (also the full player's deep-sleep wake button).
// Serial Monitor: 115200 baud. No display, DFPlayer or battery needed.
const uint8_t pins[] = {33, 19, 23, 4, 5};
const char* const names[] = {"Play/pause", "Next", "Previous", "Volume up", "Volume down"};

void setup() {
  Serial.begin(115200);
  for (uint8_t pin : pins) pinMode(pin, INPUT_PULLUP);
  delay(500);
  Serial.println("Press one button at a time.");
}

void loop() {
  static uint8_t previous[] = {HIGH, HIGH, HIGH, HIGH, HIGH};
  static uint8_t stable[] = {HIGH, HIGH, HIGH, HIGH, HIGH};
  static unsigned long changedAt[5] = {0};
  for (uint8_t i = 0; i < 5; ++i) {
    uint8_t raw = digitalRead(pins[i]);
    unsigned long now = millis();
    if (raw != previous[i]) {
      previous[i] = raw;
      changedAt[i] = now;
    }
    if (now - changedAt[i] >= 30 && raw != stable[i]) {
      stable[i] = raw;
      Serial.print(names[i]);
      Serial.println(raw == LOW ? " pressed" : " released");
    }
  }
  delay(1);
}
