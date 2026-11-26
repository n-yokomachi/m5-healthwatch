#pragma once
// Device setup and drawing helpers shared by the HealthWatch flow and screens (hw/).
#include <M5Unified.h>
#include <vector>

// M5, the screen canvas and its font, the recording buffer, the mic task and Wi-Fi
void deviceBegin();

// drawing
extern M5Canvas screen;  // full-screen frame buffer (PSRAM)
extern int W, H, CX, CY;

// eyes
void drawEyes(int cx, int cy, float scale);  // scale 1 = full size
void updateEyes(uint32_t now);
void lookAt(float x, float y);  // gaze in -1..1
void drawLevel(int cy);         // voice level bar from `level`
extern volatile int level;

// text in the Noto font; characters missing from it are drawn with M5GFX's built-in font
void drawLine(M5Canvas& dst, const String& line, int x, int y, uint16_t color);  // top-left datum
int lineWidth(M5Canvas& dst, const String& line);
void wrapLines(const String& s, int maxW, std::vector<String>& out);

// recording (micTask fills rec_buf while micOn)
extern int16_t* rec_buf;
extern volatile size_t rec_len;
extern volatile bool micOn;
extern volatile bool micIdle;

void switchToMenu();  // reboots into the launcher (ota_0)
