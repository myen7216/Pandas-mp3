"""Run the production loop/event handling against simulated DFPlayer events.

An optional sketch path allows reproducing the regression in the saved old code.
No board or audible playback is exercised by this host test.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sketch = Path(sys.argv[1]) if len(sys.argv) > 1 else (
    Path(__file__).resolve().parents[1] / 'Soundpod_ESP32/Soundpod_ESP32.ino')
source = sketch.read_text()

def function(name):
    match = re.search(r'(?:void|bool) ' + name + r'\([^)]*\) \{', source)
    if not match:
        return ''
    pos = match.end()
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[match.start():pos]

guard = re.search(r'constexpr uint32_t TRACK_FINISH_GUARD_MS = [^;]+;', source)
volume_limits = '\n'.join(re.findall(r'constexpr int (?:DEFAULT_VOLUME|MAX_VOLUME) = [^;]+;', source))
program = r'''
#include <cassert>
#include <cstdint>
#include <cmath>
#include <deque>
#include <vector>
#include <algorithm>
#include <iostream>
#include <atomic>
#include "BambooGame.h"
#include "ButtonInput.h"
#define F(x) x
constexpr bool ENABLE_BAMBOO_GAME=true;
BambooGame bambooGame;
std::atomic<uint8_t> gameHeldButtons{0};
uint32_t lastButtonActivity=0,lastGameActivity=0;
std::deque<uint8_t> modeEvents;
auto buttonEvents=&modeEvents;
constexpr int pdTRUE=1;
int xQueuePeek(std::deque<uint8_t>* q,uint8_t* e,int){if(q->empty())return 0;*e=q->front();return 1;}
int xQueueReceive(std::deque<uint8_t>* q,uint8_t* e,int){if(!xQueuePeek(q,e,0))return 0;q->pop_front();return 1;}
using std::min; using std::max;
constexpr int DFPlayerPlayFinished=5, DFPlayerCardRemoved=3, DFPlayerError=6;
constexpr int Busy=1, FileIndexOut=5, FileMismatch=6;
constexpr bool ENABLE_PANDA_PET=false;
constexpr uint32_t ANIM_INTERVAL=33, SCROLL_INTERVAL=80;
constexpr uint32_t PROGRESS_UPDATE_INTERVAL=1000, VOLUME_DISPLAY_MS=2000;
constexpr int totalTracks=10;
uint32_t clockNow, trackStartTime, totalPausedDuration, pauseStartTime;
uint32_t lastAnimUpdate, lastScrollUpdate, lastProgressUpdate, volumeShownUntil;
uint32_t millis(){return clockNow;}
void delay(uint32_t ms){clockNow+=ms;}
uint32_t esp_random(){return 42;}
bool isPlaying, playbackNeedsSelection, titleNeedsScroll=false;
int currentTrack, currentVolume=5, scrollOffset, redraws;
float wavePhase=0;
bool presses[5]={};
bool buttonPressed(int i){bool p=presses[i];presses[i]=false;return p;}
void updateDisplay(){redraws++;}
void maintainDisplayOrientation(){}
void checkIdleSleep(){}
bool updateBatteryGauge(){return false;}
struct Panda {void update(uint32_t,bool,uint32_t){}} pandaWalker;
struct Log {template<class... T> void printf(const char*,T...) {} void println(const char*){}} Serial;
struct Event {uint8_t type;int value;uint8_t command=0x3D;};
struct Player {
  std::deque<Event> pending;
  Event last{};
  std::vector<int> plays;
  int paused=0, resumed=0;
  int lastVolume=-1, volumeWrites=0;
  bool available(){return !pending.empty();}
  uint8_t readType(){last=pending.front();pending.pop_front();return last.type;}
  uint8_t readCommand(){return last.command;}
  int read(){return last.value;}
  void playMp3Folder(int t){plays.push_back(t);}
  void start(){resumed++;}
  void pause(){paused++;}
  void volume(int value){lastVolume=value;volumeWrites++;}
} dfPlayer;
GUARD
VOLUME_LIMITS
FUNCTIONS
void reset(int track=1){
  clockNow=300000;trackStartTime=0;totalPausedDuration=0;pauseStartTime=0;
  lastAnimUpdate=lastScrollUpdate=lastProgressUpdate=clockNow;
  volumeShownUntil=0;isPlaying=true;playbackNeedsSelection=false;
  currentTrack=track;scrollOffset=-12;redraws=0;dfPlayer=Player{};
  std::fill(presses,presses+5,false);
  bambooGame=BambooGame{};gameHeldButtons=0;modeEvents.clear();
  lastButtonActivity=lastGameActivity=0;
}
void finish(int number=1,uint8_t command=0x3D){
  dfPlayer.pending.push_back({DFPlayerPlayFinished,number,command});
}
int main(){
  assert(DEFAULT_VOLUME==1 && MAX_VOLUME==20);
  reset();currentVolume=2;presses[4]=true;loop();assert(currentVolume==1 && dfPlayer.lastVolume==1);
  presses[4]=true;loop();assert(currentVolume==0 && dfPlayer.lastVolume==0);
  presses[4]=true;loop();assert(currentVolume==0);
  presses[3]=true;loop();assert(currentVolume==1 && dfPlayer.lastVolume==1);
  currentVolume=19;presses[3]=true;loop();assert(currentVolume==20);
  presses[3]=true;loop();assert(currentVolume==20 && dfPlayer.lastVolume==20);
  // Reproduce the user's fault: two finish frames, one read per loop before fix.
  reset();finish();finish();loop();loop();
  assert(currentTrack==2 && (dfPlayer.plays==std::vector<int>{2}));
  assert(scrollOffset==0 && totalPausedDuration==0 && redraws>0);
  // A duplicate arriving in a separate serial burst must not skip the new song.
  clockNow=trackStartTime+400;finish();loop();
  clockNow=trackStartTime+1999;finish();loop();
  assert(currentTrack==2 && dfPlayer.plays.size()==1);
  // Normal completion of song 2 still advances, even with a different FAT index.
  clockNow=trackStartTime+284000;finish(87);loop();
  assert(currentTrack==3 && (dfPlayer.plays==std::vector<int>{2,3}));
  // Album wraps only once: two final-song reports must land on Janie, not song 2.
  reset(10);finish(10);finish(10);loop();loop();assert(currentTrack==1);
  assert((dfPlayer.plays==std::vector<int>{1}));
  // Button selection wins over a stale end message in the very same loop.
  reset();presses[1]=true;finish();loop();assert(currentTrack==2);
  assert((dfPlayer.plays==std::vector<int>{2}));
  reset(3);presses[2]=true;finish(3);loop();assert(currentTrack==2);
  assert((dfPlayer.plays==std::vector<int>{2}));
  // Paused, failed startup, and early startup/wake notifications cannot advance.
  reset();isPlaying=false;finish();loop();assert(dfPlayer.plays.empty());
  reset();playbackNeedsSelection=true;finish();loop();assert(dfPlayer.plays.empty());
  reset();resetTrackTimer();finish();loop();assert(dfPlayer.plays.empty());
  // The displayed duration is never used as an auto-next timer.
  reset();clockNow=900000;loop();assert(currentTrack==1 && dfPlayer.plays.empty());
  // Pause/resume preserves the track's elapsed timer, ordinary completion follows.
  reset();presses[0]=true;loop();assert(!isPlaying && dfPlayer.paused==1);
  clockNow+=10000;presses[0]=true;loop();assert(isPlaying && dfPlayer.resumed==1);
  assert(trackStartTime==0 && totalPausedDuration==10001);
  finish();loop();assert(currentTrack==2);
  // A manual Next remains actionable immediately after an automatic advance.
  reset();finish();loop();assert(currentTrack==2);
  clockNow+=50;presses[1]=true;finish();loop();
  assert(currentTrack==3 && (dfPlayer.plays==std::vector<int>{2,3}));
  // USB completion must not advance the SD playlist.
  reset();finish(1,0x3C);loop();assert(dfPlayer.plays.empty());
  // Removal/file failure in the same burst cancels the pending advance.
  for(Event e: {Event{DFPlayerCardRemoved,0,0x3B},Event{DFPlayerError,FileMismatch,0x40}}){
    reset();finish();dfPlayer.pending.push_back(e);loop();
    assert(!isPlaying && dfPlayer.plays.empty());
  }
  // A protocol error alone must not stop real playback or hide a valid finish.
  reset();dfPlayer.pending.push_back({DFPlayerError,99,0x40});finish();loop();
  assert(isPlaying && currentTrack==2);
  // Bound a large burst to protect the UI; leftover duplicates are guarded.
  reset();for(int i=0;i<40;i++)finish();loop();loop();loop();
  assert(currentTrack==2 && dfPlayer.plays.size()==1 && dfPlayer.pending.empty());
  // Explicit 32-bit subtraction survives millis wrapping on the ESP32.
  reset();trackStartTime=UINT32_MAX-1000;clockNow=500;finish();loop();
  assert(dfPlayer.plays.empty());clockNow=1500;finish();loop();assert(currentTrack==2);
  // Guard expires at 2000 ms (shorter audio clips are intentionally unsupported).
  reset();resetTrackTimer();clockNow+=2000;finish();loop();assert(currentTrack==2);
  // Entering/exiting or restarting the game never changes music transport.
  reset();int volume=currentVolume;auto start=trackStartTime;
  modeEvents.push_back(GameButtonRouter::ENTER);loop();assert(bambooGame.active);
  assert(isPlaying && dfPlayer.plays.empty() && currentVolume==volume && trackStartTime==start);
  gameHeldButtons=2;clockNow+=50;float px=bambooGame.x;loop();assert(bambooGame.x>px);
  assert(dfPlayer.plays.empty() && dfPlayer.paused==0 && dfPlayer.resumed==0 && currentVolume==volume);
  // Duplicate finish reports still advance just once underneath gameplay.
  finish();finish();loop();loop();assert(currentTrack==2 && dfPlayer.plays.size()==1);
  bambooGame.over=true;clockNow+=300000;finish(2);finish(2);loop();assert(currentTrack==3);
  bambooGame.score=17;loop();assert(bambooGame.over && bambooGame.score==17);
  modeEvents.push_back(GameButtonRouter::RESTART);loop();assert(!bambooGame.over && bambooGame.score==0);
  assert(dfPlayer.paused==0 && dfPlayer.resumed==0 && dfPlayer.plays.size()==2);
  modeEvents.push_back(GameButtonRouter::EXIT);loop();assert(!bambooGame.active && currentTrack==3);
  assert(currentVolume==volume && bambooGame.score==0);
  // Paused music stays paused through all game actions, including restart.
  reset();isPlaying=false;pauseStartTime=clockNow;modeEvents.push_back(GameButtonRouter::ENTER);loop();
  bambooGame.over=true;modeEvents.push_back(GameButtonRouter::RESTART);loop();
  finish();loop();modeEvents.push_back(GameButtonRouter::EXIT);loop();
  assert(!isPlaying && dfPlayer.plays.empty() && dfPlayer.paused==0 && dfPlayer.resumed==0);
  std::cout<<"PASS: audio regressions plus gameplay/game-over automatic-next, preserved pause/volume/timer, final score retained until restart, reset to zero.\n";
}
'''.replace('GUARD', guard.group() if guard else '').replace('VOLUME_LIMITS',volume_limits).replace(
    'FUNCTIONS', '\n'.join(function(n) for n in (
        'resetTrackTimer', 'markPlaybackStopped', 'processGameInput', 'handlePlayerEvents', 'loop')))

with tempfile.TemporaryDirectory(prefix='soundpod-auto-advance-') as tmp:
    cpp = Path(tmp) / 'checks.cpp'
    exe = Path(tmp) / 'checks'
    cpp.write_text(program)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-I', str(sketch.parent), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
