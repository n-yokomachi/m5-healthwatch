// HealthWatch: a voice workout coach for the M5Stack StopWatch. Runs from ota_1;
// the launcher menu (../menu) lives in ota_0.
#include <M5Unified.h>

#include "hw/health.h"
#include "shared.h"

void setup() {
  deviceBegin();
  hwSetup();
}

void loop() {
  M5.update();
  hwLoop();
}
