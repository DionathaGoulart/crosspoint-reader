#pragma once

#include <Arduino.h>
#include <GfxRenderer.h>

#include "components/UITheme.h"

// A loading popup whose dots advance (. .. ...) while a blocking job runs.
// The job reports progress as often as it likes; redraws are rate-limited
// because each one is an e-ink refresh that would otherwise slow the job.
class BusyIndicator {
 public:
  BusyIndicator(const GfxRenderer& renderer, const char* message) : renderer(renderer), message(message) {
    draw();
  }

  // Matches library::ProgressFn; `self` is the BusyIndicator.
  static void onProgress(void* self, uint16_t) { static_cast<BusyIndicator*>(self)->tick(); }

  void tick() {
    if (millis() - lastDrawMs >= MIN_STEP_MS) draw();
  }

 private:
  static constexpr uint32_t MIN_STEP_MS = 1800;

  void draw() {
    GUI.drawBusyPopup(renderer, message, step++);
    lastDrawMs = millis();
  }

  const GfxRenderer& renderer;
  const char* message;
  uint8_t step = 0;
  uint32_t lastDrawMs = 0;
};
