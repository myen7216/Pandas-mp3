"""Exercise production battery sampling, conversion, and icon layout without hardware."""
from pathlib import Path
import re
import subprocess
import tempfile

folder = Path(__file__).resolve().parents[1] / 'Soundpod_ESP32'
source = (folder / 'Soundpod_ESP32.ino').read_text()

def function(name):
    m = re.search(r'(?:void|bool) ' + name + r'\([^)]*\) \{', source)
    pos, depth = m.end(), 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[m.start():pos]

config = source[source.index('constexpr bool BATTERY_SENSE_CONNECTED'):source.index('void beginBatteryGauge()')]
functions = '\n'.join(function(n) for n in ('beginBatteryGauge', 'updateBatteryGauge', 'drawBatteryText', 'drawBatteryIndicator'))
assert 'if (updateBatteryGauge()) needsUpdate = true;' in source
assert source.index('drawPandaPet();\n  drawBatteryIndicator();') > 0

program = r'''
#include "BatteryGauge.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
constexpr int INPUT=0, ADC_11db=3, SSD1306_WHITE=1;
uint32_t clockNow=0, adcMv=1850;
int reads=0, configurations=0;
uint32_t millis(){return clockNow;}
void pinMode(int pin,int mode){assert(pin==34 && mode==INPUT);configurations++;}
void analogReadResolution(int bits){assert(bits==12);configurations++;}
void analogSetPinAttenuation(int pin,int db){assert(pin==34 && db==ADC_11db);configurations++;}
uint32_t analogReadMilliVolts(int pin){assert(pin==34);reads++;return adcMv;}
struct Log {template<class... T> void printf(const char*,T...) {}} Serial;
struct Display {
  int pixels=0;
  void drawRect(int x,int y,int w,int h,int){
    assert(x==BATTERY_REGION_X && y==55 && w==19 && h==9);
    assert(x+w<=128 && y+h<=64);pixels=0;
  }
  void fillRect(int x,int y,int w,int h,int){
    assert(x==127 && y==58 && w==1 && h==3);
  }
  void drawPixel(int x,int y,int c){
    // One clear pixel between text and each edge of the battery body.
    assert(x>=BATTERY_REGION_X+2 && x<=BATTERY_REGION_X+16);
    assert(y>=57 && y<=61 && c==1);pixels++;
  }
} display;
CONFIG
FUNCTIONS
void reset(){
  batteryGauge=BatteryGauge{};batterySampling=false;batterySampleCount=0;
  batterySampleSum=0;lastBatterySample=lastBatteryRead=0;
  clockNow=0;reads=configurations=0;adcMv=1850;
}
bool batch(){
  bool changed=false;
  for(int i=0;i<BATTERY_SAMPLE_COUNT;i++){
    clockNow+=BATTERY_SAMPLE_INTERVAL_MS;
    changed=updateBatteryGauge() || changed;
  }
  return changed;
}
int main(){
  static_assert(PANDA_MAX_X+PANDA_SPRITE_WIDTH+PANDA_BATTERY_GAP==BATTERY_REGION_X);
  static_assert(BATTERY_REGION_X+20==128);
  // All percentage and low/unknown labels fit inside the outline.
  drawBatteryIndicator();assert(display.pixels>0);
  for(int p=0;p<=100;p++){
    batteryGauge.valid=true;batteryGauge.percent=p;batteryGauge.low=p<=14;
    drawBatteryIndicator();assert(display.pixels>0);
  }
  BatteryGauge g;
  assert(g.accept(4200) && g.percent==100 && g.valid);
  assert(!g.accept(4200));
  assert(g.accept(0) && !g.valid && g.percent==-1);
  assert(!g.accept(5000) && !g.valid);
  assert(!g.accept(std::numeric_limits<float>::quiet_NaN()));
  assert(g.accept(3700) && g.percent==20);
  assert(g.accept(3800) && g.millivolts==3725); // Low-pass smoothing.
  g=BatteryGauge{};g.accept(3600);assert(g.low && g.percent==10);
  g.accept(3680);assert(g.low); // Hysteresis keeps warning stable near threshold.
  for(int i=0;i<20;i++)g.accept(3800);
  assert(!g.low);
  g=BatteryGauge{};g.accept(2900);assert(g.valid && g.percent==0 && g.low);
  g=BatteryGauge{};g.accept(4250);assert(g.valid && g.percent==100);
  int previous=-1;
  for(int mv=2800;mv<=4300;mv++){
    int p=BatteryGauge::estimatePercent(mv);assert(p>=previous && p>=0 && p<=100);previous=p;
  }
  reset();beginBatteryGauge();
  if (!BATTERY_SENSE_CONNECTED){
    for(int i=0;i<10;i++){clockNow+=30000;assert(!updateBatteryGauge());}
    assert(reads==0 && configurations==0 && !batteryGauge.valid);
    drawBatteryIndicator();assert(display.pixels>0);
    std::cout<<"PASS: unwired mode performs no ADC reads; unknown icon and every percentage fit.\n";
    return 0;
  }
  assert(configurations==3 && reads==1); // Initial conversion discarded.
  assert(!updateBatteryGauge() && reads==1);
  for(int i=0;i<31;i++){
    clockNow+=5;assert(!updateBatteryGauge());assert(!batteryGauge.valid);
  }
  clockNow+=5;assert(updateBatteryGauge());
  assert(reads==33 && batteryGauge.millivolts==3700 && batteryGauge.percent==20);
  // No resampling before 30 seconds, even when polled every millisecond.
  const auto completedAt=lastBatteryRead;
  for(int i=0;i<29999;i++){clockNow++;assert(!updateBatteryGauge());}
  assert(reads==33 && lastBatteryRead==completedAt);
  clockNow++;assert(!updateBatteryGauge());assert(reads==34);
  // One conversion per call even after a long command stall.
  clockNow+=10000;int before=reads;updateBatteryGauge();assert(reads==before+1);
  while(batterySampling){clockNow+=5;updateBatteryGauge();}
  assert(batteryGauge.millivolts==3700);
  // Zero/overrange batches become unknown, then recover from valid data.
  adcMv=0;clockNow+=30000;assert(batch());assert(!batteryGauge.valid);
  adcMv=2500;clockNow+=30000;batch();assert(!batteryGauge.valid);
  adcMv=2100;clockNow+=30000;assert(batch());assert(batteryGauge.percent==100);
  // Periodic scheduling and sample spacing both survive 32-bit timer wrap.
  reset();clockNow=UINT32_MAX-100;beginBatteryGauge();assert(batch());
  assert(batteryGauge.valid);clockNow+=30000;batch();assert(batteryGauge.valid);
  std::cout<<"PASS: GPIO34 configuration, 32-sample averaging, divider conversion, filtering, voltage curve, low warning, invalid/recovery, cadence and rollover.\n";
}
'''.replace('CONFIG', config).replace('FUNCTIONS', functions)

with tempfile.TemporaryDirectory(prefix='soundpod-battery-') as tmp:
    for enabled in (False, True):
        cpp = Path(tmp) / 'checks.cpp'
        exe = Path(tmp) / 'checks'
        cpp.write_text(program.replace('BATTERY_SENSE_CONNECTED = false',
                                       'BATTERY_SENSE_CONNECTED = ' + str(enabled).lower()))
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-I',str(folder),str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
