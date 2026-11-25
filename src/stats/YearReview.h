#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "StatsAggregate.h"

struct FinishedBook;

// One calendar year of reading, for the retrospective in Statistics.
//
// Reading time comes from the aggregate's daily ring, which keeps only the last
// kDayCount days; a year older than that has its books but not its time, and
// `partial` says so. Books come from FinishedBooksStore, which keeps the first
// and the latest finish of each book: a book read three times shows in the
// years of those two finishes only.
namespace yearreview {

struct Year {
  int year = 0;
  uint32_t minutes = 0;
  uint32_t daysRead = 0;
  uint32_t longestStreak = 0;
  // 1..12, 0 when no month had reading.
  int bestMonth = 0;
  uint32_t bestMonthMinutes = 0;
  // True when part of the year is older than the daily ring.
  bool partial = false;
  // Indices into the finished-books list, in its order (newest first).
  std::vector<size_t> books;
};

// Years with any finished book or any reading in the ring, plus `currentYear`
// when it is above zero; newest first.
std::vector<int> years(const std::vector<FinishedBook>& finished, const stats::StatsAggregate* aggregate,
                       int currentYear);

Year build(int year, const std::vector<FinishedBook>& finished, const stats::StatsAggregate* aggregate);

// Local-day index of 1 January of `year`.
int32_t firstDayOf(int year);

}  // namespace yearreview
