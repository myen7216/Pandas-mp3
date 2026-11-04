"""Exercise the production OLED guard, including the game's template renderer."""
from pathlib import Path
import re
import subprocess
import tempfile

folder = Path(__file__).resolve().parents[1] / 'Soundpod_ESP32'
source = (folder / 'Soundpod_ESP32.ino').read_text()
start = source.index('class SoundpodDisplay :')
end = source.index('\nSoundpodDisplay display(', start)
display_class = source[start:end]
match = re.search(r'void maintainDisplayOrientation\(\) \{.*?\n\}', source, re.S)
assert match
assert 'currentVolume = constrain(savedVolume, 0, MAX_VOLUME);' in source
assert 'int currentVolume = DEFAULT_VOLUME;' in source
assert 'RTC_DATA_ATTR int savedVolume = DEFAULT_VOLUME;' in source

program = r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include <iostream>
#include "BambooGame.h"
constexpr uint8_t SSD1306_SEGREMAP=0xA0, SSD1306_COMSCANDEC=0xC8;
std::vector<int> events;
uint32_t clockNow=0;
uint32_t millis(){return clockNow;}
bool oledReady=false;
struct Adafruit_SSD1306 {
 int rotation=2, remap=0xA0, scan=0xC0;
 void setRotation(int value){rotation=value;events.push_back(value);}
 void ssd1306_command(int value){
   if(value==0xA1) remap=value;
   if(value==0xC8) scan=value;
   events.push_back(value);
 }
 void display(){assert(rotation==0 && remap==0xA1 && scan==0xC8);events.push_back(999);}
 template<class... T>void clearDisplay(T...){}
 template<class... T>void setFont(T...){}
 template<class... T>void setTextSize(T...){}
 template<class... T>void setTextColor(T...){}
 template<class... T>void setTextWrap(T...){}
 template<class... T>void setCursor(T...){}
 template<class... T>void print(T...){}
 template<class... T>void drawLine(T...){}
 template<class... T>void drawPixel(T...){}
 template<class... T>void fillTriangle(T...){}
 template<class... T>void fillRoundRect(T...){}
 template<class... T>void fillRect(T...){}
 template<class... T>void fillCircle(T...){}
 template<class... T>void drawCircle(T...){}
};
DISPLAY_CLASS
SoundpodDisplay display;
MAINTENANCE
int main(){
 display.display();assert((events==std::vector<int>{0,0xA1,0xC8,999}));
 // Corrupt both software and panel orientation between ordinary frames.
 events.clear();display.rotation=2;display.remap=0xA0;display.scan=0xC0;
 display.display();assert((events==std::vector<int>{0,0xA1,0xC8,999}));
 // Game and frozen game-over frames use the same guarded display method.
 BambooGame game;game.enter(42);events.clear();game.draw(display,0);
 assert((events==std::vector<int>{0,0xA1,0xC8,999}));
 game.over=true;events.clear();game.draw(display,0);
 assert((events==std::vector<int>{0,0xA1,0xC8,999}));
 // Idle refresh does not transfer/redraw a framebuffer or touch brightness.
 events.clear();clockNow=1000;maintainDisplayOrientation();assert(events.empty());
 oledReady=true;maintainDisplayOrientation();assert((events==std::vector<int>{0,0xA1,0xC8}));
 events.clear();clockNow=1999;maintainDisplayOrientation();assert(events.empty());
 clockNow=2000;maintainDisplayOrientation();assert(events.size()==3);
 events.clear();clockNow=UINT32_MAX-500;maintainDisplayOrientation();assert(events.size()==3);
 events.clear();clockNow=499;maintainDisplayOrientation();assert(events.size()==3);
 std::cout<<"PASS: orientation restored before normal/game frames, idle cadence, no contrast/frame side effects, rollover, quiet startup/wake limits.\n";
}
'''.replace('DISPLAY_CLASS', display_class).replace('MAINTENANCE', match.group())
with tempfile.TemporaryDirectory(prefix='panda-display-') as tmp:
    cpp = Path(tmp) / 'checks.cpp'
    exe = Path(tmp) / 'checks'
    cpp.write_text(program)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-I', str(folder), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
