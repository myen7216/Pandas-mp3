// Bare Classic ESP32 (panda board) test. Connect ONLY the USB data cable.
// Open Serial Monitor at 115200 baud. A new count should appear each second.
void setup() {
  Serial.begin(115200);
}

void loop() {
  static unsigned long count = 0;
  Serial.print(F("ESP32 ready: "));
  Serial.println(count++);
  delay(1000);
}
