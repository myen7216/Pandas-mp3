"""Host checks of the production panda movement state and song-timer reset."""
from pathlib import Path
import re
import subprocess
import tempfile

folder = Path(__file__).resolve().parents[1] / 'Soundpod_ESP32'
source = (folder / 'Soundpod_ESP32.ino').read_text()
reset = re.search(r'void resetTrackTimer\(\) \{.*?\n\}', source, re.S).group()
program = r'''
#include "PandaWalker.h"
#include <cassert>
#include <set>
#include <iostream>
uint32_t clockNow=123, trackStartTime=0, totalPausedDuration=99;
uint32_t millis(){return clockNow;}
PandaWalker pandaWalker;
RESET_FUNCTION
int main(){
 pandaWalker.x=50.25f;pandaWalker.target=70;pandaWalker.speed=15;pandaWalker.moving=true;
 // Simulate long DFPlayer command delays: never move more than 0.75px per frame.
 float before=pandaWalker.x;
 pandaWalker.update(5000,true,42);
 assert(fabsf(pandaWalker.x-before-0.75f)<0.0001f);
 // Next/Previous/auto-advance reset this exact song timer, not pet state.
 for(int i=0;i<20;i++){
  auto saved=pandaWalker;clockNow+=500;resetTrackTimer();
  assert(trackStartTime==clockNow && totalPausedDuration==0);
  assert(pandaWalker.x==saved.x && pandaWalker.target==saved.target);
  assert(pandaWalker.stepMs==saved.stepMs && pandaWalker.restMs==saved.restMs);
 }
 auto saved=pandaWalker;pandaWalker.update(600000,false,123);
 assert(pandaWalker.x==saved.x && pandaWalker.target==saved.target);
 assert(pandaWalker.stepMs==saved.stepMs && !pandaWalker.moving);
 pandaWalker.restMs=500;saved=pandaWalker;pandaWalker.update(60000,false,9);
 assert(pandaWalker.restMs==500 && pandaWalker.x==saved.x);
 // Long deterministic simulation of the actual walker, including stalls and pauses.
 PandaWalker w;uint32_t rng=2463534242u;std::set<int> destinations;bool interiorTurn=false;
 for(int i=0;i<30000;i++){
  rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
  const auto prev=w;const bool playing=i%17!=0;
  w.update(i%101==0?5000:33,playing,rng);
  assert(w.x>=1.0f && w.x<=PANDA_MAX_X);
  assert(w.target>=1 && w.target<=PANDA_MAX_X);
  assert((int)lroundf(w.x)+PANDA_SPRITE_WIDTH-1 < BATTERY_REGION_X-PANDA_BATTERY_GAP);
  assert(fabsf(w.x-prev.x)<=0.751f);
  if(!playing) assert(w.x==prev.x && w.stepMs==prev.stepMs && w.restMs==prev.restMs);
  if(w.target!=prev.target)destinations.insert((int)w.target);
  if(w.rightward!=prev.rightward && prev.x>2 && prev.x<PANDA_MAX_X-1)interiorTurn=true;
 }
 assert(destinations.size()>10 && interiorTurn);
 std::cout<<"PASS: random destinations, interior turns, bounds, pause/rest freezing, command-stall movement cap, song-change continuity.\n";
}
'''.replace('RESET_FUNCTION', reset)
with tempfile.TemporaryDirectory(prefix='soundpod-panda-test-') as tmp:
 cpp=Path(tmp)/'checks.cpp';exe=Path(tmp)/'checks';cpp.write_text(program)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-I',str(folder),str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
