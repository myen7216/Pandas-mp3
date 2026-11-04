#pragma once
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <initializer_list>

// No audio or hardware APIs: a reversible game with an independently tested
// simulation. Score belongs to this round only; no flash persistence.
struct BambooGame {
  struct Point { float x, y; };
  bool active = false, initialized = false, over = false;
  uint32_t score = 0, phaseMs = 0, randomState = 1;
  float x = 20, y = 32, enemyX = 105, enemyY = 35;
  bool rightward = true;
  Point food[3] = {};

  uint32_t random() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
  }
  static float distance2(float ax, float ay, float bx, float by) {
    const float dx = ax-bx, dy = ay-by;
    return dx*dx + dy*dy;
  }
  bool safeFood(int index, float fx, float fy) const {
    if (distance2(fx,fy,x+6,y+5) < 18*18 ||
        distance2(fx,fy,enemyX,enemyY) < 15*15) return false;
    for (int i = 0; i < 3; ++i)
      if (i != index && distance2(fx,fy,food[i].x,food[i].y) < 12*12) return false;
    return true;
  }
  void spawn(int index) {
    for (int attempt = 0; attempt < 100; ++attempt) {
      const float fx = 4 + random()%120, fy = 14 + random()%47;
      if (safeFood(index,fx,fy)) { food[index] = {fx,fy}; return; }
    }
    // Bounded deterministic fallback instead of an unbounded random loop.
    for (int fy = 14; fy <= 60; fy += 6)
      for (int fx = 4; fx <= 123; fx += 6)
        if (safeFood(index,fx,fy)) { food[index] = {(float)fx,(float)fy}; return; }
  }
  void restart() {
    initialized = true; over = false; phaseMs = 0; score = 0;
    x = 20; y = 32; enemyX = 105; enemyY = 35; rightward = true;
    for (auto &f : food) f = {-100,-100};
    for (int i = 0; i < 3; ++i) spawn(i);
  }
  void enter(uint32_t seed) {
    active = true;
    if (!initialized) { randomState = seed ? seed : 1; restart(); }
  }
  void leave() { active = false; }
  static float clamp(float value, float low, float high) {
    return value < low ? low : value > high ? high : value;
  }
  bool touchesPanda() const {
    // Union of the visible face/ears/body, against the smiley's 5px radius.
    if (distance2(enemyX,enemyY,x+6,y+4) <= 9*9 ||
        distance2(enemyX,enemyY,x+2,y+2) <= 7*7 ||
        distance2(enemyX,enemyY,x+10,y+2) <= 7*7) return true;
    const float nx = clamp(enemyX,x+3,x+9), ny = clamp(enemyY,y+7,y+10);
    return distance2(enemyX,enemyY,nx,ny) <= 25;
  }
  void update(uint32_t elapsedMs, uint8_t held) {
    if (!active || over || (held & 0x80)) return;
    uint32_t remaining = elapsedMs > 50 ? 50 : elapsedMs;
    phaseMs = (phaseMs + remaining) % 1200;
    float dx = ((held >> 1)&1) - (int)((held >> 2)&1);
    float dy = ((held >> 4)&1) - (int)((held >> 3)&1);
    const float length = sqrtf(dx*dx+dy*dy);
    if (length) { dx /= length; dy /= length; }
    if (dx) rightward = dx > 0;
    while (remaining && !over) {
      const uint32_t dt = remaining > 10 ? 10 : remaining;
      remaining -= dt;
      x = clamp(x + dx * 0.030f * dt,0,115);
      y = clamp(y + dy * 0.030f * dt,10,53);
      const float ex = x+6-enemyX, ey = y+5-enemyY;
      const float distance = sqrtf(ex*ex+ey*ey);
      if (distance > 0) {
        const float travel = fminf(distance,0.013f * dt);
        enemyX += ex/distance*travel; enemyY += ey/distance*travel;
      }
      if (touchesPanda()) { over = true; break; }
      for (int i = 0; i < 3; ++i) {
        if (distance2(x+6,y+5,food[i].x,food[i].y) <= 6.5f*6.5f) {
          if (score < UINT32_MAX) ++score;
          spawn(i);
        }
      }
    }
  }
  template<class Display> void draw(Display &d, uint8_t held) const {
    d.clearDisplay(); d.setFont(nullptr); d.setTextSize(1);
    d.setTextColor(1); d.setTextWrap(false); d.setCursor(2,1);
    char label[11]; snprintf(label,sizeof(label),"%lu",(unsigned long)score);
    d.print(label);
    const bool large = phaseMs >= 300 && phaseMs < 900;
    for (const auto &f : food) {
      const int fx = (int)lroundf(f.x), fy = (int)lroundf(f.y);
      const int halfHeight = large ? 3 : 2;
      d.drawLine(fx,fy-halfHeight,fx,fy+halfHeight,1);
      if (large) d.fillTriangle(fx+1,fy,fx+3,fy-2,fx+2,fy,1);
      else d.drawLine(fx+1,fy,fx+2,fy-1,1);
    }
    const int px = (int)lroundf(x), py = (int)lroundf(y);
    const bool walking = !over && (held & 30) && !(held & 0x80);
    const bool step = phaseMs/160%2;
    d.fillRoundRect(px+3,py+7,7,3,1,1);
    d.drawPixel(px+(step && walking ? 3 : 4),py+10,1);
    d.drawPixel(px+(step && walking ? 8 : 9),py+10,1);
    d.fillCircle(px+2,py+2,2,1); d.fillCircle(px+10,py+2,2,1);
    d.fillCircle(px+2,py+2,1,0); d.fillCircle(px+10,py+2,1,0);
    d.fillCircle(px+6,py+4,4,1);
    for (int eye : {4,8}) {
      d.fillCircle(px+eye,py+4,1,0);
      if (!over) d.drawPixel(px+eye+(rightward ? 1 : -1),py+4,1);
      else {
        d.drawLine(px+eye-1,py+3,px+eye+1,py+5,0);
        d.drawLine(px+eye+1,py+3,px+eye-1,py+5,0);
      }
    }
    d.drawPixel(px+6,py+6,0);
    const int sx = (int)lroundf(enemyX), sy = (int)lroundf(enemyY);
    d.drawCircle(sx,sy,5,1);
    d.drawPixel(sx-2,sy-1,1); d.drawPixel(sx+2,sy-1,1);
    d.drawLine(sx-2,sy+1,sx-1,sy+2,1);
    d.drawLine(sx-1,sy+2,sx+1,sy+2,1);
    d.drawLine(sx+1,sy+2,sx+2,sy+1,1);
    if (over) {
      // Retain the final round score until an explicit Play/Pause restart.
      d.fillRect(0,12,128,40,0);
      d.setCursor(37,14); d.print("GAME OVER");
      char finalScore[24];
      const int length = snprintf(finalScore,sizeof(finalScore),"Score: %lu",(unsigned long)score);
      d.setCursor((128-length*6)/2,26); d.print(finalScore);
      d.setCursor(25,40); d.print("PLAY: RESTART");
    }
    d.display();
  }
};
