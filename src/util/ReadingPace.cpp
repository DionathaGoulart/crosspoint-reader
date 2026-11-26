#include "ReadingPace.h"

#include <Arduino.h>

#include <cstdint>

namespace readingpace {
namespace {
constexpr int WINDOW = 8;
constexpr int MIN_SAMPLES = 3;
// A page read in under 3 s was skimmed past; over 5 min the reader walked away.
constexpr uint32_t MIN_PAGE_S = 3;
constexpr uint32_t MAX_PAGE_S = 300;

uint16_t samples[WINDOW];
int sampleCount = 0;
int nextSlot = 0;
int lastPage = -1;
int lastPageCount = -1;
uint32_t lastTurnMs = 0;
}  // namespace

void notePage(const int page, const int pageCount) {
  const uint32_t now = millis();
  if (page == lastPage && pageCount == lastPageCount) return;
  if (pageCount == lastPageCount && page == lastPage + 1) {
    const uint32_t seconds = (now - lastTurnMs) / 1000;
    if (seconds >= MIN_PAGE_S && seconds <= MAX_PAGE_S) {
      samples[nextSlot] = static_cast<uint16_t>(seconds);
      nextSlot = (nextSlot + 1) % WINDOW;
      if (sampleCount < WINDOW) sampleCount++;
    }
  }
  lastPage = page;
  lastPageCount = pageCount;
  lastTurnMs = now;
}

int minutesLeft(const int pagesLeft) {
  if (pagesLeft <= 0 || sampleCount < MIN_SAMPLES) return -1;
  uint32_t total = 0;
  for (int i = 0; i < sampleCount; i++) total += samples[i];
  const uint32_t seconds = total * static_cast<uint32_t>(pagesLeft) / static_cast<uint32_t>(sampleCount);
  const int minutes = static_cast<int>((seconds + 59) / 60);
  return minutes < 1 ? 1 : minutes;
}

}  // namespace readingpace
