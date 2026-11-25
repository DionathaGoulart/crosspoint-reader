#include "YearReview.h"

#include <algorithm>
#include <climits>
#include <ctime>

#include "FinishedBooksStore.h"

namespace {

// Days since 1970-01-01 for a civil date (Howard Hinnant's days_from_civil).
int32_t daysFromCivil(int year, const unsigned month, const unsigned day) {
  year -= month <= 2 ? 1 : 0;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

int localYear(const int64_t epoch) {
  const time_t at = static_cast<time_t>(epoch);
  struct tm local{};
  localtime_r(&at, &local);
  return local.tm_year + 1900;
}

bool finishedIn(const FinishedBook& book, const int year) {
  if (book.timesRead == 0 || book.finishedEpoch <= 0) return false;
  if (localYear(book.finishedEpoch) == year) return true;
  return book.lastFinishedEpoch != book.finishedEpoch && book.lastFinishedEpoch > 0 &&
         localYear(book.lastFinishedEpoch) == year;
}

bool ringEmpty(const stats::StatsAggregate* aggregate) {
  return aggregate == nullptr || aggregate->lastDay == INT32_MIN;
}

int32_t ringFirstDay(const stats::StatsAggregate& aggregate) { return aggregate.lastDay - stats::kDayCount + 1; }

}  // namespace

int32_t yearreview::firstDayOf(const int year) { return daysFromCivil(year, 1, 1); }

std::vector<int> yearreview::years(const std::vector<FinishedBook>& finished, const stats::StatsAggregate* aggregate,
                                   const int currentYear) {
  std::vector<int> out;
  const auto addYear = [&out](const int year) {
    if (year > 1999 && std::find(out.begin(), out.end(), year) == out.end()) out.push_back(year);
  };
  if (currentYear > 0) addYear(currentYear);
  for (const FinishedBook& book : finished) {
    if (book.timesRead == 0) continue;
    if (book.finishedEpoch > 0) addYear(localYear(book.finishedEpoch));
    if (book.lastFinishedEpoch > 0) addYear(localYear(book.lastFinishedEpoch));
  }
  if (!ringEmpty(aggregate)) {
    // One probe per year the ring spans: a year with reading but no finished
    // book still belongs in the list.
    const int32_t first = ringFirstDay(*aggregate);
    for (int year = currentYear > 0 ? currentYear : 2100; year > 1999; --year) {
      const int32_t start = firstDayOf(year);
      const int32_t end = firstDayOf(year + 1) - 1;
      if (end < first) break;
      if (start > aggregate->lastDay) continue;
      for (int32_t day = std::max(start, first); day <= std::min(end, aggregate->lastDay); ++day) {
        if (stats::minutesOnDay(*aggregate, day) > 0) {
          addYear(year);
          break;
        }
      }
    }
  }
  std::sort(out.begin(), out.end(), [](const int a, const int b) { return a > b; });
  return out;
}

yearreview::Year yearreview::build(const int year, const std::vector<FinishedBook>& finished,
                                   const stats::StatsAggregate* aggregate) {
  Year out;
  out.year = year;
  for (size_t i = 0; i < finished.size(); ++i) {
    if (finishedIn(finished[i], year)) out.books.push_back(i);
  }
  if (ringEmpty(aggregate)) return out;

  const int32_t ringFirst = ringFirstDay(*aggregate);
  const int32_t start = firstDayOf(year);
  const int32_t end = firstDayOf(year + 1) - 1;
  // Reading before the ring began cannot be shown; say so only when there was
  // some, so a reader who started this year sees no warning.
  const int64_t ringFirstEpoch = static_cast<int64_t>(ringFirst) * 86400;
  out.partial = start < ringFirst && aggregate->firstStart > 0 && aggregate->firstStart < ringFirstEpoch;

  uint32_t monthMinutes[12] = {};
  uint32_t streak = 0;
  for (int32_t day = std::max(start, ringFirst); day <= std::min(end, aggregate->lastDay); ++day) {
    const uint16_t minutes = stats::minutesOnDay(*aggregate, day);
    if (minutes == 0) {
      streak = 0;
      continue;
    }
    out.minutes += minutes;
    ++out.daysRead;
    ++streak;
    out.longestStreak = std::max(out.longestStreak, streak);
    const time_t at = static_cast<time_t>(day) * 86400;
    struct tm civil{};
    gmtime_r(&at, &civil);
    monthMinutes[civil.tm_mon] += minutes;
  }
  for (int month = 0; month < 12; ++month) {
    if (monthMinutes[month] > out.bestMonthMinutes) {
      out.bestMonthMinutes = monthMinutes[month];
      out.bestMonth = month + 1;
    }
  }
  return out;
}
