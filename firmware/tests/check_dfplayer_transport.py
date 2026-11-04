"""Verify missing-ACK latency and finish events using the installed DFPlayer library."""
from pathlib import Path
import os
import subprocess
import tempfile
lib=Path(os.environ.get('DFPLAYER_LIBRARY_DIR', str(Path.home()/'Documents/Arduino/libraries/DFRobotDFPlayerMini')))
if not (lib/'DFRobotDFPlayerMini.cpp').is_file():
 raise SystemExit('Set DFPLAYER_LIBRARY_DIR to your installed DFRobotDFPlayerMini library folder')
arduino=r'''
#pragma once
#include <cstdint>
#include <cstddef>
using std::size_t;
unsigned long millis();
void delay(unsigned long);
class Stream {public: virtual size_t write(const uint8_t*,size_t)=0;virtual int read()=0;virtual int available()=0;};
'''
program=r'''
#include "DFRobotDFPlayerMini.h"
#include <vector>
#include <deque>
#include <cassert>
#include <iostream>
unsigned long now=0;
unsigned long millis(){return now;}
void delay(unsigned long ms){now+=ms?ms:1;}
struct UART:Stream {
 std::vector<std::vector<uint8_t>> sent;std::deque<uint8_t> rx;
 size_t write(const uint8_t* data,size_t n){sent.emplace_back(data,data+n);return n;}
 int read(){int b=rx.front();rx.pop_front();return b;}
 int available(){return rx.size();}
} uart;
int main(){
 DFRobotDFPlayerMini player;
 player.begin(uart,true,false);
 player.playMp3Folder(2);
 auto at=now;player.playMp3Folder(3);
 assert(now-at>=500); // Reproduce the installed library's missing ACK stall.
 auto count=uart.sent.size();
 player.begin(uart,false,false); // Exact firmware transition: no reset/command.
 assert(uart.sent.size()==count);
 at=now;player.playMp3Folder(4);
 assert(now-at==10 && uart.sent.back()[4]==0 && uart.sent.back()[6]==4);
 // Unsolicited track-finished messages still parse in no-ACK mode.
 std::vector<uint8_t> msg={0x7e,0xff,6,0x3d,0,0,4,0,0,0xef};
 uint16_t sum=0;for(int i=1;i<7;i++)sum+=msg[i];sum=-sum;
 msg[7]=sum>>8;msg[8]=sum;uart.rx.insert(uart.rx.end(),msg.begin(),msg.end());
 assert(player.available());assert(player.readType()==DFPlayerPlayFinished);
 assert(player.readCommand()==0x3d && player.read()==4);
 std::cout<<"PASS: reproduced 500 ms ACK stall; runtime no-ACK sends in 10 ms without reset and still receives song-finished events.\n";
}
'''
with tempfile.TemporaryDirectory(prefix='soundpod-transport-') as tmp:
 tmp=Path(tmp);(tmp/'Arduino.h').write_text(arduino);(tmp/'check.cpp').write_text(program)
 subprocess.run(['c++','-std=c++17','-I',str(tmp),'-I',str(lib),str(tmp/'check.cpp'),str(lib/'DFRobotDFPlayerMini.cpp'),'-o',str(tmp/'check')],check=True)
 subprocess.run([str(tmp/'check')],check=True)
