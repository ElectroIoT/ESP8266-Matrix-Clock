// 32x8 frame buffer, text, clock face and animations on top of MD_MAX72XX
#pragma once

#include <Arduino.h>
#include <time.h>

static const int W = 32;   // display width in pixels
static const int H = 8;    // display height in pixels

// Provided by main.cpp: waits while keeping WiFi / web server responsive.
void serviceWait(uint32_t ms);

// ---- low level ----------------------------------------------------------------
void displayBegin();
void displayIntensity(uint8_t level);   // 0..15
void displayPower(bool on);
void fbClear();
void fbSet(int x, int y, bool on);      // x: 0 = left, y: 0 = top; out-of-range is ignored
void fbShow();                          // push the frame buffer to the modules

// ---- text (library system font) ------------------------------------------------
int  textWidth(const String& s);
void drawText(int x, const String& s);  // left edge at x, clipped to the display
void scrollText(const String& s);       // blocking right-to-left scroll across the display

// Non-blocking scroller: call step() once per frame, returns true after one full pass.
struct Marquee {
    String text;
    int    pos = 0;
    void   start(const String& s) { text = s; pos = 0; }
    bool   step();
};

// ---- clock face -----------------------------------------------------------------
struct ClockFace {
    void reset();                                        // next draw shows digits without animating
    void demo();                                         // animate all digits once (preview on the web page)
    void draw(const struct tm& t, uint16_t ms, int dx);  // dx shifts the face horizontally (slide transitions)
    void tick();                                         // advance digit animations, once per frame
private:
    void    drawSlot(int i, int x);
    uint8_t cur[4]   = {0};
    uint8_t prev[4]  = {0};
    uint8_t phase[4] = {0};   // frames of the change animation left, 0 = settled
    uint8_t style = 0;        // Roll style of the running animation (a fixed pick when set to Random)
    bool    fresh = true;
    bool    demoPending = false;
};

// ---- animations (blocking, ~2-4 s) ---------------------------------------------
void playAnim(uint8_t anim);            // Anim value; ANIM_RANDOM picks one
void spinnerFrame(uint16_t n);          // one frame of a "working..." bouncing bar
