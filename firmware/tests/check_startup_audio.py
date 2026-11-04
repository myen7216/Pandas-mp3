"""Exercise production startup logic with simulated DFPlayer responses."""
from pathlib import Path
import re
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'Soundpod_ESP32/Soundpod_ESP32.ino').read_text()
functions = '\n'.join(re.search(pattern + r' \{.*?\n\}', source, re.S).group() for pattern in (
    r'int readStartupPlaybackState\(\)', r'bool startInitialTrack\(\)', r'void resetTrackTimer\(\)'))
program = r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include <iostream>
#define F(x) x
constexpr int DFPLAYER_DEVICE_SD=2;
uint32_t clockNow=0, trackStartTime=0, totalPausedDuration=0;
uint32_t millis(){return clockNow;}
void delay(unsigned ms){clockNow+=ms;}
int currentTrack=1,currentVolume=5;
struct SerialMock {
 template<class... A> void printf(const char*,A...){}
 void println(const char*){}
} Serial;
struct Reply {int value;uint8_t command;};
struct Event {char command;int value;uint32_t at;};
struct Player {
 std::vector<Reply> replies;
 std::vector<Event> events;
 unsigned cursor=0;uint8_t command=0;
 void outputDevice(int x){events.push_back({'s',x,millis()});}
 void enableDAC(){events.push_back({'d',0,millis()});}
 void volume(int x){events.push_back({'v',x,millis()});}
 void playMp3Folder(int x){events.push_back({'p',x,millis()});}
 int readState(){assert(cursor<replies.size());auto r=replies[cursor++];command=r.command;return r.value;}
 uint8_t readCommand(){return command;}
} dfPlayer;
void resetTrackTimer();
FUNCTIONS
void reset(std::vector<Reply> replies,int track=1,int volume=5){
 dfPlayer=Player{};dfPlayer.replies=replies;clockNow=0;
 currentTrack=track;currentVolume=volume;totalPausedDuration=999;
}
void checkCommands(unsigned plays){
 const auto& e=dfPlayer.events;
 assert(e.size()==3+plays);
 assert(e[0].command=='s' && e[0].value==2);
 assert(e[1].command=='d' && e[1].at-e[0].at>=400);
 assert(e[2].command=='v' && e[2].value==currentVolume && e[2].at-e[1].at>=150);
 assert(e[3].at-e[2].at>=150);
 for(unsigned i=3;i<e.size();i++)assert(e[i].command=='p' && e[i].value==currentTrack);
 assert(trackStartTime==e.back().at && totalPausedDuration==0);
}
int main(){
 // Cold start succeeds: only one selection; elapsed time begins at play command.
 reset({{0x0201,0x42}});assert(startInitialTrack());checkCommands(1);
 // Still stopped through the first polling window, then same song succeeds.
 reset({{0,0x42},{0,0x42},{2,0x42},{1,0x42}});
 assert(startInitialTrack());checkCommands(2);
 // Slow start within first window does not unnecessarily restart the song.
 reset({{0,0x42},{1,0x42}});assert(startInitialTrack());checkCommands(1);
 // Wake must retain the selected track and volume, including muted volume zero.
 reset({{1,0x42}},7,0);assert(startInitialTrack());checkCommands(1);
 // Unavailable status is a compatibility fallback, not endless retries.
 reset({{-1,0x42},{1,0x43},{3,0x42}});
 assert(startInitialTrack());checkCommands(1);assert(dfPlayer.cursor==3);
 // Stale unrelated response is ignored; genuine stopped replies permit retry.
 reset({{1,0x43},{0,0x42},{-1,0x42},{1,0x42}});
 assert(startInitialTrack());checkCommands(2);
 // Persistent failure is bounded and leaves the UI paused.
 reset(std::vector<Reply>(9,{0,0x42}));
 assert(!startInitialTrack());checkCommands(3);assert(dfPlayer.cursor==9);
 std::cout<<"PASS: startup command pacing, same-track retries, bounded failure, stale/unavailable replies, retained wake track/volume, playback timer.\n";
}
'''.replace('FUNCTIONS', functions)
with tempfile.TemporaryDirectory(prefix='soundpod-startup-test-') as tmp:
    cpp=Path(tmp)/'checks.cpp';exe=Path(tmp)/'checks';cpp.write_text(program)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
