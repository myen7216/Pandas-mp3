/**************************************************************************
 Soundpod for Panda's classic ESP32 (ESP32-D0WD-V3, 30-pin USB-C board).
 Based on kondovskii/portable-mp3-player, commit
 2e7a7582d7ba4d36e4c4aca781daac328f22e59b.
 https://github.com/kondovskii/portable-mp3-player
 Original animation preserved in comments; panda intro and elapsed/total time added.

 OLED (Adafruit 938): VIN -> 3V3, GND -> GND, Data -> D21, Clk -> D22.
 Factory I2C address 0x3D; leave Rst, CS, A0/DC and 3Vo unconnected.
 DFPlayer TX -> D16; RX <- D17 through 1k ohm; GND -> GND.
 DFPlayer VCC -> verified USB-derived 5V rail (see START_HERE.md).
 Buttons to GND: play/wake D33, next D19, previous D23, up D4, down D5.
 D5 is a strap pin: leave volume-down released during power-up/reset.
 Board: ESP32 Dev Module; upload and Serial Monitor at 115200 baud.
 Wi-Fi and Bluetooth are not initialized. No panda servos are used.
 **************************************************************************/

#if !defined(CONFIG_IDF_TARGET_ESP32)
#error "Select ESP32 Dev Module for the classic panda ESP32 board."
#endif

#include <math.h>
#include <esp_random.h>
#include "PandaWalker.h"
#include "BatteryGauge.h"
#include "ButtonInput.h"
#include "BambooGame.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <driver/gpio.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Adafruit_SSD1306.h>
#include <DFRobotDFPlayerMini.h>

// ---------- Personalize this ----------
#define OWNER_NAME "Panda"

// ---------- Your tracks ----------
// Ten files: /mp3/0001.mp3 through /mp3/0010.mp3.
// Keep matching entries in all three arrays. Titles are NOT read from MP3 tags.
// A duration of 0 means unknown: no estimated progress bar is shown.
const char* const songTitleTable[] = {
  "Janie", "Willoughby's Theme", "Fuck Me Eyes", "Nettles",
  "Willoughby's Interlude", "Dust Bowl", "A Knock at the Door",
  "Radio Towers", "Tempest", "Waco, Texas"
};
const char* const songArtistTable[] = {
  "Ethel Cain", "Ethel Cain", "Ethel Cain", "Ethel Cain", "Ethel Cain",
  "Ethel Cain", "Ethel Cain", "Ethel Cain", "Ethel Cain", "Ethel Cain"
};
// User-supplied durations in seconds; used for estimated progress only.
const int songDurations[] = { 300, 284, 364, 483, 447, 386, 324, 312, 600, 915 };
const int totalTracks = sizeof(songTitleTable) / sizeof(songTitleTable[0]);
// /mp3/0011.mp3 is a 5.85-second startup jingle, outside the ten-song playlist.
constexpr int STARTUP_INTRO_TRACK = 11;
static_assert(totalTracks == sizeof(songArtistTable) / sizeof(songArtistTable[0]),
              "Add one artist entry per track");
static_assert(totalTracks == sizeof(songDurations) / sizeof(songDurations[0]),
              "Add one duration entry per track");

void getSongTitle(int track, char* outBuf, size_t bufSize) {
  const char* p = songTitleTable[track];
  strncpy(outBuf, p, bufSize - 1);
  outBuf[bufSize - 1] = '\0';
}

void getSongArtist(int track, char* outBuf, size_t bufSize) {
  const char* p = songArtistTable[track];
  strncpy(outBuf, p, bufSize - 1);
  outBuf[bufSize - 1] = '\0';
}

int getSongDuration(int track) {
  return songDurations[track];
}

// ---------- OLED setup ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDRESS 0x3D
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// UART2 stays independent of USB UART0; explicitly set pins for core 3.x.
constexpr uint8_t OLED_SDA = 21;
constexpr uint8_t OLED_SCL = 22;
constexpr uint8_t DF_RX = 16; // ESP32 receive <- DFPlayer TX
constexpr uint8_t DF_TX = 17; // ESP32 transmit -> 1k -> DFPlayer RX
HardwareSerial dfSerial(2);
DFRobotDFPlayerMini dfPlayer;

// ---------- Battery gauge (enable AFTER installing the GPIO34 divider) ----------
constexpr bool BATTERY_SENSE_CONNECTED = true; // Set true once the two 100k resistors + 100nF are wired.
constexpr uint8_t BATTERY_ADC_PIN = 34;
constexpr float BATTERY_CALIBRATION = 1.0f; // Meter voltage / logged raw battery voltage.
constexpr uint32_t BATTERY_READ_INTERVAL_MS = 30000;
constexpr uint32_t BATTERY_SAMPLE_INTERVAL_MS = 5;
constexpr uint8_t BATTERY_SAMPLE_COUNT = 32;
BatteryGauge batteryGauge;
bool batterySampling = false;
uint8_t batterySampleCount = 0;
uint32_t batterySampleSum = 0;
uint32_t lastBatterySample = 0;
uint32_t lastBatteryRead = 0;

void beginBatteryGauge() {
  if (!BATTERY_SENSE_CONNECTED) return; // Floating GPIO34 cannot be auto-detected reliably.
  pinMode(BATTERY_ADC_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db); // Covers the divider's 2.1 V maximum.
  (void)analogReadMilliVolts(BATTERY_ADC_PIN); // Discard initial ADC conversion.
  lastBatterySample = millis();
  batterySampling = true;
}

bool updateBatteryGauge() {
  if (!BATTERY_SENSE_CONNECTED) return false;
  const uint32_t now = millis();
  if (!batterySampling) {
    if (now - lastBatteryRead < BATTERY_READ_INTERVAL_MS) return false;
    batterySampling = true;
    batterySampleCount = 0;
    batterySampleSum = 0;
  }
  if (now - lastBatterySample < BATTERY_SAMPLE_INTERVAL_MS) return false;
  lastBatterySample = now;
  // One conversion per loop, no delays or catch-up burst that could stall animation.
  batterySampleSum += analogReadMilliVolts(BATTERY_ADC_PIN);
  if (++batterySampleCount < BATTERY_SAMPLE_COUNT) return false;
  batterySampling = false;
  lastBatteryRead = now;
  const float rawBatteryMv = 2.0f * batterySampleSum / BATTERY_SAMPLE_COUNT;
  const bool changed = batteryGauge.accept(rawBatteryMv * BATTERY_CALIBRATION);
  Serial.printf("Battery: raw=%.0f mV filtered=%.0f mV percent=%d valid=%d\n",
                rawBatteryMv, batteryGauge.millivolts, batteryGauge.percent, batteryGauge.valid);
  return changed;
}

// ---------- Button pins ----------
#define BTN_PLAY   33
#define BTN_NEXT   19
#define BTN_PREV   23
#define BTN_VOLUP  4
#define BTN_VOLDN  5

// ---------- Power saving ----------
// Move Play/Pause from D18 to D33; the same button now wakes deep sleep.
constexpr uint32_t IDLE_SLEEP_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t BUTTON_IDLE_GRACE_MS = 2000;
constexpr uint8_t CPU_MHZ = 80;
constexpr uint8_t OLED_PLAY_CONTRAST = 0x7F;
constexpr gpio_num_t SLEEP_WAKE_PIN = static_cast<gpio_num_t>(BTN_PLAY);
constexpr uint32_t SAVED_STATE_MAGIC = 0x534F554E;
RTC_DATA_ATTR uint32_t savedStateMagic = 0;
RTC_DATA_ATTR int savedTrack = 1;
RTC_DATA_ATTR int savedVolume = 10;
bool wokeFromIdleSleep = false;
bool sleepAvailable = true;
uint32_t lastButtonActivity = 0;
bool titleNeedsScroll = false;
bool oledReady = false;
bool dfPlayerSerialReady = false;
bool playbackNeedsSelection = false;

// ---------- Reversible animation prototype ----------
constexpr bool ENABLE_PANDA_PET = true; // false hides the pet; the battery keeps its bottom strip.
constexpr int WAVE_BAR_COUNT = 5; // Previously 11: remove three from each side.
PandaWalker pandaWalker; // Position, destination and gait persist across song changes.

// Set false to restore the original controls and hide the game completely.
constexpr bool ENABLE_BAMBOO_GAME = true;
BambooGame bambooGame;
uint32_t lastGameActivity = 0;
std::atomic<uint8_t> gameHeldButtons{0};

// ---------- State ----------
bool isPlaying = true;
int currentTrack = 1;
int currentVolume = 10; // Cold-start headphone volume (range 0-30).

unsigned long lastAnimUpdate = 0;
const unsigned long ANIM_INTERVAL = 33; // Target 30 fps, subject to OLED I2C transfer time.
float wavePhase = 0.0f;

unsigned long volumeShownUntil = 0;
const unsigned long VOLUME_DISPLAY_MS = 2000;

unsigned long lastScrollUpdate = 0;
const unsigned long SCROLL_INTERVAL = 80;
int scrollOffset = 0;

unsigned long trackStartTime = 0;
unsigned long pauseStartTime = 0;
unsigned long totalPausedDuration = 0;
unsigned long lastProgressUpdate = 0;
const unsigned long PROGRESS_UPDATE_INTERVAL = 1000;
// These are full songs (the shortest is 4:44). Ignore late/duplicate finish
// notifications during the first two seconds of any newly selected track.
// Do not use the hand-entered song durations to decide when playback ends.
constexpr uint32_t TRACK_FINISH_GUARD_MS = 2000;

void resetTrackTimer() {
  trackStartTime = millis();
  totalPausedDuration = 0;
}

int getElapsedSeconds() {
  unsigned long now = isPlaying ? millis() : pauseStartTime;
  unsigned long elapsedMs = now - trackStartTime - totalPausedDuration;
  return elapsedMs / 1000;
}

void setup() {
  // Release our held UART output before reinitializing peripherals after sleep.
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)DF_TX);
  rtc_gpio_deinit(SLEEP_WAKE_PIN);
  wokeFromIdleSleep = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1
                     && savedStateMagic == SAVED_STATE_MAGIC;
  if (wokeFromIdleSleep) {
    currentTrack = constrain(savedTrack, 1, totalTracks);
    currentVolume = constrain(savedVolume, 0, 30);
  }
  savedStateMagic = 0;

  // Wi-Fi is never initialized: do not pull its unused driver into this build.
  // No Bluetooth stack is started; Arduino 3.x releases unused BT memory at boot.
  btStop(); // Defensive stop, without initializing a controller or radio.
  // 80 MHz retains the normal 80 MHz peripheral clock for UART/I2C.
  const bool cpuConfigured = setCpuFrequencyMhz(CPU_MHZ);
  Serial.begin(115200);
  if (!cpuConfigured) Serial.println(F("CPU speed unchanged; continuing safely."));
  dfSerial.begin(9600, SERIAL_8N1, DF_RX, DF_TX);

  pinMode(BTN_PLAY, INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLUP);
  pinMode(BTN_VOLUP, INPUT_PULLUP);
  pinMode(BTN_VOLDN, INPUT_PULLUP);

  beginBatteryGauge();

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.beginTransmission(OLED_ADDRESS);
  if (Wire.endTransmission() != 0) {
    showSetupError(F("OLED not found at 0x3D. Run 02_OLED_Test."));
  }
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false)) {
    showSetupError(F("SSD1306 allocation failed"));
  }
  oledReady = true;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Cold start initializes before the intro; deep-sleep wake first recovers
  // the still-powered DFPlayer from standby without demanding a new boot event.
  showMessage(wokeFromIdleSleep ? F("Waking audio...") : F("Starting..."));
  const bool playerReady = initializeAudioPlayer();
  if (!playerReady) showSetupError(F("DFPlayer error!"));
  const uint32_t initializedAt = millis();

  // playCatIntro(); // Original intro: restore the commented functions below first.
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(OLED_PLAY_CONTRAST);
  if (!wokeFromIdleSleep) {
    startStartupIntroAudio();
    playPandaIntro();
    drainStartupIntroEvents();
  }
  // Wake skips the intro, but still needs the same hardware settling opportunity.
  while (millis() - initializedAt < 1500) delay(10);
  showMessage(F("Starting music..."));
  isPlaying = startInitialTrack();
  playbackNeedsSelection = !isPlaying;
  if (!isPlaying) pauseStartTime = millis();

  // begin(false, false) changes the library to no-ACK mode without sending a
  // reset or restarting playback. Startup/wake validation above still uses ACKs.
  // Missing ACKs otherwise block subsequent commands for up to 500 ms.
  dfPlayer.begin(dfSerial, false, false);
  beginButtonInput();
  lastButtonActivity = millis();
  Serial.printf("CPU %u MHz. Auto sleep after 10 min paused; wake with Play/Pause.\n",
                getCpuFrequencyMhz());
  lastAnimUpdate = millis(); // Do not count the startup intro as walking time.
  updateDisplay();
}

// This is a communication/media check, not proof that audio is playing.
// Some modules announce SD-online; others answer only explicit queries on wake.
bool probeAudioReady() {
  for (int attempt = 0; attempt < 3; attempt++) {
    const int files = dfPlayer.readFileCounts(DFPLAYER_DEVICE_SD);
    const uint8_t type = dfPlayer.readType();
    const uint8_t command = dfPlayer.readCommand();
    Serial.printf("Audio init: type=%u command=0x%02X files=%d\n", type, command, files);
    if (type == DFPlayerCardOnline ||
        (type == DFPlayerFeedBack && command == 0x48 && files > 0)) return true;
    delay(250);
  }
  return false;
}

bool initializeAudioPlayer() {
  if (wokeFromIdleSleep) {
    // With doReset=false, begin() only attaches the stream; its true return is
    // NOT hardware confirmation. Validate a real module response below.
    dfPlayer.begin(dfSerial, true, false);
    dfPlayerSerialReady = true;
    Serial.println(F("Audio wake: selecting SD to exit standby."));
    dfPlayer.outputDevice(DFPLAYER_DEVICE_SD);
    delay(1500);
    if (probeAudioReady()) return true;
    Serial.println(F("Audio wake: no SD response; trying reset recovery."));
  }

  // Normal cold initialization, or one bounded reset fallback after wake.
  const bool online = dfPlayer.begin(dfSerial, true, true);
  dfPlayerSerialReady = true;
  if (online) return true;
  // begin() may return on an unrelated event or after its 2-second timeout.
  // Give that SAME reset time to finish rather than immediately resetting again.
  Serial.println(F("Audio init: waiting for a late SD response."));
  delay(2000);
  if (probeAudioReady()) return true;
  Serial.println(F("Audio init: no SD response. Check power/card/UART; power-cycle to recover."));
  return false;
}

// Startup-only status checks: avoid UART polling during normal playback.
// The low byte of the 0x42 reply is 0=stopped, 1=playing, 2=paused.
int readStartupPlaybackState() {
  const int reply = dfPlayer.readState();
  const uint8_t command = dfPlayer.readCommand();
  if (reply < 0 || command != 0x42) return -1; // Ignore stale unrelated replies.
  const int state = reply & 0xFF;
  return state <= 2 ? state : -1;
}

void startStartupIntroAudio() {
  // Configure the output before the visual intro so the jingle begins as soon
  // as the cold-start "Starting..." initialization has completed.
  dfPlayer.outputDevice(DFPLAYER_DEVICE_SD);
  delay(400);
  dfPlayer.enableDAC();
  delay(150);
  dfPlayer.volume(currentVolume);
  delay(150);
  Serial.printf("Startup intro: playing /mp3/%04d.mp3 once\n", STARTUP_INTRO_TRACK);
  dfPlayer.playMp3Folder(STARTUP_INTRO_TRACK);
}

void drainStartupIntroEvents() {
  // The 5.85-second jingle finishes inside the roughly 7.4-second visual intro.
  // Remove its completion/ACK traffic before startup status checks for Janie.
  for (uint8_t count = 0; count < 16 && dfPlayer.available(); count++) {
    const uint8_t type = dfPlayer.readType();
    const uint8_t command = dfPlayer.readCommand();
    const int value = dfPlayer.read();
    if (type == DFPlayerPlayFinished && command == 0x3D) {
      Serial.printf("Startup intro finished: reported=%d\n", value);
    } else if (type == DFPlayerError) {
      Serial.printf("Startup intro DFPlayer error: %d\n", value);
    }
  }
}

bool startInitialTrack() {
  // A reset acknowledgement is not proof that back-to-back audio commands work.
  dfPlayer.outputDevice(DFPLAYER_DEVICE_SD);
  delay(400);
  dfPlayer.enableDAC();
  delay(150);
  dfPlayer.volume(currentVolume);
  delay(150);

  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.printf("Startup: playing /mp3/%04d.mp3, attempt %d\n", currentTrack, attempt);
    dfPlayer.playMp3Folder(currentTrack);
    resetTrackTimer();
    delay(350);
    bool gotStatus = false;
    for (int poll = 0; poll < 3; poll++) {
      const int state = readStartupPlaybackState();
      Serial.printf("Startup playback state: %d (-1 = unavailable)\n", state);
      if (state == 1) {
        Serial.println(F("Startup: DFPlayer reports playing."));
        return true;
      }
      if (state >= 0) gotStatus = true;
      delay(200);
    }
    if (!gotStatus) {
      // Some clones do not implement status queries. Do not repeatedly restart
      // potentially audible music or label an unavailable response as 'stopped'.
      Serial.println(F("Startup: play sent; status unavailable. Please check audio."));
      return true;
    }
    // A valid stopped/paused response permits a bounded retry of this SAME file.
    Serial.println(F("Startup: module still stopped/paused; retrying same track."));
    delay(300);
  }
  Serial.println(F("Startup did not start playback. Press Play to retry or use Next."));
  return false;
}

// The sampler keeps running while the main loop transfers OLED frames or sends
// audio commands. Only the main loop touches the DFPlayer and display.
QueueHandle_t buttonEvents = nullptr;

void sampleButtonsTask(void*) {
  const uint8_t pins[] = {BTN_PLAY, BTN_NEXT, BTN_PREV, BTN_VOLUP, BTN_VOLDN};
  DebouncedButton inputs[5];
  GameButtonRouter router;
  router.enabled = ENABLE_BAMBOO_GAME;
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();
    uint8_t down = 0, pressed = 0;
    for (uint8_t i = 0; i < 5; i++) {
      if (inputs[i].sample(digitalRead(pins[i]) == LOW, now)) pressed |= 1 << i;
      if (inputs[i].armed && inputs[i].stableDown) down |= 1 << i;
    }
    const auto routed = router.sample(down, pressed, now);
    gameHeldButtons.store(routed.held, std::memory_order_relaxed);
    if (routed.toggled) xQueueReset(buttonEvents); // Drop stale actions at the mode boundary.
    for (uint8_t i = 0; i < routed.count; ++i) {
      if (xQueueSend(buttonEvents, &routed.events[i], 0) != pdTRUE) {
        Serial.println(F("Button queue full; press could not be saved."));
      }
    }
    vTaskDelayUntil(&lastWake, max((TickType_t)1, pdMS_TO_TICKS(5)));
  }
}

bool processGameInput() {
  if (!ENABLE_BAMBOO_GAME) return false;
  bool changed = false;
  uint8_t event;
  for (int count = 0; count < 32 && xQueuePeek(buttonEvents, &event, 0) == pdTRUE; ++count) {
    if (event < GameButtonRouter::ENTER) break; // Normal audio controls stay in their queue.
    if (xQueueReceive(buttonEvents, &event, 0) != pdTRUE) break;
    lastButtonActivity = lastGameActivity = millis();
    if (event == GameButtonRouter::ENTER) {
      bambooGame.enter(esp_random());
      lastAnimUpdate = millis();
      Serial.println(F("Bamboo game entered. Hold both volume buttons 5s to exit."));
    } else if (event == GameButtonRouter::EXIT) {
      bambooGame.leave();
      volumeShownUntil = 0;
      lastAnimUpdate = millis();
      Serial.println(F("Bamboo game exited; music state preserved."));
    } else if (event == GameButtonRouter::RESTART && bambooGame.active && bambooGame.over) {
      bambooGame.restart();
    }
    changed = true;
  }
  return changed;
}

void beginButtonInput() {
  buttonEvents = xQueueCreate(32, sizeof(uint8_t));
  if (!buttonEvents || xTaskCreate(sampleButtonsTask, "buttons", 2048, nullptr,
                                 1, nullptr) != pdPASS) {
    showSetupError(F("Button input error"));
  }
}

bool buttonPressed(uint8_t index) {
  uint8_t event;
  // Peek preserves press order, even when several taps were queued during I/O.
  if (xQueuePeek(buttonEvents, &event, 0) != pdTRUE || event != index) return false;
  if (xQueueReceive(buttonEvents, &event, 0) != pdTRUE) return false;
  lastButtonActivity = millis();
  Serial.printf("Button: %u track=%d\n", index, currentTrack);
  return true;
}

void loop() {
  bool needsUpdate = processGameInput();
  const uint8_t gameHeld = gameHeldButtons.load(std::memory_order_relaxed);
  if (bambooGame.active && gameHeld) lastButtonActivity = lastGameActivity = millis();

  if (!bambooGame.active) {
    if (buttonPressed(0)) {
      isPlaying = !isPlaying;
      if (isPlaying) {
        if (playbackNeedsSelection) {
          dfPlayer.playMp3Folder(currentTrack);
          playbackNeedsSelection = false;
          resetTrackTimer();
        } else {
          dfPlayer.start();
          totalPausedDuration += millis() - pauseStartTime;
        }
      } else {
        dfPlayer.pause();
        pauseStartTime = millis();
      }
      needsUpdate = true;
    }

    if (buttonPressed(1)) {
      currentTrack++;
      if (currentTrack > totalTracks) currentTrack = 1;
      Serial.printf("Next: selecting /mp3/%04d.mp3\n", currentTrack);
      dfPlayer.playMp3Folder(currentTrack);
      playbackNeedsSelection = false;
      isPlaying = true;
      scrollOffset = 0;
      resetTrackTimer();
      needsUpdate = true;
    }

    if (buttonPressed(2)) {
      currentTrack--;
      if (currentTrack < 1) currentTrack = totalTracks;
      Serial.printf("Previous: selecting /mp3/%04d.mp3\n", currentTrack);
      dfPlayer.playMp3Folder(currentTrack);
      playbackNeedsSelection = false;
      isPlaying = true;
      scrollOffset = 0;
      resetTrackTimer();
      needsUpdate = true;
    }

    if (buttonPressed(3)) {
      currentVolume = min(currentVolume + 2, 30);
      dfPlayer.volume(currentVolume);
      volumeShownUntil = millis() + VOLUME_DISPLAY_MS;
      needsUpdate = true;
    }

    if (buttonPressed(4)) {
      currentVolume = max(currentVolume - 2, 0);
      dfPlayer.volume(currentVolume);
      volumeShownUntil = millis() + VOLUME_DISPLAY_MS;
      needsUpdate = true;
    }
  }

  if (handlePlayerEvents()) needsUpdate = true;
  if (updateBatteryGauge()) needsUpdate = true;

  const unsigned long animNow = millis();
  if (animNow - lastAnimUpdate >= ANIM_INTERVAL) {
    if (bambooGame.active) {
      bambooGame.update(animNow - lastAnimUpdate, gameHeld);
      if (!bambooGame.over && !(gameHeld & 0x80)) needsUpdate = true;
      // Draw the collision frame once, then leave the frozen screen alone.
      static bool wasGameOver = false;
      if (wasGameOver != bambooGame.over) needsUpdate = true;
      wasGameOver = bambooGame.over;
    } else if (ENABLE_PANDA_PET) {
      pandaWalker.update(animNow - lastAnimUpdate, isPlaying, isPlaying ? esp_random() : 0);
    }
    if (isPlaying && !bambooGame.active) {
      wavePhase = fmodf(wavePhase + (animNow - lastAnimUpdate) * 0.0012f,
                        6.28318530718f);
      needsUpdate = true;
    }
    // Keep the clock current while paused without animating or refreshing bars.
    lastAnimUpdate = animNow;
  }

  if (!bambooGame.active && isPlaying && titleNeedsScroll && millis() - lastScrollUpdate > SCROLL_INTERVAL) {
    lastScrollUpdate = millis();
    scrollOffset--;
    needsUpdate = true;
  }

  if (!bambooGame.active && isPlaying && millis() - lastProgressUpdate > PROGRESS_UPDATE_INTERVAL) {
    lastProgressUpdate = millis();
    needsUpdate = true;
  }

  static bool wasShowingVolume = false;
  bool showingVolumeNow = millis() < volumeShownUntil;
  if (wasShowingVolume && !showingVolumeNow) {
    needsUpdate = true;
  }
  wasShowingVolume = showingVolumeNow;

  if (needsUpdate) {
    updateDisplay();
  }
  checkIdleSleep();
  delay(1); // Short yield keeps controls responsive without limiting frame rate.
}

// Read all pending events before sending the next play command. One burst of
// finish notifications may advance only once; split/delayed duplicates are
// suppressed by the start guard on the next loop. Bound work per frame.
bool handlePlayerEvents() {
  bool advancePending = false;
  bool needsUpdate = false;
  for (uint8_t count = 0; count < 16 && dfPlayer.available(); count++) {
    const uint8_t type = dfPlayer.readType();
    const uint8_t command = dfPlayer.readCommand();
    const int value = dfPlayer.read(); // Snapshot before any outbound command.
    if (type == DFPlayerPlayFinished) {
      const uint32_t sinceStart = (uint32_t)millis() - (uint32_t)trackStartTime;
      // The library also labels USB (0x3C) completion as PlayFinished. We use SD.
      if (command == 0x3D && isPlaying && !playbackNeedsSelection &&
          sinceStart >= TRACK_FINISH_GUARD_MS && !advancePending) {
        advancePending = true;
        Serial.printf("Track end accepted: selected=%d reported=%d age=%lu ms\n",
                      currentTrack, value, (unsigned long)sinceStart);
      } else {
        Serial.printf("Track end ignored: selected=%d reported=%d age=%lu ms command=0x%02X\n",
                      currentTrack, value, (unsigned long)sinceStart, command);
      }
      // Do not require value == currentTrack: some modules report SD/FAT file
      // indices rather than the numbered /mp3 filename used by playMp3Folder().
    } else if (type == DFPlayerCardRemoved) {
      markPlaybackStopped();
      advancePending = false;
      needsUpdate = true;
    } else if (type == DFPlayerError) {
      if (value == Busy || value == FileIndexOut || value == FileMismatch) {
        markPlaybackStopped();
        advancePending = false;
        needsUpdate = true;
      }
      Serial.printf("DFPlayer error: %d\n", value);
    }
  }
  if (advancePending && isPlaying) {
    currentTrack = currentTrack % totalTracks + 1;
    dfPlayer.playMp3Folder(currentTrack);
    playbackNeedsSelection = false;
    scrollOffset = 0;
    resetTrackTimer(); // Arms the guard just as manual selection/startup does.
    needsUpdate = true;
  }
  return needsUpdate;
}

// ---------- Idle shutdown ----------
void markPlaybackStopped() {
  if (isPlaying) {
    isPlaying = false;
    pauseStartTime = millis();
  }
}

void showSetupError(const __FlashStringHelper *message) {
  Serial.println(message);
  if (oledReady) showMessage(message);
  markPlaybackStopped();
  lastButtonActivity = millis();
  // Failed startup must not leave the battery running forever either.
  while (true) {
    checkIdleSleep();
    delay(20);
  }
}

// Checked with unsigned subtraction so the timeout also works across millis() rollover.
bool idleSleepDue(uint32_t now) {
  if (ENABLE_BAMBOO_GAME && bambooGame.active)
    return !isPlaying && now - lastGameActivity >= IDLE_SLEEP_MS;
  return !isPlaying && now - (uint32_t)pauseStartTime >= IDLE_SLEEP_MS
         && now - lastButtonActivity >= BUTTON_IDLE_GRACE_MS;
}

void checkIdleSleep() {
  if (!sleepAvailable || !idleSleepDue(millis())) return;
  const uint8_t buttons[] = {BTN_PLAY, BTN_NEXT, BTN_PREV, BTN_VOLUP, BTN_VOLDN};
  for (uint8_t pin : buttons) {
    if (digitalRead(pin) == LOW) return; // Never enter sleep on a held button.
  }

  // A single active-low RTC pin uses ALL_LOW; RTC pull-up is held by IDF in sleep.
  rtc_gpio_init(SLEEP_WAKE_PIN);
  rtc_gpio_set_direction(SLEEP_WAKE_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en(SLEEP_WAKE_PIN);
  rtc_gpio_pulldown_dis(SLEEP_WAKE_PIN);
  esp_err_t wakeResult = esp_sleep_enable_ext1_wakeup_io(1ULL << SLEEP_WAKE_PIN,
                                                        ESP_EXT1_WAKEUP_ALL_LOW);
  if (wakeResult != ESP_OK) {
    rtc_gpio_deinit(SLEEP_WAKE_PIN);
    pinMode(BTN_PLAY, INPUT_PULLUP);
    sleepAvailable = false;
    Serial.println(F("Sleep wake configuration failed; staying awake."));
    return;
  }

  savedTrack = currentTrack;
  savedVolume = currentVolume;
  savedStateMagic = SAVED_STATE_MAGIC;
  if (dfPlayerSerialReady) {
    dfPlayer.disableDAC();
    delay(50);
    dfPlayer.sleep(); // Command standby; the DFPlayer supply remains connected.
    dfSerial.flush();
    delay(100);
  }
  if (oledReady) {
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    display.ssd1306_command(SSD1306_CHARGEPUMP);
    display.ssd1306_command(0x10); // Disable the OLED charge pump while asleep.
  }
  Wire.end();
  dfSerial.end();

  // Keep DFPlayer RX idle-high, avoiding a floating input during deep sleep.
  pinMode(DF_TX, OUTPUT);
  digitalWrite(DF_TX, HIGH);
  gpio_hold_en((gpio_num_t)DF_TX);
  gpio_deep_sleep_hold_en();
  Serial.println(F("Sleeping. Press Play/Pause to wake and restart this song."));
  Serial.flush();
  esp_deep_sleep_start();
}

// ---------- Original intro retained for restoration ----------
// To restore: uncomment this block and use playCatIntro() in setup()
// instead of playPandaIntro(). The shared welcome and drawing helpers remain active.
/*
// ---------- Cat intro animation ----------
void drawCatFrame(int hipOffset, bool noteUp) {
  display.clearDisplay();

  int cx = SCREEN_WIDTH / 2;
  int cy = 18;

  int nOffset = noteUp ? 0 : 3;
  drawNote(cx - 40, 2 + nOffset);
  drawNotePair(cx + 22, 0 - nOffset);
  drawNote(cx + 40, 20 + nOffset);
  drawStar(10, 50, 5);
  drawStar(SCREEN_WIDTH - 12, 46, 4);

  display.drawCircle(cx, cy, 11, SSD1306_WHITE);

  display.drawTriangle(cx - 9, cy - 8, cx - 13, cy - 18, cx - 3, cy - 11, SSD1306_WHITE);
  display.drawTriangle(cx + 9, cy - 8, cx + 13, cy - 18, cx + 3, cy - 11, SSD1306_WHITE);

  display.fillRoundRect(cx - 13, cy - 21, 26, 3, 1, SSD1306_WHITE);
  display.drawLine(cx - 13, cy - 19, cx - 15, cy - 3, SSD1306_WHITE);
  display.drawLine(cx + 13, cy - 19, cx + 15, cy - 3, SSD1306_WHITE);
  display.fillRoundRect(cx - 18, cy - 7, 6, 11, 2, SSD1306_WHITE);
  display.fillRoundRect(cx + 12, cy - 7, 6, 11, 2, SSD1306_WHITE);

  display.drawLine(cx - 7, cy - 1, cx - 4, cy - 3, SSD1306_WHITE);
  display.drawLine(cx - 4, cy - 3, cx - 1, cy - 1, SSD1306_WHITE);
  display.drawLine(cx + 1, cy - 1, cx + 4, cy - 3, SSD1306_WHITE);
  display.drawLine(cx + 4, cy - 3, cx + 7, cy - 1, SSD1306_WHITE);

  display.drawPixel(cx, cy + 2, SSD1306_WHITE);
  display.drawLine(cx - 3, cy + 5, cx, cy + 3, SSD1306_WHITE);
  display.drawLine(cx, cy + 3, cx + 3, cy + 5, SSD1306_WHITE);

  display.drawLine(cx - 10, cy + 1, cx - 18, cy - 1, SSD1306_WHITE);
  display.drawLine(cx - 10, cy + 3, cx - 18, cy + 3, SSD1306_WHITE);
  display.drawLine(cx + 10, cy + 1, cx + 18, cy - 1, SSD1306_WHITE);
  display.drawLine(cx + 10, cy + 3, cx + 18, cy + 3, SSD1306_WHITE);

  int bx = cx + hipOffset;
  int by = cy + 15;

  display.drawRoundRect(bx - 10, by, 20, 18, 7, SSD1306_WHITE);

  display.drawLine(bx - 9, by + 2, bx - 15, by - 6, SSD1306_WHITE);
  display.fillCircle(bx - 16, by - 8, 2, SSD1306_WHITE);

  display.fillRoundRect(bx - 8, by + 16, 6, 9, 2, SSD1306_WHITE);
  display.fillRoundRect(bx + 3, by + 16, 6, 9, 2, SSD1306_WHITE);

  int tx = bx + 10;
  display.drawLine(tx, by + 10, tx + 8 - hipOffset, by + 5, SSD1306_WHITE);
  display.drawLine(tx + 8 - hipOffset, by + 5, tx + 6 - hipOffset, by - 3, SSD1306_WHITE);

  display.display();
}

void playCatIntro() {
  int offsets[] = {-4, 0, 4, 0};
  bool noteToggle = false;

  for (int cycle = 0; cycle < 3; cycle++) {
    for (int i = 0; i < 4; i++) {
      drawCatFrame(offsets[i], noteToggle);
      noteToggle = !noteToggle;
      delay(220);
    }
  }

  drawCatFrame(0, true);
  delay(500);

  display.clearDisplay();
  display.display();
  delay(200);

  typewriterWelcome();
}
*/

// ---------- Panda intro animation ----------
void drawPandaFrame(int bob, bool blink, bool noteUp) {
  display.clearDisplay();
  const int cx = SCREEN_WIDTH / 2;
  const int cy = 28 + bob;
  const int noteOffset = noteUp ? 0 : 3;
  drawNote(20, 14 + noteOffset);
  drawNotePair(99, 18 - noteOffset);
  drawStar(17, 48, 4);
  drawStar(111, 48, 3);

  // Compact seated body, white belly and dark paws outlined against the OLED.
  display.fillRoundRect(cx - 21, 41, 42, 20, 9, SSD1306_WHITE);
  display.fillRoundRect(cx - 19, 43, 38, 16, 7, SSD1306_BLACK);
  display.fillRoundRect(cx - 12, 44, 24, 14, 6, SSD1306_WHITE);
  display.fillRoundRect(cx - 22, 53, 15, 10, 4, SSD1306_WHITE);
  display.fillRoundRect(cx + 7, 53, 15, 10, 4, SSD1306_WHITE);
  display.fillRoundRect(cx - 20, 55, 11, 6, 2, SSD1306_BLACK);
  display.fillRoundRect(cx + 9, 55, 11, 6, 2, SSD1306_BLACK);

  // Round black ears with a white rim, behind a large white face.
  display.fillCircle(cx - 18, cy - 16, 8, SSD1306_WHITE);
  display.fillCircle(cx + 18, cy - 16, 8, SSD1306_WHITE);
  display.fillCircle(cx - 18, cy - 16, 6, SSD1306_BLACK);
  display.fillCircle(cx + 18, cy - 16, 6, SSD1306_BLACK);
  display.fillCircle(cx, cy, 22, SSD1306_WHITE);

  // Dark eye patches distinguish the panda even on a monochrome display.
  display.fillCircle(cx - 9, cy - 2, 7, SSD1306_BLACK);
  display.fillCircle(cx + 9, cy - 2, 7, SSD1306_BLACK);
  if (blink) {
    display.drawLine(cx - 12, cy - 1, cx - 7, cy - 1, SSD1306_WHITE);
    display.drawLine(cx + 7, cy - 1, cx + 12, cy - 1, SSD1306_WHITE);
  } else {
    display.fillCircle(cx - 8, cy - 3, 2, SSD1306_WHITE);
    display.fillCircle(cx + 8, cy - 3, 2, SSD1306_WHITE);
  }
  display.fillTriangle(cx - 3, cy + 6, cx + 3, cy + 6,
                       cx, cy + 9, SSD1306_BLACK);
  display.drawLine(cx, cy + 9, cx, cy + 12, SSD1306_BLACK);
  display.drawLine(cx, cy + 12, cx - 4, cy + 10, SSD1306_BLACK);
  display.drawLine(cx, cy + 12, cx + 4, cy + 10, SSD1306_BLACK);
  display.display();
}

void playPandaIntro() {
  const int bobs[] = {0, -1, 0, 1};
  for (int cycle = 0; cycle < 3; cycle++) {
    for (int frame = 0; frame < 4; frame++) {
      drawPandaFrame(bobs[frame], frame == 3, frame % 2 == 0);
      delay(220);
    }
  }
  drawPandaFrame(0, false, true);
  delay(500);
  display.clearDisplay();
  display.display();
  delay(200);
  typewriterWelcome();
}

// ---------- Previous welcome preserved for restoration ----------
// void typewriterWelcome() {
//   display.clearDisplay();
// 
//   drawStar(8, 8, 5);
//   drawStar(SCREEN_WIDTH - 10, 10, 5);
//   drawStar(10, SCREEN_HEIGHT - 10, 5);
//   drawStar(SCREEN_WIDTH - 10, SCREEN_HEIGHT - 10, 5);
//   display.display();
// 
//   display.setTextSize(1);
//   display.setCursor(24, 6);
//   const char* line1 = "Welcome to";
//   for (int i = 0; line1[i] != '\0'; i++) {
//     display.print(line1[i]);
//     display.display();
//     delay(45);
//   }
//   delay(200);
// 
//   display.setTextSize(2);
//   char nameBuf[16];
//   snprintf(nameBuf, sizeof(nameBuf), "%s's", OWNER_NAME);
//   int nameChars = strlen(nameBuf);
//   int nameX = (SCREEN_WIDTH - (nameChars * 12)) / 2;
//   display.setCursor(nameX, 26);
//   for (int i = 0; i < nameChars; i++) {
//     display.print(nameBuf[i]);
//     display.display();
//     delay(70);
//   }
//   delay(150);
// 
//   const char* line3 = "MP3!";
//   int line3X = (SCREEN_WIDTH - (4 * 12)) / 2;
//   display.setCursor(line3X, 46);
//   for (int i = 0; line3[i] != '\0'; i++) {
//     display.print(line3[i]);
//     display.display();
//     delay(70);
//   }
// 
//   delay(1800);
// }

// Star animation is confined to the welcome; normal playback/sleep is unchanged.
uint32_t introTwinkleStartedAt = 0;
int32_t lastIntroTwinkleFrame = -1;

bool updateIntroStars() {
  const uint32_t frame = (millis() - introTwinkleStartedAt) / 120;
  if ((int32_t)frame == lastIntroTwinkleFrame) return false;
  lastIntroTwinkleFrame = frame;
  const int x[] = {10, 116, 109, 20};
  const int y[] = {28, 20, 52, 56};
  const int maxSize[] = {4, 3, 4, 2};
  const int pulse[] = {0, 1, 2, 3, 2, 1};
  for (int i = 0; i < 4; i++) {
    const int radius = maxSize[i];
    // Only erase this star's reserved area, preserving already typed letters.
    display.fillRect(x[i] - radius, y[i] - radius,
                     2 * radius + 1, 2 * radius + 1, SSD1306_BLACK);
    const int size = (radius * pulse[(frame + i * 2) % 6] + 1) / 3;
    if (size == 0) display.drawPixel(x[i], y[i], SSD1306_WHITE);
    else drawStar(x[i], y[i], size);
  }
  return true;
}

void introPause(uint32_t durationMs) {
  const uint32_t startedAt = millis();
  while (millis() - startedAt < durationMs) {
    if (updateIntroStars()) display.display();
    delay(5);
  }
}

void typewriterLine(const char* text, uint8_t size, int y) {
  display.setTextSize(size);
  display.setCursor((SCREEN_WIDTH - (int)strlen(text) * 6 * size) / 2, y);
  for (int i = 0; text[i] != '\0'; i++) {
    display.print(text[i]);
    display.display();
    introPause(size == 1 ? 45 : 70);
  }
}

void typewriterWelcome() {
  display.clearDisplay();
  display.setFont(nullptr);
  display.setTextWrap(false);

  // Same side positions, now twinkling at staggered phases.
  introTwinkleStartedAt = millis();
  lastIntroTwinkleFrame = -1;
  updateIntroStars();
  display.display();

  // Previous wording, kept here for easy restoration:
  // typewriterLine("Meow and thick", 1, 3);
  // introPause(150);
  // typewriterLine("presents:", 1, 15);
  // introPause(200);

  typewriterLine("Welcome to", 1, 5);
  introPause(200);
  char nameBuf[16];
  snprintf(nameBuf, sizeof(nameBuf), "%s's", OWNER_NAME);
  typewriterLine(nameBuf, 2, 25);
  introPause(150);
  typewriterLine("MP3!", 2, 46);
  introPause(1800);
}

// ---------- Main screen ----------
// ---------- Previous playback layout preserved for restoration ----------
// void updateDisplay() {
//   display.clearDisplay();
// 
//   int noteY = (isPlaying && noteBounceUp) ? 0 : 3;
//   drawNote(SCREEN_WIDTH / 2 - 4, noteY);
// 
//   char title[64];
//   getSongTitle(currentTrack - 1, title, sizeof(title));
//   int titleWidthPx = strlen(title) * 6;
// 
//   display.setTextSize(1);
//   display.setTextWrap(false); // Scroll long titles without spilling into the artist row.
//   if (titleWidthPx <= SCREEN_WIDTH) {
//     int titleX = (SCREEN_WIDTH - titleWidthPx) / 2;
//     display.setCursor(titleX, 14);
//     display.print(title);
//   } else {
//     int gap = 20;
//     int totalScrollWidth = titleWidthPx + gap;
//     if (scrollOffset <= -totalScrollWidth) scrollOffset = 0;
//     display.setCursor(scrollOffset, 14);
//     display.print(title);
//     display.setCursor(scrollOffset + totalScrollWidth, 14);
//     display.print(title);
//   }
// 
//   char artistBuf[22];
//   getSongArtist(currentTrack - 1, artistBuf, sizeof(artistBuf));
//   int maxArtistChars = SCREEN_WIDTH / 6;
//   if ((int)strlen(artistBuf) > maxArtistChars) {
//     artistBuf[maxArtistChars - 3] = '\0';
//     strcat(artistBuf, "...");
//   }
//   int artistWidthPx = strlen(artistBuf) * 6;
//   int artistX = (SCREEN_WIDTH - artistWidthPx) / 2;
//   display.setCursor(artistX, 25);
//   display.print(artistBuf);
// 
//   int elapsed = getElapsedSeconds();
//   int duration = getSongDuration(currentTrack - 1);
//   if (duration > 0 && elapsed > duration) elapsed = duration;
//   float fraction = duration > 0 ? (float)elapsed / (float)duration : 0.0f;
// 
//   int barX = 10;
//   int barY = 35;
//   int barWidth = SCREEN_WIDTH - 20;
//   int barHeight = 3;
//   if (duration > 0) display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
//   int fillWidth = (int)(barWidth * fraction);
//   if (fillWidth > 0) {
//     display.fillRect(barX, barY, fillWidth, barHeight, SSD1306_WHITE);
//   }
// 
//   char statusBuf[24];
//   sprintf(statusBuf, "Track %d - %s", currentTrack, isPlaying ? "Playing" : "Paused");
//   int statusWidthPx = strlen(statusBuf) * 6;
//   int statusX = (SCREEN_WIDTH - statusWidthPx) / 2;
//   display.setCursor(statusX, 42);
//   display.print(statusBuf);
// 
//   /* Original bottom row, retained if the time display layout is reverted.
//   if (millis() < volumeShownUntil) {
//     char volBuf[16];
//     sprintf(volBuf, "Volume: %d", currentVolume);
//     int volWidthPx = strlen(volBuf) * 6;
//     int volX = (SCREEN_WIDTH - volWidthPx) / 2;
//     display.setCursor(volX, 54);
//     display.print(volBuf);
//   }
//   */
// 
//   // The same pause-aware timer drives both the bar and the elapsed time.
//   // Durations are supplied above, not queried from the audio file.
//   char timeBuf[24];
//   if (duration > 0) {
//     snprintf(timeBuf, sizeof(timeBuf), "%d:%02d / %d:%02d",
//              elapsed / 60, elapsed % 60, duration / 60, duration % 60);
//   } else {
//     snprintf(timeBuf, sizeof(timeBuf), "%d:%02d / --:--",
//              elapsed / 60, elapsed % 60);
//   }
//   const int timeX = SCREEN_WIDTH - 4 - strlen(timeBuf) * 6;
//   display.setCursor(timeX, 54);
//   display.print(timeBuf);
// 
//   // "Vol:30" leaves a gap even beside "15:15 / 15:15" on this 128px screen.
//   if (millis() < volumeShownUntil) {
//     display.setCursor(4, 54);
//     display.print(F("Vol:"));
//     display.print(currentVolume);
//   }
// 
//   display.display();
// }

// Decorative waveform: no audio-level samples are supplied by the DFPlayer.
// Bar widths, spacing and vertical midpoint stay fixed in every frame.
void drawPlaybackWaveform() {
  constexpr int count = WAVE_BAR_COUNT;
  constexpr int width = 4;
  constexpr int gap = 5;
  constexpr int totalWidth = count * width + (count - 1) * gap;
  constexpr int left = (SCREEN_WIDTH - totalWidth) / 2;
  // Keep the bottom 11px free for the panda and battery, even if the pet is off.
  constexpr int centerY = 46;
  constexpr int heightRange = 10;
  for (int i = 0; i < count; i++) {
    int height = 4;
    if (isPlaying) {
      const float breath = 0.75f + 0.25f * sinf(wavePhase);
      const float wave = 0.5f + 0.32f * sinf(2.0f * wavePhase - i * 0.65f)
                              + 0.18f * sinf(3.0f * wavePhase + i * 0.95f);
      height = 4 + (int)lroundf(heightRange * breath * wave);
    }
    display.fillRoundRect(left + i * (width + gap), centerY - height / 2,
                          width, height, 1, SSD1306_WHITE);
  }
}

// Tiny 13x11 panda prototype, drawn below all text and the progress/wave bars.
void drawPandaPet() {
  if (!ENABLE_PANDA_PET) return;
  const int x = (int)lroundf(pandaWalker.x);
  constexpr int y = 53;
  const bool walking = isPlaying && pandaWalker.moving;
  const bool step = (pandaWalker.stepMs / 160UL) % 2 != 0;

  // Seated haunches or a narrow body with alternating tiny feet.
  if (walking) {
    display.fillRoundRect(x + 3, y + 7, 7, 3, 1, SSD1306_WHITE);
    display.drawPixel(x + (step ? 3 : 4), y + 10, SSD1306_WHITE);
    display.drawPixel(x + (step ? 8 : 9), y + 10, SSD1306_WHITE);
  } else {
    display.fillRoundRect(x + 2, y + 7, 9, 4, 2, SSD1306_WHITE);
    display.fillRect(x + 3, y + 9, 2, 1, SSD1306_BLACK);
    display.fillRect(x + 8, y + 9, 2, 1, SSD1306_BLACK);
  }

  // Round ears, white face and black eye patches echo the larger intro panda.
  display.fillCircle(x + 2, y + 2, 2, SSD1306_WHITE);
  display.fillCircle(x + 10, y + 2, 2, SSD1306_WHITE);
  display.fillCircle(x + 2, y + 2, 1, SSD1306_BLACK);
  display.fillCircle(x + 10, y + 2, 1, SSD1306_BLACK);
  display.fillCircle(x + 6, y + 4, 4, SSD1306_WHITE);
  display.fillCircle(x + 4, y + 4, 1, SSD1306_BLACK);
  display.fillCircle(x + 8, y + 4, 1, SSD1306_BLACK);
  const int look = walking ? (pandaWalker.rightward ? 1 : -1) : 0;
  display.drawPixel(x + 4 + look, y + 4, SSD1306_WHITE);
  display.drawPixel(x + 8 + look, y + 4, SSD1306_WHITE);
  display.drawPixel(x + 6, y + 6, SSD1306_BLACK);
}

// Compact 3x5 glyphs keep the battery small without changing the other UI fonts.
void drawBatteryText(const char* text, int x, int y) {
  const uint8_t glyphs[][5] = {
    {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
    {7,4,7,1,7}, {7,4,7,5,7}, {7,1,2,2,2}, {7,5,7,5,7}, {7,5,7,1,7},
    {5,1,2,4,5}, // %
    {0,0,7,0,0}, // -
    {2,2,2,0,2}  // !
  };
  for (; *text; ++text, x += 4) {
    const int index = *text >= '0' && *text <= '9' ? *text - '0' :
                      *text == '%' ? 10 : *text == '-' ? 11 : 12;
    for (int row = 0; row < 5; ++row) {
      for (int col = 0; col < 3; ++col) {
        if (glyphs[index][row] & (1 << (2 - col)))
          display.drawPixel(x + col, y + row, SSD1306_WHITE);
      }
    }
  }
}

void drawBatteryIndicator() {
  char label[6];
  if (batteryGauge.valid)
    snprintf(label, sizeof(label), batteryGauge.low ? "%d%%!" : "%d%%", batteryGauge.percent);
  else snprintf(label, sizeof(label), "--%%");
  // 19x9 body + 1px terminal. Blank rows 56 and 62 pad the 5px text.
  display.drawRect(BATTERY_REGION_X, 55, 19, 9, SSD1306_WHITE);
  display.fillRect(BATTERY_REGION_X + 19, 58, 1, 3, SSD1306_WHITE);
  const int textWidth = (int)strlen(label) * 4 - 1;
  drawBatteryText(label, BATTERY_REGION_X + (19 - textWidth) / 2, 57);
}

void updateDisplay() {
  if (ENABLE_BAMBOO_GAME && bambooGame.active) {
    bambooGame.draw(display, gameHeldButtons.load(std::memory_order_relaxed));
    return;
  }
  static int previousContrast = -1;
  // Pausing changes the animation state, but leaves screen brightness unchanged.
  const int targetContrast = OLED_PLAY_CONTRAST;
  if (previousContrast != targetContrast) {
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(targetContrast);
    previousContrast = targetContrast;
  }
  display.clearDisplay();
  display.setTextWrap(false);

  // A proportional 9pt title sits between the old 2x font and the small artist font.
  char title[64];
  getSongTitle(currentTrack - 1, title, sizeof(title));
  display.setTextSize(1);
  display.setFont(&FreeSans9pt7b);
  int16_t titleXOffset, titleYOffset;
  uint16_t titleWidth, titleHeight;
  display.getTextBounds(title, 0, 0, &titleXOffset, &titleYOffset,
                        &titleWidth, &titleHeight);
  constexpr int titleBaseline = 13;
  titleNeedsScroll = titleWidth > SCREEN_WIDTH;
  if (!titleNeedsScroll) {
    display.setCursor((SCREEN_WIDTH - titleWidth) / 2 - titleXOffset, titleBaseline);
    display.print(title);
  } else {
    const int totalScrollWidth = titleWidth + 24;
    if (scrollOffset <= -totalScrollWidth) scrollOffset = 0;
    display.setCursor(scrollOffset - titleXOffset, titleBaseline);
    display.print(title);
    display.setCursor(scrollOffset + totalScrollWidth - titleXOffset, titleBaseline);
    display.print(title);
  }
  display.setFont(nullptr); // Restore the compact font for all other rows.

  // Artist stays smaller and remains visible during volume changes.
  display.setTextSize(1);
  char subtitle[22];
  getSongArtist(currentTrack - 1, subtitle, sizeof(subtitle));
  if (strlen(subtitle) > SCREEN_WIDTH / 6) {
    subtitle[18] = '\0';
    strcat(subtitle, "...");
  }
  display.setCursor((SCREEN_WIDTH - (int)strlen(subtitle) * 6) / 2, 19);
  display.print(subtitle);

  int elapsed = getElapsedSeconds();
  const int duration = getSongDuration(currentTrack - 1);
  if (duration > 0 && elapsed > duration) elapsed = duration;
  constexpr int barX = 6;
  constexpr int barY = 36;
  constexpr int barWidth = SCREEN_WIDTH - 2 * barX;
  constexpr int barHeight = 3;

  char elapsedBuf[16];
  char durationBuf[16];
  snprintf(elapsedBuf, sizeof(elapsedBuf), "%d:%02d", elapsed / 60, elapsed % 60);
  if (duration > 0) {
    snprintf(durationBuf, sizeof(durationBuf), "%d:%02d", duration / 60, duration % 60);
  } else {
    snprintf(durationBuf, sizeof(durationBuf), "--:--");
  }
  display.setCursor(barX, 27);
  display.print(elapsedBuf);
  display.setCursor(barX + barWidth - strlen(durationBuf) * 6, 27);
  display.print(durationBuf);

  display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
  if (duration > 0) {
    const int fillWidth = (barWidth - 2) * elapsed / duration;
    if (fillWidth > 0) display.fillRect(barX + 1, barY + 1, fillWidth, 1, SSD1306_WHITE);
  }

  // Each volume press restarts the existing two-second feedback timeout.
  if (millis() < volumeShownUntil) {
    char volumeBuf[16];
    snprintf(volumeBuf, sizeof(volumeBuf), "Volume: %d", currentVolume);
    display.setCursor((SCREEN_WIDTH - (int)strlen(volumeBuf) * 6) / 2,
                      43);
    display.print(volumeBuf);
  } else {
    drawPlaybackWaveform();
  }
  drawPandaPet();
  drawBatteryIndicator();
  display.display();
}

// ---------- Decorative shapes ----------

void drawNote(int x, int y) {
  display.fillCircle(x, y + 6, 2, SSD1306_WHITE);
  display.drawLine(x + 2, y + 6, x + 2, y, SSD1306_WHITE);
  display.drawLine(x + 2, y, x + 5, y + 2, SSD1306_WHITE);
}

void drawNotePair(int x, int y) {
  display.fillCircle(x, y + 10, 2, SSD1306_WHITE);
  display.fillCircle(x + 7, y + 10, 2, SSD1306_WHITE);
  display.drawLine(x + 2, y + 10, x + 2, y, SSD1306_WHITE);
  display.drawLine(x + 9, y + 10, x + 9, y, SSD1306_WHITE);
  display.drawLine(x + 2, y, x + 9, y, SSD1306_WHITE);
}

void drawStar(int cx, int cy, int size) {
  display.drawLine(cx, cy - size, cx, cy + size, SSD1306_WHITE);
  display.drawLine(cx - size, cy, cx + size, cy, SSD1306_WHITE);
  int diag = size / 2;
  display.drawLine(cx - diag, cy - diag, cx + diag, cy + diag, SSD1306_WHITE);
  display.drawLine(cx - diag, cy + diag, cx + diag, cy - diag, SSD1306_WHITE);
}

void showMessage(const __FlashStringHelper *msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 28);
  display.println(msg);
  display.display();
}
