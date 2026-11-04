// SoundPod ESP32.
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

// Playlist.
#define OWNER_NAME "Panda"

const char* const songTitleTable[] = {
  "Janie", "Willoughby's Theme", "Fuck Me Eyes", "Nettles",
  "Willoughby's Interlude", "Dust Bowl", "A Knock at the Door",
  "Radio Towers", "Tempest", "Waco, Texas"
};
const char* const songArtistTable[] = {
  "Ethel Cain", "Ethel Cain", "Ethel Cain", "Ethel Cain", "Ethel Cain",
  "Ethel Cain", "Ethel Cain", "Ethel Cain", "Ethel Cain", "Ethel Cain"
};

const int songDurations[] = { 300, 284, 364, 483, 447, 386, 324, 312, 600, 915 };
const int totalTracks = sizeof(songTitleTable) / sizeof(songTitleTable[0]);

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

// Display and audio.
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDRESS 0x3D
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

constexpr uint8_t OLED_SDA = 21;
constexpr uint8_t OLED_SCL = 22;
constexpr uint8_t DF_RX = 16;
constexpr uint8_t DF_TX = 17;
HardwareSerial dfSerial(2);
DFRobotDFPlayerMini dfPlayer;

// Battery readings.
constexpr bool BATTERY_SENSE_CONNECTED = true;
constexpr uint8_t BATTERY_ADC_PIN = 34;
constexpr float BATTERY_CALIBRATION = 1.0f;
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
  if (!BATTERY_SENSE_CONNECTED) return;
  pinMode(BATTERY_ADC_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
  (void)analogReadMilliVolts(BATTERY_ADC_PIN);
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

// Buttons and sleep settings.
#define BTN_PLAY   33
#define BTN_NEXT   19
#define BTN_PREV   23
#define BTN_VOLUP  4
#define BTN_VOLDN  5

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

// Panda and game settings.
constexpr bool ENABLE_PANDA_PET = true;
constexpr int WAVE_BAR_COUNT = 5;
PandaWalker pandaWalker;

constexpr bool ENABLE_BAMBOO_GAME = true;
BambooGame bambooGame;
uint32_t lastGameActivity = 0;
std::atomic<uint8_t> gameHeldButtons{0};

// Playback state.
bool isPlaying = true;
int currentTrack = 1;
int currentVolume = 10;

unsigned long lastAnimUpdate = 0;
const unsigned long ANIM_INTERVAL = 33;
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

// Startup.
void setup() {

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

  btStop();

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

  showMessage(wokeFromIdleSleep ? F("Waking audio...") : F("Starting..."));
  const bool playerReady = initializeAudioPlayer();
  if (!playerReady) showSetupError(F("DFPlayer error!"));
  const uint32_t initializedAt = millis();

  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(OLED_PLAY_CONTRAST);
  if (!wokeFromIdleSleep) {
    startStartupIntroAudio();
    playPandaIntro();
    drainStartupIntroEvents();
  }

  while (millis() - initializedAt < 1500) delay(10);
  showMessage(F("Starting music..."));
  isPlaying = startInitialTrack();
  playbackNeedsSelection = !isPlaying;
  if (!isPlaying) pauseStartTime = millis();

  dfPlayer.begin(dfSerial, false, false);
  beginButtonInput();
  lastButtonActivity = millis();
  Serial.printf("CPU %u MHz. Auto sleep after 10 min paused; wake with Play/Pause.\n",
                getCpuFrequencyMhz());
  lastAnimUpdate = millis();
  updateDisplay();
}

// Audio startup checks.
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

    dfPlayer.begin(dfSerial, true, false);
    dfPlayerSerialReady = true;
    Serial.println(F("Audio wake: selecting SD to exit standby."));
    dfPlayer.outputDevice(DFPLAYER_DEVICE_SD);
    delay(1500);
    if (probeAudioReady()) return true;
    Serial.println(F("Audio wake: no SD response; trying reset recovery."));
  }

  const bool online = dfPlayer.begin(dfSerial, true, true);
  dfPlayerSerialReady = true;
  if (online) return true;

  Serial.println(F("Audio init: waiting for a late SD response."));
  delay(2000);
  if (probeAudioReady()) return true;
  Serial.println(F("Audio init: no SD response. Check power/card/UART; power-cycle to recover."));
  return false;
}

int readStartupPlaybackState() {
  const int reply = dfPlayer.readState();
  const uint8_t command = dfPlayer.readCommand();
  if (reply < 0 || command != 0x42) return -1;
  const int state = reply & 0xFF;
  return state <= 2 ? state : -1;
}

// Startup sound.
void startStartupIntroAudio() {

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

      Serial.println(F("Startup: play sent; status unavailable. Please check audio."));
      return true;
    }

    Serial.println(F("Startup: module still stopped/paused; retrying same track."));
    delay(300);
  }
  Serial.println(F("Startup did not start playback. Press Play to retry or use Next."));
  return false;
}

// Button sampling.
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
    if (routed.toggled) xQueueReset(buttonEvents);
    for (uint8_t i = 0; i < routed.count; ++i) {
      if (xQueueSend(buttonEvents, &routed.events[i], 0) != pdTRUE) {
        Serial.println(F("Button queue full; press could not be saved."));
      }
    }
    vTaskDelayUntil(&lastWake, max((TickType_t)1, pdMS_TO_TICKS(5)));
  }
}

// Game controls.
bool processGameInput() {
  if (!ENABLE_BAMBOO_GAME) return false;
  bool changed = false;
  uint8_t event;
  for (int count = 0; count < 32 && xQueuePeek(buttonEvents, &event, 0) == pdTRUE; ++count) {
    if (event < GameButtonRouter::ENTER) break;
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

  if (xQueuePeek(buttonEvents, &event, 0) != pdTRUE || event != index) return false;
  if (xQueueReceive(buttonEvents, &event, 0) != pdTRUE) return false;
  lastButtonActivity = millis();
  Serial.printf("Button: %u track=%d\n", index, currentTrack);
  return true;
}

// Main player loop.
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
  delay(1);
}

// Track completion.
bool handlePlayerEvents() {
  bool advancePending = false;
  bool needsUpdate = false;
  for (uint8_t count = 0; count < 16 && dfPlayer.available(); count++) {
    const uint8_t type = dfPlayer.readType();
    const uint8_t command = dfPlayer.readCommand();
    const int value = dfPlayer.read();
    if (type == DFPlayerPlayFinished) {
      const uint32_t sinceStart = (uint32_t)millis() - (uint32_t)trackStartTime;

      if (command == 0x3D && isPlaying && !playbackNeedsSelection &&
          sinceStart >= TRACK_FINISH_GUARD_MS && !advancePending) {
        advancePending = true;
        Serial.printf("Track end accepted: selected=%d reported=%d age=%lu ms\n",
                      currentTrack, value, (unsigned long)sinceStart);
      } else {
        Serial.printf("Track end ignored: selected=%d reported=%d age=%lu ms command=0x%02X\n",
                      currentTrack, value, (unsigned long)sinceStart, command);
      }

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
    resetTrackTimer();
    needsUpdate = true;
  }
  return needsUpdate;
}

// Idle sleep and wake.
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

  while (true) {
    checkIdleSleep();
    delay(20);
  }
}

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
    if (digitalRead(pin) == LOW) return;
  }

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
    dfPlayer.sleep();
    dfSerial.flush();
    delay(100);
  }
  if (oledReady) {
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    display.ssd1306_command(SSD1306_CHARGEPUMP);
    display.ssd1306_command(0x10);
  }
  Wire.end();
  dfSerial.end();

  pinMode(DF_TX, OUTPUT);
  digitalWrite(DF_TX, HIGH);
  gpio_hold_en((gpio_num_t)DF_TX);
  gpio_deep_sleep_hold_en();
  Serial.println(F("Sleeping. Press Play/Pause to wake and restart this song."));
  Serial.flush();
  esp_deep_sleep_start();
}

// Intro panda.
void drawPandaFrame(int bob, bool blink, bool noteUp) {
  display.clearDisplay();
  const int cx = SCREEN_WIDTH / 2;
  const int cy = 28 + bob;
  const int noteOffset = noteUp ? 0 : 3;
  drawNote(20, 14 + noteOffset);
  drawNotePair(99, 18 - noteOffset);
  drawStar(17, 48, 4);
  drawStar(111, 48, 3);

  display.fillRoundRect(cx - 21, 41, 42, 20, 9, SSD1306_WHITE);
  display.fillRoundRect(cx - 19, 43, 38, 16, 7, SSD1306_BLACK);
  display.fillRoundRect(cx - 12, 44, 24, 14, 6, SSD1306_WHITE);
  display.fillRoundRect(cx - 22, 53, 15, 10, 4, SSD1306_WHITE);
  display.fillRoundRect(cx + 7, 53, 15, 10, 4, SSD1306_WHITE);
  display.fillRoundRect(cx - 20, 55, 11, 6, 2, SSD1306_BLACK);
  display.fillRoundRect(cx + 9, 55, 11, 6, 2, SSD1306_BLACK);

  display.fillCircle(cx - 18, cy - 16, 8, SSD1306_WHITE);
  display.fillCircle(cx + 18, cy - 16, 8, SSD1306_WHITE);
  display.fillCircle(cx - 18, cy - 16, 6, SSD1306_BLACK);
  display.fillCircle(cx + 18, cy - 16, 6, SSD1306_BLACK);
  display.fillCircle(cx, cy, 22, SSD1306_WHITE);

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

// Welcome and twinkling stars.
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

  introTwinkleStartedAt = millis();
  lastIntroTwinkleFrame = -1;
  updateIntroStars();
  display.display();

  typewriterLine("Welcome to", 1, 5);
  introPause(200);
  char nameBuf[16];
  snprintf(nameBuf, sizeof(nameBuf), "%s's", OWNER_NAME);
  typewriterLine(nameBuf, 2, 25);
  introPause(150);
  typewriterLine("MP3!", 2, 46);
  introPause(1800);
}

// Playback animations.
void drawPlaybackWaveform() {
  constexpr int count = WAVE_BAR_COUNT;
  constexpr int width = 4;
  constexpr int gap = 5;
  constexpr int totalWidth = count * width + (count - 1) * gap;
  constexpr int left = (SCREEN_WIDTH - totalWidth) / 2;

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

void drawPandaPet() {
  if (!ENABLE_PANDA_PET) return;
  const int x = (int)lroundf(pandaWalker.x);
  constexpr int y = 53;
  const bool walking = isPlaying && pandaWalker.moving;
  const bool step = (pandaWalker.stepMs / 160UL) % 2 != 0;

  if (walking) {
    display.fillRoundRect(x + 3, y + 7, 7, 3, 1, SSD1306_WHITE);
    display.drawPixel(x + (step ? 3 : 4), y + 10, SSD1306_WHITE);
    display.drawPixel(x + (step ? 8 : 9), y + 10, SSD1306_WHITE);
  } else {
    display.fillRoundRect(x + 2, y + 7, 9, 4, 2, SSD1306_WHITE);
    display.fillRect(x + 3, y + 9, 2, 1, SSD1306_BLACK);
    display.fillRect(x + 8, y + 9, 2, 1, SSD1306_BLACK);
  }

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

// Battery icon.
void drawBatteryText(const char* text, int x, int y) {
  const uint8_t glyphs[][5] = {
    {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
    {7,4,7,1,7}, {7,4,7,5,7}, {7,1,2,2,2}, {7,5,7,5,7}, {7,5,7,1,7},
    {5,1,2,4,5},
    {0,0,7,0,0},
    {2,2,2,0,2}
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

  display.drawRect(BATTERY_REGION_X, 55, 19, 9, SSD1306_WHITE);
  display.fillRect(BATTERY_REGION_X + 19, 58, 1, 3, SSD1306_WHITE);
  const int textWidth = (int)strlen(label) * 4 - 1;
  drawBatteryText(label, BATTERY_REGION_X + (19 - textWidth) / 2, 57);
}

// Music screen.
void updateDisplay() {
  if (ENABLE_BAMBOO_GAME && bambooGame.active) {
    bambooGame.draw(display, gameHeldButtons.load(std::memory_order_relaxed));
    return;
  }
  static int previousContrast = -1;

  const int targetContrast = OLED_PLAY_CONTRAST;
  if (previousContrast != targetContrast) {
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(targetContrast);
    previousContrast = targetContrast;
  }
  display.clearDisplay();
  display.setTextWrap(false);

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
  display.setFont(nullptr);

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

// Small drawing helpers.
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
