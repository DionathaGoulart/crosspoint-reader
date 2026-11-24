#include "StatsView.h"

#include <algorithm>

namespace stats {

Summary summarize(const StatsAggregate& aggregate, const uint32_t booksFinished, const uint32_t booksThisYear,
                  const int64_t now, const int tzMinutes) {
  Summary s;
  s.totalMinutes = static_cast<uint32_t>(aggregate.totalSecs / 60);
  s.daysRead = aggregate.daysRead;
  s.sessionCount = aggregate.sessionCount;
  s.longestSessionMinutes = aggregate.longestSessionSecs / 60;
  s.longestStreak = aggregate.longestStreakDays;
  s.booksFinished = booksFinished;
  s.booksThisYear = booksThisYear;
  s.firstStart = aggregate.firstStart;

  for (const auto& book : aggregate.books) {
    if (book.docHash != 0) ++s.booksStarted;
  }

  uint32_t bestSecs = 0;
  for (int hour = 0; hour < kHourCount; ++hour) {
    if (aggregate.hourHistogram[hour] > bestSecs) {
      bestSecs = aggregate.hourHistogram[hour];
      s.bestHour = static_cast<int8_t>(hour);
    }
  }

  if (s.daysRead > 0) s.avgMinutesPerReadDay = s.totalMinutes / s.daysRead;

  // Without a clock there is no "today", so the windowed figures stay zero
  // rather than being computed against a day index that means nothing.
  if (now <= 0) return s;

  s.today = localDayIndex(now, tzMinutes);
  s.todayMinutes = minutesOnDay(aggregate, s.today);
  s.weekMinutes = minutesInWindow(aggregate, s.today, 7);
  s.monthMinutes = minutesInWindow(aggregate, s.today, 30);
  s.currentStreak = currentStreak(aggregate, s.today);

  if (aggregate.firstStart > 0) {
    const int32_t firstDay = localDayIndex(aggregate.firstStart, tzMinutes);
    const int32_t span = s.today - firstDay + 1;
    if (span > 0) s.avgMinutesPerCalendarDay = s.totalMinutes / static_cast<uint32_t>(span);
  }
  return s;
}

DurationParts splitDuration(const uint32_t minutes) {
  DurationParts parts;
  if (minutes == 0) return parts;
  if (minutes < 60) {
    parts.unit = DurationParts::Minutes;
    parts.minutes = minutes;
    return parts;
  }
  parts.hours = minutes / 60;
  // Past a hundred hours the minutes stop being interesting and the tile runs
  // out of room, so they are dropped rather than shrunk.
  if (parts.hours >= 100) {
    parts.unit = DurationParts::Hours;
    return parts;
  }
  parts.unit = DurationParts::HoursAndMinutes;
  parts.minutes = minutes % 60;
  return parts;
}

size_t monthMap(const StatsAggregate& aggregate, const int32_t today, const int days, uint16_t* out,
                const size_t capacity) {
  if (out == nullptr || days <= 0) return 0;
  const size_t count = std::min(static_cast<size_t>(days), capacity);
  for (size_t i = 0; i < count; ++i) {
    // Oldest first, so the newest cell is the last one drawn.
    const int32_t day = today - static_cast<int32_t>(count - 1 - i);
    out[i] = minutesOnDay(aggregate, day);
  }
  return count;
}

void hourBars(const StatsAggregate& aggregate, const uint8_t height, uint8_t* out24) {
  if (out24 == nullptr) return;
  uint32_t peak = 0;
  for (int hour = 0; hour < kHourCount; ++hour) peak = std::max(peak, aggregate.hourHistogram[hour]);
  for (int hour = 0; hour < kHourCount; ++hour) {
    if (peak == 0) {
      out24[hour] = 0;
      continue;
    }
    // Rounded, and any hour with reading gets at least one pixel: an hour that
    // disappears reads as an hour with nothing in it.
    const uint32_t scaled = (aggregate.hourHistogram[hour] * height + peak / 2) / peak;
    out24[hour] = static_cast<uint8_t>(aggregate.hourHistogram[hour] > 0 ? std::max<uint32_t>(1, scaled) : 0);
  }
}

}  // namespace stats
