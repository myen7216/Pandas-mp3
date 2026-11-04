"""Exercise the production startup-jingle command and event-drain functions."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "Soundpod_ESP32/Soundpod_ESP32.ino").read_text()

def function(name):
    match = re.search(r"void " + name + r"\([^)]*\) \{", source)
    pos, depth = match.end(), 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[match.start():pos]

constant = re.search(r"constexpr int STARTUP_INTRO_TRACK = \d+;", source).group()
assert "if (!wokeFromIdleSleep) {\n    startStartupIntroAudio();\n    playPandaIntro();\n    drainStartupIntroEvents();" in source
assert "currentTrack = currentTrack % totalTracks + 1;" in source

program = r'''
#include <cassert>
#include <cstdint>
#include <deque>
#include <vector>
#include <iostream>
constexpr int DFPLAYER_DEVICE_SD=2, DFPlayerPlayFinished=5, DFPlayerError=6;
uint32_t now=0; int currentVolume=10;
uint32_t millis(){return now;}
void delay(uint32_t ms){now+=ms;}
struct Event {uint8_t type,command;int value;};
struct Player {
  std::vector<char> commands; std::vector<int> arguments;
  std::deque<Event> pending; Event last{};
  void outputDevice(int value){commands.push_back('s');arguments.push_back(value);}
  void enableDAC(){commands.push_back('d');arguments.push_back(0);}
  void volume(int value){commands.push_back('v');arguments.push_back(value);}
  void playMp3Folder(int value){commands.push_back('p');arguments.push_back(value);}
  bool available(){return !pending.empty();}
  uint8_t readType(){last=pending.front();pending.pop_front();return last.type;}
  uint8_t readCommand(){return last.command;}
  int read(){return last.value;}
} dfPlayer;
struct Log {template<class... A>void printf(const char*,A...) {}} Serial;
CONSTANT
FUNCTIONS
int main(){
  startStartupIntroAudio();
  assert((dfPlayer.commands==std::vector<char>{'s','d','v','p'}));
  assert((dfPlayer.arguments==std::vector<int>{2,0,10,11}));
  assert(now==700);
  dfPlayer.pending.push_back({DFPlayerPlayFinished,0x3d,11});
  dfPlayer.pending.push_back({DFPlayerError,0x40,5});
  drainStartupIntroEvents();
  assert(dfPlayer.pending.empty());
  for(int i=0;i<20;i++)dfPlayer.pending.push_back({DFPlayerPlayFinished,0x3d,11});
  drainStartupIntroEvents();
  assert(dfPlayer.pending.size()==4); // Bounded startup work.
  std::cout<<"PASS: startup track 11 command order, volume, pacing, event drain bound and cold-boot-only setup call.\n";
}
'''.replace("CONSTANT", constant).replace(
    "FUNCTIONS", function("startStartupIntroAudio") + "\n" + function("drainStartupIntroEvents"))

with tempfile.TemporaryDirectory(prefix="soundpod-intro-") as tmp:
    cpp=Path(tmp)/"check.cpp"; exe=Path(tmp)/"check"
    cpp.write_text(program)
    subprocess.run(["c++","-std=c++17","-Wall","-Wextra",str(cpp),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
