#pragma once

#include <cstddef>
#include <cstdint>

#include "StatsAggregate.h"

// The numbers a screen shows, derived from the aggregate in one place.
//
// Pure, like StatsAggregate: no clock of its own, no storage, no renderer. The
// caller passes the current time and offset, so the same figures can be tested
// on the host and drawn by both the statistics tab and the sleep screen without
// either of them re-deriving anything.
namespace stats {

struct Summary {
  uint32_t totalMinutes = 0;
  uint32_t todayMinutes = 0;
  uint32_t weekMinutes = 0;   // today and the six days before it
  uint32_t monthMinutes = 0;  // today and the twenty-nine days before it
  uint32_t currentStreak = 0;
  uint32_t longestStreak = 0;
  uint32_t daysRead = 0;
  uint32_t sessionCount = 0;
  uint32_t longestSessionMinutes = 0;
  // Total divided by the days that had reading, and by the days since the
  // first session. Both are true and they answer different questions, so a
  // screen that shows only one is choosing which story to tell.
  uint32_t avgMinutesPerReadDay = 0;
  uint32_t avgMinutesPerCalendarDay = 0;
  // Hour of day, 0..23, with the most seconds recorded. -1 when nothing is.
  int8_t bestHour = -1;
  uint32_t booksStarted = 0;   // books with any reading time
  uint32_t booksFinished = 0;  // from FinishedBooksStore, passed in
  uint32_t booksThisYear = 0;
  int64_t firstStart = 0;
  int32_t today = 0;  // local day index the figures were built for
};

// `now` is Unix time and `tzMinutes` the reader's current offset. A `now` of 0
// (clock never set) yields a summary with today's figures zeroed but the
// all-time ones intact, which is the honest reading.
Summary summarize(const StatsAggregate& aggregate, uint32_t booksFinished, uint32_t booksThisYear, int64_t now,
                  int tzMinutes);

// "1 h 05" above an hour, "47 min" below it, "--" for zero. Sized for an
// e-ink tile, so it never spells out words that would need translating.
void formatDuration(char* out, size_t size, uint32_t minutes);

// One row of the month map: `days` cells ending at `summary.today`, each the
// minutes read that day, oldest first. Returns how many cells were written.
size_t monthMap(const StatsAggregate& aggregate, int32_t today, int days, uint16_t* out, size_t capacity);

// Hour histogram scaled to 0..`height`, for drawing. The tallest bar is always
// `height`, so an empty histogram writes zeros rather than dividing by zero.
void hourBars(const StatsAggregate& aggregate, uint8_t height, uint8_t* out24);

}  // namespace stats
