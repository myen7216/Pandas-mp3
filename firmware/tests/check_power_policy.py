"""Host checks of actual firmware sleep policy and shutdown sequencing, with mocked hardware.
Run: python3 build/ESP32/tests/check_power_policy.py
These checks cannot establish board current, electrical wake behavior or DFPlayer standby.
"""
from pathlib import Path
import re
import subprocess
import tempfile

sketch = Path(__file__).resolve().parents[1] / 'Soundpod_ESP32/Soundpod_ESP32.ino'
source = sketch.read_text()
def function(name):
    match = re.search(r'(?:bool|void) ' + name + r'\([^)]*\) \{', source)
    start = match.start()
    pos = match.end()
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos]
constants = '\n'.join(re.findall(r'constexpr uint32_t (?:IDLE_SLEEP_MS|BUTTON_IDLE_GRACE_MS|SAVED_STATE_MAGIC) = [^;]+;', source))
wake_pin = re.search(r'constexpr gpio_num_t SLEEP_WAKE_PIN = [^;]+;', source).group()
buttons = '\n'.join(re.findall(r'#define BTN_\w+\s+\d+', source))
setup = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include "BambooGame.h"
using std::string;
using std::vector;
#define F(x) x
constexpr int LOW=0, HIGH=1, INPUT_PULLUP=2, OUTPUT=3;
constexpr int RTC_GPIO_MODE_INPUT_ONLY=0, ESP_EXT1_WAKEUP_ALL_LOW=0, ESP_OK=0;
constexpr int SSD1306_DISPLAYOFF=0xAE, SSD1306_CHARGEPUMP=0x8D, DF_TX=17;
using gpio_num_t=int; using esp_err_t=int;
bool isPlaying=true, sleepAvailable=true, oledReady=true, dfPlayerSerialReady=true;
constexpr bool ENABLE_BAMBOO_GAME=true;
BambooGame bambooGame;
uint32_t lastGameActivity=0;
uint32_t pauseStartTime=0, lastButtonActivity=0, clockNow=0, savedStateMagic=0;
int currentTrack=7, currentVolume=13, savedTrack=0, savedVolume=0, wakeResult=0;
int inputs[40];
vector<string> events;
uint32_t millis(){return clockNow;}
int digitalRead(int pin){return inputs[pin];}
void pinMode(int,int){}
void digitalWrite(int,int){}
void delay(int ms){clockNow+=ms;}
void rtc_gpio_init(int){}
void rtc_gpio_set_direction(int,int){}
void rtc_gpio_pullup_en(int){}
void rtc_gpio_pulldown_dis(int){}
void rtc_gpio_deinit(int){}
int esp_sleep_enable_ext1_wakeup_io(uint64_t mask,int mode){
  assert(mask==(1ULL<<BTN_PLAY)); assert(mode==ESP_EXT1_WAKEUP_ALL_LOW); return wakeResult;
}
void gpio_hold_en(int){}
void gpio_deep_sleep_hold_en(){}
struct Sleep {};
void esp_deep_sleep_start(){events.push_back("deep"); throw Sleep{};}
struct Player {void disableDAC(){events.push_back("dac_off");} void sleep(){events.push_back("df_sleep");}} dfPlayer;
struct Display {void ssd1306_command(int cmd){if(cmd==SSD1306_DISPLAYOFF)events.push_back("oled_off");}} display;
struct WireMock {void end(){events.push_back("i2c_end");}} Wire;
struct UART {void flush(){events.push_back("uart_flush");} void end(){events.push_back("uart_end");}} dfSerial;
struct Log {void println(const char*){} void flush(){}} Serial;
'''
checks = r'''
void reset(){
  events.clear(); std::fill(inputs, inputs+40, HIGH);
  isPlaying=false; sleepAvailable=true; oledReady=true; dfPlayerSerialReady=true;
  pauseStartTime=1000; clockNow=pauseStartTime+IDLE_SLEEP_MS;
  lastButtonActivity=1000; wakeResult=0; savedStateMagic=0;
  bambooGame=BambooGame{};lastGameActivity=0;
}
bool attempt(){try{checkIdleSleep();return false;}catch(Sleep&){return true;}}
int main(){
 assert(BTN_PLAY==33 && SLEEP_WAKE_PIN==BTN_PLAY);
 reset(); isPlaying=true; clockNow+=3600000; assert(!attempt()); assert(events.empty());
 reset(); --clockNow; assert(!attempt()); assert(events.empty());
 reset(); assert(attempt()); assert(savedTrack==7 && savedVolume==13 && savedStateMagic==SAVED_STATE_MAGIC);
 assert((events==vector<string>{"dac_off","df_sleep","uart_flush","oled_off","i2c_end","uart_end","deep"}));
 for(int pin : {BTN_PLAY,BTN_NEXT,BTN_PREV,BTN_VOLUP,BTN_VOLDN}){
   reset();inputs[pin]=LOW;assert(!attempt());assert(events.empty());
 }
 reset();lastButtonActivity=clockNow-1999;assert(!attempt());++clockNow;assert(attempt());
 reset();pauseStartTime=UINT32_MAX-1000;clockNow=pauseStartTime+IDLE_SLEEP_MS;
 lastButtonActivity=clockNow-2000; assert(attempt()); // Includes millis rollover.
 reset();wakeResult=-1;assert(!attempt());assert(!sleepAvailable);assert(events.empty());
 reset();bambooGame.enter(42);lastGameActivity=clockNow;
 assert(!attempt());clockNow+=IDLE_SLEEP_MS-1;assert(!attempt());++clockNow;assert(attempt());
 reset();bambooGame.enter(42);bambooGame.over=true;lastGameActivity=clockNow;
 clockNow+=IDLE_SLEEP_MS;assert(attempt());
 reset();oledReady=false;dfPlayerSerialReady=false;assert(attempt());
 assert((events==vector<string>{"i2c_end","uart_end","deep"}));
 // Wake debounce and captured-press activity are checked in check_button_capture.py.
 std::cout<<"PASS: active playback, 10-minute boundary, held buttons, volume grace, rollover, wake failure, partial startup, shutdown ordering.\n";
}
'''
with tempfile.TemporaryDirectory(prefix='soundpod-power-test-') as tmp:
    cpp=Path(tmp)/'checks.cpp'; exe=Path(tmp)/'checks'
    cpp.write_text(buttons+'\n'+setup+'\n'+constants+'\n'+wake_pin+'\n'+function('idleSleepDue')+'\n'+function('checkIdleSleep')+'\n'+checks)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-I',str(sketch.parent),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
