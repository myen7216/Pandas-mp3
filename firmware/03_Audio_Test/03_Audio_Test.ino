// Classic ESP32 (panda board) and DFPlayer only; no OLED or buttons required.
// DFPlayer VCC -> verified USB-derived 5V, GND -> GND, TX -> D16,
// RX <- D17 through 1k ohm. USB power only; no battery connected.
// Put one MP3 at /mp3/0001.mp3 on a FAT32 card while power is OFF.
// DAC_L/DAC_R/GND go to headphone L/R/GND; NOT SPK1/SPK2.
#include <DFRobotDFPlayerMini.h>
HardwareSerial dfSerial(2);
DFRobotDFPlayerMini player;

void setup() {
  Serial.begin(115200);
  dfSerial.begin(9600, SERIAL_8N1, 16, 17);
  delay(500);
  if (!player.begin(dfSerial)) {
    while (true) {
      Serial.println(F("DFPlayer not detected. Check power, crossed RX/TX and SD card."));
      delay(2000);
    }
  }
  player.volume(5);
  player.playMp3Folder(1);
  Serial.println(F("Playing /mp3/0001.mp3 at volume 5/30."));
  Serial.println(F("Serial commands: p=pause, r=resume, +=louder, -=quieter."));
}

void loop() {
  if (Serial.available()) {
    switch (Serial.read()) {
      case 'p': player.pause(); break;
      case 'r': player.start(); break;
      case '+': player.volumeUp(); break;
      case '-': player.volumeDown(); break;
    }
  }
  if (player.available()) {
    uint8_t type = player.readType();
    if (type == DFPlayerPlayFinished) {
      Serial.println(F("Track finished. Test complete; upload again to replay."));
    } else if (type == DFPlayerError) {
      Serial.print(F("DFPlayer error code: "));
      Serial.println(player.read());
    }
  }
}
