#pragma once

#include <Arduino.h>
#include <GfxRenderer.h>

#include "components/UITheme.h"

// A loading popup whose dots advance (. .. ...) while a blocking job runs.
// The job reports progress as often as it likes; redraws are rate-limited
// because each one is an e-ink refresh that would otherwise slow the job.
class BusyIndicator {
 public:
  BusyIndicator(const GfxRenderer& renderer, const char* message) : renderer(renderer), message(message) { draw(); }

  // Matches library::ProgressFn; `self` is the BusyIndicator.
  // `books` is the running count; the job knows no total, so the popup shows the count.
  static void onProgress(void* self, uint16_t books) {
    auto* busy = static_cast<BusyIndicator*>(self);
    busy->count = books;
    busy->tick();
  }

  void tick() {
    if (millis() - lastDrawMs >= MIN_STEP_MS) draw();
  }

 private:
  static constexpr uint32_t MIN_STEP_MS = 1800;

  void draw() {
    if (count > 0) {
      // The popup appends its own animated dots, so the message goes in without its trailing ones.
      int length = static_cast<int>(strlen(message));
      while (length > 0 && message[length - 1] == '.') --length;
      snprintf(text, sizeof(text), "%.*s (%u)", length, message, static_cast<unsigned>(count));
      GUI.drawBusyPopup(renderer, text, step++);
    } else {
      GUI.drawBusyPopup(renderer, message, step++);
    }
    lastDrawMs = millis();
  }

  const GfxRenderer& renderer;
  const char* message;
  uint8_t step = 0;
  uint16_t count = 0;
  char text[64];
  uint32_t lastDrawMs = 0;
};
