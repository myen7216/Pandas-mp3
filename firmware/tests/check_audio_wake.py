"""Host checks of production audio initialization, with simulated UART replies."""
from pathlib import Path
import re
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'Soundpod_ESP32/Soundpod_ESP32.ino').read_text()
functions = '\n'.join(re.search(r'bool ' + name + r'\(\) \{.*?\n\}', source, re.S).group()
                      for name in ('probeAudioReady', 'initializeAudioPlayer'))
program = r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include <iostream>
#define F(x) x
constexpr int DFPLAYER_DEVICE_SD=2,DFPlayerCardOnline=4,DFPlayerFeedBack=11;
bool wokeFromIdleSleep=false,dfPlayerSerialReady=false;
uint32_t now=0;
void delay(unsigned ms){now+=ms;}
struct Log {template<class... A>void printf(const char*,A...){} void println(const char*){}} Serial;
struct UART {} dfSerial;
struct Reply {int value;uint8_t type,command;};
struct Player {
 bool resetOnline=false;int binds=0,resets=0,selections=0;
 uint32_t selectedAt=0,resetAt=0;
 std::vector<Reply> replies;unsigned cursor=0;
 Reply last={-1,0,0};
 bool begin(UART&,bool ack,bool reset){
   assert(ack);
   if(reset){++resets;resetAt=now;now+=2200;return resetOnline;}
   ++binds;return true; // Mirrors library's assumed success without reset.
 }
 void outputDevice(int device){assert(device==DFPLAYER_DEVICE_SD);++selections;selectedAt=now;}
 int readFileCounts(int device){
   assert(device==DFPLAYER_DEVICE_SD);
   assert(cursor<replies.size());
   if(!resets) assert(now-selectedAt>=1500);
   else assert(now-resetAt>=4200); // Allow late responses after begin's timeout.
   last=replies[cursor++];return last.value;
 }
 uint8_t readType(){return last.type;}
 uint8_t readCommand(){return last.command;}
} dfPlayer;
FUNCTIONS
void setupCase(bool wake,bool online,std::vector<Reply> replies){
 wokeFromIdleSleep=wake;dfPlayerSerialReady=false;now=0;
 dfPlayer=Player{};dfPlayer.resetOnline=online;dfPlayer.replies=replies;
}
int main(){
 // Wake without a new boot event succeeds on an actual SD file-count reply.
 setupCase(true,false,{{10,DFPlayerFeedBack,0x48}});
 assert(initializeAudioPlayer());assert(dfPlayer.binds==1 && dfPlayer.resets==0);
 assert(dfPlayer.selections==1 && dfPlayerSerialReady);
 // A late online event is also acceptable, even though readFileCounts returns -1.
 setupCase(true,false,{{-1,0,0},{-1,DFPlayerCardOnline,0x3f}});
 assert(initializeAudioPlayer());assert(dfPlayer.resets==0);
 // Unrelated feedback/empty card do not pass; fall back to reset once.
 setupCase(true,true,{{10,DFPlayerFeedBack,0x43},{0,DFPlayerFeedBack,0x48},{-1,0,0}});
 assert(initializeAudioPlayer());assert(dfPlayer.resets==1 && dfPlayer.cursor==3);
 // The library's optimistic no-reset begin() cannot make a disconnected player pass.
 setupCase(true,false,std::vector<Reply>(6,{-1,0,0}));
 assert(!initializeAudioPlayer());assert(dfPlayer.resets==1 && dfPlayer.cursor==6);
 // Cold start preserves the successful reset path, without unnecessary queries.
 setupCase(false,true,{});assert(initializeAudioPlayer());
 assert(dfPlayer.binds==0 && dfPlayer.resets==1 && dfPlayer.selections==0);
 // A cold reset timeout is allowed to finish; it is not immediately reset again.
 setupCase(false,false,{{-1,0,0},{10,DFPlayerFeedBack,0x48}});
 assert(initializeAudioPlayer());assert(dfPlayer.resets==1);
 // Wake reset fallback can also recover from a late SD reply.
 setupCase(true,false,{{-1,0,0},{-1,0,0},{-1,0,0},{10,DFPlayerFeedBack,0x48}});
 assert(initializeAudioPlayer());assert(dfPlayer.resets==1);
 std::cout<<"PASS: standby wake selection, settling, real-response validation, reset fallback, late replies, bounded disconnected failure, cold initialization.\n";
}
'''.replace('FUNCTIONS', functions)
with tempfile.TemporaryDirectory(prefix='soundpod-wake-test-') as tmp:
    cpp=Path(tmp)/'checks.cpp';exe=Path(tmp)/'checks';cpp.write_text(program)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
