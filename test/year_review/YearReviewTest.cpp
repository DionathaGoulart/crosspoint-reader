// Year boundaries, the partial flag and first/last finishes in different years.
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>

#include "stats/FinishedBooksStore.h"
#include "stats/YearReview.h"

namespace {
int failures = 0;
void check(const bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}
int64_t epochOf(const int32_t day) { return static_cast<int64_t>(day) * 86400 + 12 * 3600; }
void setMinutes(stats::StatsAggregate& agg, const int32_t day, const uint16_t minutes) {
  agg.minutesPerDay[((day % stats::kDayCount) + stats::kDayCount) % stats::kDayCount] = minutes;
}
}  // namespace

int main() {
  setenv("TZ", "UTC", 1);
  tzset();
  auto agg = std::make_unique<stats::StatsAggregate>();
  stats::reset(*agg);
  const int32_t jan1 = yearreview::firstDayOf(2026);
  const int32_t today = jan1 + 328;  // 25 November 2026
  agg->lastDay = today;
  setMinutes(*agg, today, 40);
  setMinutes(*agg, today - 1, 30);
  setMinutes(*agg, today - 2, 20);
  setMinutes(*agg, jan1, 5);       // first day of the year counts
  setMinutes(*agg, jan1 - 1, 70);  // 31 December 2025 does not
  agg->firstStart = epochOf(jan1 - 1);

  std::vector<FinishedBook> books(2);
  books[0].timesRead = 1;
  books[0].finishedEpoch = books[0].lastFinishedEpoch = epochOf(today - 1);
  books[1].timesRead = 2;
  books[1].finishedEpoch = epochOf(yearreview::firstDayOf(2024) + 3);
  books[1].lastFinishedEpoch = epochOf(jan1 - 1);

  const auto years = yearreview::years(books, agg.get(), 2026);
  check(years.size() == 3 && years[0] == 2026 && years[1] == 2025 && years[2] == 2024, "years newest first");

  const auto y2026 = yearreview::build(2026, books, agg.get());
  check(y2026.minutes == 95, "2026 minutes");
  check(y2026.daysRead == 4, "2026 days");
  check(y2026.longestStreak == 3, "2026 streak");
  check(y2026.bestMonth == 11, "2026 best month");
  check(y2026.books.size() == 1 && y2026.books[0] == 0, "2026 books");
  check(!y2026.partial, "2026 not partial");

  const auto y2025 = yearreview::build(2025, books, agg.get());
  check(y2025.minutes == 70 && y2025.bestMonth == 12, "2025 last day");
  check(y2025.books.size() == 1 && y2025.books[0] == 1, "2025 latest finish");

  const auto y2024 = yearreview::build(2024, books, agg.get());
  check(y2024.books.size() == 1, "2024 first finish");
  check(!y2024.partial, "no reading before the ring: not partial");

  agg->firstStart = epochOf(today - stats::kDayCount - 5);
  check(yearreview::build(2024, books, agg.get()).partial, "reading before the ring: partial");

  if (failures != 0) return 1;
  std::printf("ok year_review  all checks passed\n");
  return 0;
}
