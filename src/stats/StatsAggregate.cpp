#include "StatsAggregate.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstring>
#include <iterator>

namespace stats {
namespace {

constexpr int32_t kNoDay = INT32_MIN;

bool isDocumentId(const char* document) {
  return document != nullptr && strlen(document) == 32 && strspn(document, "0123456789abcdefABCDEF") == 32;
}

int ringSlot(const int32_t day) {
  // C++ leaves the sign of % with a negative left operand, so normalise.
  return static_cast<int>(((day % kDayCount) + kDayCount) % kDayCount);
}

bool dayInRing(const StatsAggregate& aggregate, const int32_t day) {
  if (aggregate.lastDay == kNoDay) return false;
  return day <= aggregate.lastDay && day > aggregate.lastDay - kDayCount;
}

// Moves the ring forward to `day`, clearing the days passed over. Without this
// a slot still holds the minutes of the same weekday 730 days ago.
void advanceRing(StatsAggregate& aggregate, const int32_t day) {
  if (aggregate.lastDay == kNoDay) {
    std::fill(std::begin(aggregate.minutesPerDay), std::end(aggregate.minutesPerDay), 0);
    aggregate.lastDay = day;
    return;
  }
  if (day <= aggregate.lastDay) return;
  const int64_t gap = static_cast<int64_t>(day) - aggregate.lastDay;
  if (gap >= kDayCount) {
    std::fill(std::begin(aggregate.minutesPerDay), std::end(aggregate.minutesPerDay), 0);
  } else {
    for (int32_t d = aggregate.lastDay + 1; d <= day; ++d) aggregate.minutesPerDay[ringSlot(d)] = 0;
  }
  aggregate.lastDay = day;
}

// Longest run of consecutive days inside the ring. Recomputed after each fold
// rather than tracked incrementally, because sessions can arrive out of order
// and an incremental run counter cannot repair itself when they do. 730
// iterations over a byte array is nothing next to parsing the line.
uint32_t longestRunInRing(const StatsAggregate& aggregate) {
  if (aggregate.lastDay == kNoDay) return 0;
  uint32_t best = 0;
  uint32_t run = 0;
  const int32_t oldest = aggregate.lastDay - (kDayCount - 1);
  for (int32_t day = oldest; day <= aggregate.lastDay; ++day) {
    if (aggregate.minutesPerDay[ringSlot(day)] > 0) {
      ++run;
      best = std::max(best, run);
    } else {
      run = 0;
    }
  }
  return best;
}

// The slot for `hash`, creating it when there is room. When every slot is
// taken the weakest one is reused, but only for a book that already has more
// time than it -- so the table keeps the books worth showing instead of
// whichever was read last.
BookStat* slotFor(StatsAggregate& aggregate, const uint64_t hash, const uint32_t incomingSecs) {
  BookStat* weakest = nullptr;
  for (auto& book : aggregate.books) {
    if (book.docHash == hash) return &book;
    if (book.docHash == 0) {
      book = BookStat{};
      book.docHash = hash;
      ++aggregate.bookCount;
      return &book;
    }
    if (weakest == nullptr || book.secs < weakest->secs) weakest = &book;
  }
  if (weakest != nullptr && incomingSecs > weakest->secs) {
    *weakest = BookStat{};
    weakest->docHash = hash;
    return weakest;
  }
  return nullptr;
}

}  // namespace

uint64_t documentHash(const char* document) {
  // FNV-1a, 64-bit.
  uint64_t hash = 1469598103934665603ull;
  for (const char* p = document; p != nullptr && *p != '\0'; ++p) {
    hash ^= static_cast<uint8_t>(*p);
    hash *= 1099511628211ull;
  }
  // Zero marks an empty slot, so it can never be a real key.
  return hash == 0 ? 1 : hash;
}

int32_t localDayIndex(const int64_t startEpoch, const int tzMinutes) {
  const int64_t local = startEpoch + static_cast<int64_t>(tzMinutes) * 60;
  // Integer division truncates toward zero; days must floor.
  const int64_t day = local >= 0 ? local / 86400 : -(((-local) + 86399) / 86400);
  return static_cast<int32_t>(day);
}

int localHour(const int64_t startEpoch, const int tzMinutes) {
  const int64_t local = startEpoch + static_cast<int64_t>(tzMinutes) * 60;
  int64_t secondOfDay = local % 86400;
  if (secondOfDay < 0) secondOfDay += 86400;
  return static_cast<int>(secondOfDay / 3600);
}

void reset(StatsAggregate& aggregate) {
  aggregate = StatsAggregate{};
  aggregate.lastDay = kNoDay;
}

bool foldLine(StatsAggregate& aggregate, const char* line, const size_t length, const uint64_t offset) {
  if (line == nullptr || length == 0) return false;

  JsonDocument entry;
  if (deserializeJson(entry, line, length) != DeserializationError::Ok) return false;

  const char* document = entry["doc"] | "";
  if (!isDocumentId(document)) return false;

  const int64_t start = entry["start"] | static_cast<int64_t>(0);
  const uint32_t seconds = entry["secs"] | static_cast<uint32_t>(0);
  if (start < kMinStart || seconds < kMinSeconds || seconds > kMaxSeconds) return false;

  const int tz = std::clamp(entry["tz"] | 0, -720, 840);
  const int32_t day = localDayIndex(start, tz);

  // Totals first: they are all-time and do not depend on the ring window.
  aggregate.totalSecs += seconds;
  ++aggregate.sessionCount;
  aggregate.longestSessionSecs = std::max(aggregate.longestSessionSecs, seconds);
  if (aggregate.firstStart == 0 || start < aggregate.firstStart) aggregate.firstStart = start;
  aggregate.lastStart = std::max(aggregate.lastStart, start);
  aggregate.hourHistogram[localHour(start, tz)] += seconds;

  // The day ring. A session older than the window still counted above, but it
  // has no slot to land in and cannot affect a streak.
  advanceRing(aggregate, day);
  if (dayInRing(aggregate, day)) {
    const int slot = ringSlot(day);
    const bool newDay = aggregate.minutesPerDay[slot] == 0;
    // Minutes, not seconds: a u16 of seconds overflows after 18 hours, and the
    // month map is read in minutes anyway. Sub-minute sessions still mark the
    // day as read, which is what a streak asks.
    const uint32_t minutes = std::max<uint32_t>(1, seconds / 60);
    aggregate.minutesPerDay[slot] =
        static_cast<uint16_t>(std::min<uint32_t>(0xFFFF, aggregate.minutesPerDay[slot] + minutes));
    if (newDay) {
      // All-time, and only ever incremented, so it survives the ring wrapping
      // past the days it already counted. It is what folding observed: a day
      // whose only session arrives out of order, after the ring has moved more
      // than kDayCount days past it, is not counted.
      ++aggregate.daysRead;
      // Only on a new day, because nothing else can lengthen a run. Doing it
      // per line would cost 730 iterations per session during a rebuild.
      aggregate.longestStreakDays = std::max(aggregate.longestStreakDays, longestRunInRing(aggregate));
    }
  }

  BookStat* book = slotFor(aggregate, documentHash(document), seconds);
  if (book != nullptr) {
    book->secs += seconds;
    ++book->sessions;
    if (book->firstStart == 0 || start < book->firstStart) book->firstStart = start;
    if (start >= book->lastStart) {
      book->lastStart = start;
      book->lastLineOffset = static_cast<uint32_t>(std::min<uint64_t>(offset, UINT32_MAX));
    }
    const uint16_t to = static_cast<uint16_t>(std::min<int>(entry["to"] | 0, 10000));
    book->maxTo = std::max(book->maxTo, to);
  }
  return true;
}

uint16_t minutesOnDay(const StatsAggregate& aggregate, const int32_t day) {
  if (!dayInRing(aggregate, day)) return 0;
  return aggregate.minutesPerDay[ringSlot(day)];
}

uint32_t daysReadInWindow(const StatsAggregate& aggregate, const int32_t today, const int days) {
  uint32_t count = 0;
  for (int i = 0; i < days; ++i) {
    if (minutesOnDay(aggregate, today - i) > 0) ++count;
  }
  return count;
}

uint32_t minutesInWindow(const StatsAggregate& aggregate, const int32_t today, const int days) {
  uint32_t total = 0;
  for (int i = 0; i < days; ++i) total += minutesOnDay(aggregate, today - i);
  return total;
}

uint32_t currentStreak(const StatsAggregate& aggregate, const int32_t today) {
  if (aggregate.lastDay == kNoDay) return 0;
  // Today having no session yet does not break a streak; yesterday's does.
  int32_t day = minutesOnDay(aggregate, today) > 0 ? today : today - 1;
  uint32_t run = 0;
  while (minutesOnDay(aggregate, day) > 0) {
    ++run;
    --day;
  }
  return run;
}

const BookStat* findBook(const StatsAggregate& aggregate, const char* document) {
  if (!isDocumentId(document)) return nullptr;
  const uint64_t hash = documentHash(document);
  for (const auto& book : aggregate.books) {
    if (book.docHash == hash) return &book;
  }
  return nullptr;
}

}  // namespace stats
