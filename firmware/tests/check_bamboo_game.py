"""Exercise production game and chord routing, without an ESP32."""
from pathlib import Path
import subprocess
import tempfile

folder = Path(__file__).resolve().parents[1] / 'Soundpod_ESP32'
program = r'''
#include <cassert>
#include <cmath>
#include <vector>
#include <iostream>
#include <string>
#include "BambooGame.h"
#include "ButtonInput.h"
constexpr uint8_t UP=8, DOWN=16, RIGHT=2, LEFT=4, BOTH=24;
struct Display {
 std::vector<std::string> labels;
 void clearDisplay(){labels.clear();}
 template<class... A>void setFont(A...){}
 template<class... A>void setTextSize(A...){}
 template<class... A>void setTextColor(A...){}
 template<class... A>void setTextWrap(A...){}
 template<class... A>void setCursor(A...){}
 template<class... A>void drawLine(A...){}
 template<class... A>void drawPixel(A...){}
 template<class... A>void fillTriangle(A...){}
 template<class... A>void fillRoundRect(A...){}
 template<class... A>void fillRect(A...){}
 template<class... A>void fillCircle(A...){}
 template<class... A>void drawCircle(A...){}
 void print(const char* label){labels.emplace_back(label);}
 void display(){}
};
int main(){
 GameButtonRouter r;
 // A staggered entry never sends a volume command; threshold is continuous.
 assert(r.sample(UP,UP,100).count==0);
 assert(r.sample(BOTH,DOWN,300).count==0);
 assert(r.sample(BOTH,0,5299).count==0);
 auto e=r.sample(BOTH,0,5300);
 assert(e.toggled && e.count==1 && e.events[0]==r.ENTER && r.active);
 assert(e.held==128);
 assert(r.sample(BOTH,0,11000).count==0 && r.active);
 assert(r.sample(UP,0,11100).held==128);
 assert(r.sample(0,0,11200).count==0);
 // Directions are held state only; Play is routed away from audio.
 e=r.sample(RIGHT,RIGHT,11300);assert(e.count==0 && e.held==RIGHT);
 e=r.sample(1,1,11400);assert(e.count==1 && e.events[0]==r.RESTART);
 r.sample(0,0,11500);
 r.sample(BOTH,BOTH,12000);e=r.sample(BOTH,0,17000);
 assert(e.toggled && e.events[0]==r.EXIT && !r.active);
 r.sample(0,0,17100);
 assert(!r.sample(UP,UP,17200).count);
 e=r.sample(0,0,17300);assert(e.count==1 && e.events[0]==3);
 // Cancelled chords consume both releases. A new tap still works.
 r.sample(UP,UP,18000);r.sample(BOTH,DOWN,18100);
 assert(!r.sample(UP,0,18500).count);
 assert(!r.sample(0,0,18600).count && !r.active);
 r.sample(DOWN,DOWN,19000);e=r.sample(0,0,19100);
 assert(e.count==1 && e.events[0]==4);
 // Rollover-safe gesture and original controls when feature is disabled.
 GameButtonRouter wrap;wrap.sample(BOTH,BOTH,UINT32_MAX-2000);
 assert(!wrap.sample(BOTH,0,2998).count);
 assert(wrap.sample(BOTH,0,2999).toggled);
 GameButtonRouter off;off.enabled=false;e=off.sample(UP,UP,0);
 assert(e.count==1 && e.events[0]==3 && !off.active);

 BambooGame g;g.enter(42);assert(g.score==0);
 assert(g.active && g.initialized && !g.over && !g.touchesPanda());
 for(int i=0;i<3;i++)assert(g.safeFood(i,g.food[i].x,g.food[i].y));
 // All four directions and diagonal normalization; no motion across a chord.
 float x=g.x,y=g.y;
 g.update(50,RIGHT);assert(g.x>x && g.y==y);
 g.update(50,LEFT);assert(fabs(g.x-x)<0.001);
 g.update(50,UP);assert(g.y<y);
 g.update(50,DOWN);assert(fabs(g.y-y)<0.001);
 x=g.x;y=g.y;g.update(50,RIGHT|DOWN);
 assert(fabs(sqrtf((g.x-x)*(g.x-x)+(g.y-y)*(g.y-y))-1.5f)<0.001);
 auto phase=g.phaseMs;auto ex=g.enemyX;auto ey=g.enemyY;x=g.x;y=g.y;
 g.update(50,128);assert(g.x==x && g.y==y && g.enemyX==ex && g.enemyY==ey && g.phaseMs==phase);
 // A long serial stall cannot teleport the player or smiley.
 g.update(5000,RIGHT);assert(g.x-x<=1.501f);
 // Opposing directions cancel, while the smiley takes a straight shortest path.
 g.restart();x=g.x;y=g.y;ex=g.enemyX;ey=g.enemyY;
 float dx=x+6-ex,dy=y+5-ey;
 g.update(50,LEFT|RIGHT|UP|DOWN);assert(g.x==x && g.y==y);
 assert(fabs((g.enemyX-ex)*dy-(g.enemyY-ey)*dx)<0.001);
 assert(fabs(sqrtf((g.enemyX-ex)*(g.enemyX-ex)+(g.enemyY-ey)*(g.enemyY-ey))-0.65f)<0.001);
 // Fixed collection range: exactly one point and safe respawn, no repeat point.
 g.score=12;g.food[0]={g.x+6,g.y+5};g.update(1,0);assert(g.score==13);
 g.update(1,0);assert(g.score==13);
 assert(g.safeFood(0,g.food[0].x,g.food[0].y));
 // Mode changes retain this round; death shows the final score until restart.
 g.leave();x=g.x;g.update(50,RIGHT);assert(g.x==x);
 g.enter(87);assert(g.score==13 && g.x==x);
 g.enemyX=g.x+6;g.enemyY=g.y+4;g.update(1,0);assert(g.over);
 x=g.x;y=g.y;ex=g.enemyX;phase=g.phaseMs;
 g.update(50,RIGHT);assert(g.x==x && g.y==y && g.enemyX==ex && g.phaseMs==phase && g.score==13);
 g.leave();g.enter(87);assert(g.over && g.score==13);
 Display display;g.draw(display,0);
 assert((display.labels==std::vector<std::string>{"13","GAME OVER","Score: 13","PLAY: RESTART"}));
 assert(g.score==13);
 g.restart();assert(!g.over && g.score==0 && !g.touchesPanda());
 g.draw(display,0);assert((display.labels==std::vector<std::string>{"0"}));
 // Thousands of safe spawns over varied seeds stay in bounds/separate.
 for(uint32_t seed=1;seed<1000;seed++){
   BambooGame a;a.enter(seed);
   for(int i=0;i<3;i++){
     assert(a.food[i].x>=4 && a.food[i].x<=123 && a.food[i].y>=14 && a.food[i].y<=60);
     assert(a.safeFood(i,a.food[i].x,a.food[i].y));
   }
 }
 // Every boundary clamps, even with a long update; max score saturates.
 g.restart();g.x=115;g.y=53;g.update(50,RIGHT|DOWN);assert(g.x==115 && g.y==53);
 g.x=0;g.y=10;g.update(50,LEFT|UP);assert(g.x==0 && g.y==10);
 g.restart();g.score=UINT32_MAX;g.food[0]={g.x+6,g.y+5};g.update(1,0);assert(g.score==UINT32_MAX);
 std::cout<<"PASS: chords/release/cancel/rollover, directions, pursuit, collection, boundaries, final score on death, zero on restart, safe spawns.\n";
}
'''
with tempfile.TemporaryDirectory(prefix='panda-game-') as tmp:
    cpp = Path(tmp) / 'test.cpp'
    exe = Path(tmp) / 'test'
    cpp.write_text(program)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-I', str(folder), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
