"""Exercise production button sampler/queue across blocked UI and bouncing contacts."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'Soundpod_ESP32/Soundpod_ESP32.ino').read_text()
def extract(name, text=source):
    m = re.search(r'(?:void|bool) ' + name + r'\([^)]*\) \{', text)
    end, depth = m.end(), 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[m.start():end]
program = r'''
#include <cassert>
#include <cstdint>
#include <deque>
#include <vector>
#include <algorithm>
#include <iostream>
#include <atomic>
#include "ButtonInput.h"
using std::max;
#define F(x) x
constexpr int LOW=0, HIGH=1, pdTRUE=1;
constexpr int BTN_PLAY=33, BTN_NEXT=19, BTN_PREV=23, BTN_VOLUP=4, BTN_VOLDN=5;
using TickType_t=uint32_t;
constexpr bool ENABLE_BAMBOO_GAME=true;
std::atomic<uint8_t> gameHeldButtons{0};
uint32_t now=0, until=0, lastButtonActivity=0;
int currentTrack=1;
struct Interval {int pin;uint32_t from,to;};
std::vector<Interval> lows;
std::deque<uint8_t> queue;
auto buttonEvents=&queue;
uint32_t millis(){return now;}
int digitalRead(int pin){for(auto i:lows)if(i.pin==pin && now>=i.from && now<i.to)return LOW;return HIGH;}
int xQueueSend(std::deque<uint8_t>* q,const uint8_t* i,int){if(q->size()==32)return 0;q->push_back(*i);return pdTRUE;}
void xQueueReset(std::deque<uint8_t>* q){q->clear();}
int xQueuePeek(std::deque<uint8_t>* q,uint8_t* i,int){if(q->empty())return 0;*i=q->front();return pdTRUE;}
int xQueueReceive(std::deque<uint8_t>* q,uint8_t* i,int){if(!xQueuePeek(q,i,0))return 0;q->pop_front();return pdTRUE;}
struct Log {void println(const char*){} template<class... T>void printf(const char*,T...) {}} Serial;
TickType_t xTaskGetTickCount(){return now;}
TickType_t pdMS_TO_TICKS(int n){return n;}
struct End {};
void vTaskDelayUntil(TickType_t* wake,TickType_t dt){*wake+=dt;now=*wake;if(now>=until)throw End{};}
FUNCTIONS
void sampleFor(uint32_t end,std::vector<Interval> edges){now=0;until=end;lows=edges;queue.clear();try{sampleButtonsTask(nullptr);}catch(End&) {}}
int main(){
 // Main UI blocked for 500 ms; three complete Next taps must survive it.
 sampleFor(500,{{BTN_NEXT,50,110},{BTN_NEXT,170,230},{BTN_NEXT,320,380}});
 assert(queue.size()==3);
 for(int i=0;i<3;i++){assert(!buttonPressed(0));assert(buttonPressed(1));}
 assert(!buttonPressed(1));
 // Bounce on both edges and a long hold produce one press, then a new press.
 sampleFor(900,{{BTN_NEXT,50,55},{BTN_NEXT,60,65},{BTN_NEXT,70,600},
                {BTN_NEXT,605,610},{BTN_NEXT,700,770}});
 assert(queue.size()==2);
 // Wake button held at boot cannot immediately pause; release then new press.
 sampleFor(400,{{BTN_PLAY,0,170},{BTN_PLAY,230,300}});
 assert(queue.size()==1 && buttonPressed(0));assert(lastButtonActivity==now);
 // Mixed controls retain chronological order instead of collapsing events.
 sampleFor(500,{{BTN_PREV,50,110},{BTN_NEXT,170,230},{BTN_VOLUP,300,370}});
 assert(!buttonPressed(1));assert(buttonPressed(2));assert(buttonPressed(1));assert(buttonPressed(3));
 // Sub-debounce glitches are ignored, while a 40 ms tap is retained.
 sampleFor(300,{{BTN_NEXT,50,70},{BTN_NEXT,150,190}});
 assert(queue.size()==1);
 // Actual 5ms sampler: staggered, bouncing volume chord produces only Enter.
 sampleFor(12000,{{BTN_VOLUP,50,55},{BTN_VOLUP,60,11000},
                  {BTN_VOLDN,150,155},{BTN_VOLDN,160,11100}});
 assert(queue.size()==1 && queue.front()==GameButtonRouter::ENTER);
 assert(gameHeldButtons.load()==0);
 // An early cancellation sends no volume events; fresh taps work afterward.
 sampleFor(2000,{{BTN_VOLUP,50,700},{BTN_VOLDN,150,900},{BTN_VOLUP,1300,1400}});
 assert(queue.size()==1 && queue.front()==3);
 DebouncedButton b;
 assert(!b.sample(false,UINT32_MAX-100));
 assert(!b.sample(true,UINT32_MAX-15));
 assert(!b.sample(true,10));assert(b.sample(true,20));assert(!b.sample(true,30));
 std::cout<<"PASS: quick/repeated taps during blocked UI, bounce, holds, wake suppression, ordered controls and rollover.\n";
}
'''.replace('FUNCTIONS',extract('sampleButtonsTask')+'\n'+extract('buttonPressed'))
with tempfile.TemporaryDirectory(prefix='soundpod-buttons-') as tmp:
    cpp=Path(tmp)/'test.cpp';exe=Path(tmp)/'test';cpp.write_text(program)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-I',str(root/'Soundpod_ESP32'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
