#pragma once
// HealthWatch: a spoken check-in with the coach agent, then the workout.
#include <Arduino.h>

#include "hw/workout.h"

void hwSetup();  // after the screen, fonts, rec_buf, mic and Wi-Fi are set up
void hwLoop();   // after M5.update()

// ---- shared by health.cpp (flow) and screens.cpp (drawing) ----

// Suggest and Menu follow the "stage" of the agent's response; the rest is the workout
enum class HwStage : uint8_t { Suggest, Menu, Exercise, ItemDone, Next, Done };
enum class HwPhase : uint8_t { Idle, Recording, Sending };
// shown as an icon: Wi-Fi is down, or anything else on the way to the agent or in its answer
enum class HwError : uint8_t { None, Wifi, Cloud };
// what a yellow press does now, shown as the yellow key icon
enum class HwYellow : uint8_t { None, Talk, Retry };

struct HwView {
  HwStage stage = HwStage::Suggest;
  HwPhase phase = HwPhase::Sending;
  String reply;                 // the agent's last reply
  String heard;                 // what the agent heard in the last recording
  HwError error = HwError::None;
  HwYellow yellow = HwYellow::None;
  const Menu* menu = nullptr;   // set in the Menu stage and the workout stages
  const Workout* workout = nullptr;  // set in the workout stages
  uint32_t now = 0;             // millis() of this frame, for the workout clocks
  uint32_t itemDoneAt = 0;      // millis() when the last item ended, for the completion animation
  int scroll = 0;               // pixels the menu list is dragged up, 0 to hwMenuScrollMax
};

void hwScreensBegin();  // once, after the screen canvas exists
void hwDraw(const HwView& v);
int hwMenuScrollMax(const Menu& m);  // 0 when the whole list fits
