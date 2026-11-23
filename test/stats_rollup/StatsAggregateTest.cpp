// Statistics break on clocks, time zones and days that straddle midnight, not
// on arithmetic. Every case here is one of those, driven through the same
// foldLine() the firmware runs.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "stats/Achievements.h"
#include "stats/StatsAggregate.h"
#include "stats/StatsView.h"

namespace {

int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

#define CHECK_EQ(actual, expected)                                                                      \
  do {                                                                                                  \
    const long long a_ = static_cast<long long>(actual);                                                \
    const long long e_ = static_cast<long long>(expected);                                              \
    if (a_ != e_) {                                                                                     \
      std::printf("FAIL %s:%d  %s == %lld, expected %lld\n", __FILE__, __LINE__, #actual, a_, e_);      \
      ++failures;                                                                                       \
    }                                                                                                   \
  } while (0)

constexpr char kDocA[] = "0123456789abcdef0123456789abcdef";
constexpr char kDocB[] = "fedcba9876543210fedcba9876543210";

// 2024-01-01 00:00:00 UTC, a Monday.
constexpr int64_t kEpoch2024 = 1704067200;

std::string line(const char* doc, int64_t start, unsigned secs, int tz = 0, int from = 0, int to = 0) {
  char buffer[512];
  std::snprintf(buffer, sizeof(buffer),
                R"({"doc":"%s","start":%lld,"secs":%u,"tz":%d,"from":%d,"to":%d,"title":"T","path":"/b.epub"})", doc,
                static_cast<long long>(start), secs, tz, from, to);
  return buffer;
}

bool fold(stats::StatsAggregate& aggregate, const std::string& text, uint64_t offset = 0) {
  return stats::foldLine(aggregate, text.c_str(), text.size(), offset);
}

void testAcceptsAPlainSession() {
  stats::StatsAggregate a;
  stats::reset(a);
  CHECK(fold(a, line(kDocA, kEpoch2024, 600)));
  CHECK_EQ(a.sessionCount, 1);
  CHECK_EQ(a.totalSecs, 600);
  CHECK_EQ(a.longestSessionSecs, 600);
  CHECK_EQ(a.daysRead, 1);
  CHECK_EQ(a.bookCount, 1);
  CHECK_EQ(a.firstStart, kEpoch2024);
  CHECK_EQ(a.lastStart, kEpoch2024);
}

void testRejectsWhatCannotBePlaced() {
  stats::StatsAggregate a;
  stats::reset(a);
  // Clock never set: the session cannot belong to any day.
  CHECK(!fold(a, line(kDocA, 0, 600)));
  // Below the 10 s floor the firmware itself applies.
  CHECK(!fold(a, line(kDocA, kEpoch2024, 5)));
  // Longer than a week is a crash artefact, not reading.
  CHECK(!fold(a, line(kDocA, kEpoch2024, 8 * 86400)));
  // Not a 32-hex document id.
  CHECK(!fold(a, line("short", kEpoch2024, 600)));
  // Not JSON at all, and a truncated line from a power cut.
  CHECK(!fold(a, "this is not json"));
  CHECK(!fold(a, R"({"doc":"0123456789abcdef0123456789abcdef","sta)"));
  // A rejected line must leave every counter untouched.
  CHECK_EQ(a.sessionCount, 0);
  CHECK_EQ(a.totalSecs, 0);
  CHECK_EQ(a.daysRead, 0);
}

void testLocalDayFollowsTheOffset() {
  // 23:50 UTC on 2024-01-01 is still 2024-01-01 in UTC, but already the 2nd
  // at UTC+2 -- the reader's own day is the one that counts.
  const int64_t late = kEpoch2024 + 23 * 3600 + 50 * 60;
  const int32_t utcDay = stats::localDayIndex(late, 0);
  CHECK_EQ(stats::localDayIndex(late, 120), utcDay + 1);
  // And 00:30 UTC is still the previous evening at UTC-5.
  const int64_t early = kEpoch2024 + 30 * 60;
  CHECK_EQ(stats::localDayIndex(early, -300), stats::localDayIndex(early, 0) - 1);
  // Hours follow the same offset.
  CHECK_EQ(stats::localHour(late, 0), 23);
  CHECK_EQ(stats::localHour(late, 120), 1);
}

void testTwoSessionsOnOneDayAreOneDayRead() {
  stats::StatsAggregate a;
  stats::reset(a);
  fold(a, line(kDocA, kEpoch2024 + 3600, 600));
  fold(a, line(kDocA, kEpoch2024 + 7200, 900));
  CHECK_EQ(a.sessionCount, 2);
  CHECK_EQ(a.daysRead, 1);
  CHECK_EQ(a.totalSecs, 1500);
  const int32_t day = stats::localDayIndex(kEpoch2024, 0);
  CHECK_EQ(stats::minutesOnDay(a, day), 25);
}

void testStreaks() {
  stats::StatsAggregate a;
  stats::reset(a);
  const int32_t day0 = stats::localDayIndex(kEpoch2024, 0);
  for (int i = 0; i < 5; ++i) fold(a, line(kDocA, kEpoch2024 + static_cast<int64_t>(i) * 86400, 600));
  CHECK_EQ(stats::currentStreak(a, day0 + 4), 5);
  // Today with nothing yet does not break yesterday's streak.
  CHECK_EQ(stats::currentStreak(a, day0 + 5), 5);
  // Two days of silence does.
  CHECK_EQ(stats::currentStreak(a, day0 + 6), 0);
  CHECK_EQ(a.longestStreakDays, 5);

  // A later, shorter run must not lower the record.
  fold(a, line(kDocA, kEpoch2024 + 20 * 86400, 600));
  CHECK_EQ(a.longestStreakDays, 5);
  CHECK_EQ(stats::currentStreak(a, day0 + 20), 1);
}

void testOutOfOrderSessions() {
  stats::StatsAggregate a;
  stats::reset(a);
  // Newest first, which a clock correction can produce in an append-only log.
  fold(a, line(kDocA, kEpoch2024 + 2 * 86400, 600));
  fold(a, line(kDocA, kEpoch2024, 600));
  fold(a, line(kDocA, kEpoch2024 + 86400, 600));
  CHECK_EQ(a.sessionCount, 3);
  CHECK_EQ(a.daysRead, 3);
  CHECK_EQ(a.firstStart, kEpoch2024);
  CHECK_EQ(a.lastStart, kEpoch2024 + 2 * 86400);
  CHECK_EQ(stats::currentStreak(a, stats::localDayIndex(kEpoch2024, 0) + 2), 3);
}

void testRingDropsDaysOlderThanTheWindow() {
  stats::StatsAggregate a;
  stats::reset(a);
  fold(a, line(kDocA, kEpoch2024, 600));
  const int32_t day0 = stats::localDayIndex(kEpoch2024, 0);
  // Far past the window, which must clear the ring rather than alias onto it.
  const int64_t later = kEpoch2024 + static_cast<int64_t>(stats::kDayCount + 10) * 86400;
  fold(a, line(kDocA, later, 600));
  CHECK_EQ(stats::minutesOnDay(a, day0), 0);
  CHECK_EQ(stats::minutesOnDay(a, stats::localDayIndex(later, 0)), 10);
  // All-time totals are not limited by the window.
  CHECK_EQ(a.sessionCount, 2);
  CHECK_EQ(a.totalSecs, 1200);
  CHECK_EQ(a.daysRead, 2);
}

void testWindowQueries() {
  stats::StatsAggregate a;
  stats::reset(a);
  const int32_t day0 = stats::localDayIndex(kEpoch2024, 0);
  fold(a, line(kDocA, kEpoch2024, 600));                 // 10 min
  fold(a, line(kDocA, kEpoch2024 + 2 * 86400, 1800));    // 30 min, two days later
  const int32_t today = day0 + 2;
  CHECK_EQ(stats::minutesInWindow(a, today, 7), 40);
  CHECK_EQ(stats::daysReadInWindow(a, today, 7), 2);
  CHECK_EQ(stats::daysReadInWindow(a, today, 1), 1);
  CHECK_EQ(stats::minutesInWindow(a, today, 1), 30);
}

void testPerBookFigures() {
  stats::StatsAggregate a;
  stats::reset(a);
  fold(a, line(kDocA, kEpoch2024, 600, 0, 0, 2000), 100);
  fold(a, line(kDocB, kEpoch2024 + 3600, 300, 0, 0, 500), 400);
  fold(a, line(kDocA, kEpoch2024 + 86400, 900, 0, 2000, 6000), 900);

  const stats::BookStat* a1 = stats::findBook(a, kDocA);
  CHECK(a1 != nullptr);
  if (a1 != nullptr) {
    CHECK_EQ(a1->secs, 1500);
    CHECK_EQ(a1->sessions, 2);
    CHECK_EQ(a1->firstStart, kEpoch2024);
    CHECK_EQ(a1->lastStart, kEpoch2024 + 86400);
    CHECK_EQ(a1->maxTo, 6000);
    // The offset of the book's most recent line, for fetching its title.
    CHECK_EQ(a1->lastLineOffset, 900);
  }
  const stats::BookStat* b1 = stats::findBook(a, kDocB);
  CHECK(b1 != nullptr);
  if (b1 != nullptr) CHECK_EQ(b1->secs, 300);
  CHECK_EQ(a.bookCount, 2);
  // A book that was never read has no slot.
  CHECK(stats::findBook(a, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == nullptr);
}

void testProgressGoingBackwardsKeepsTheFurthestPoint() {
  stats::StatsAggregate a;
  stats::reset(a);
  fold(a, line(kDocA, kEpoch2024, 600, 0, 0, 9000));
  // Re-reading an earlier chapter must not undo how far the book was read.
  fold(a, line(kDocA, kEpoch2024 + 3600, 600, 0, 9000, 1000));
  const stats::BookStat* book = stats::findBook(a, kDocA);
  CHECK(book != nullptr);
  if (book != nullptr) CHECK_EQ(book->maxTo, 9000);
}

void testHourHistogram() {
  stats::StatsAggregate a;
  stats::reset(a);
  // 22:00 local at UTC+1 means 21:00 UTC.
  fold(a, line(kDocA, kEpoch2024 + 21 * 3600, 600, 60));
  CHECK_EQ(a.hourHistogram[22], 600);
  CHECK_EQ(a.hourHistogram[21], 0);
}

void testBookTableOverflowKeepsTheHeaviestBooks() {
  stats::StatsAggregate a;
  stats::reset(a);
  char doc[33];
  // Fill every slot with a one-minute book.
  for (int i = 0; i < stats::kBookCount; ++i) {
    std::snprintf(doc, sizeof(doc), "%032x", i);
    fold(a, line(doc, kEpoch2024, 60));
  }
  CHECK_EQ(a.bookCount, stats::kBookCount);

  // A heavier book evicts the weakest slot.
  CHECK(fold(a, line(kDocA, kEpoch2024, 3600)));
  const stats::BookStat* heavy = stats::findBook(a, kDocA);
  CHECK(heavy != nullptr);
  if (heavy != nullptr) CHECK_EQ(heavy->secs, 3600);

  // A lighter one does not, but its time still reaches the all-time totals.
  const uint64_t before = a.totalSecs;
  CHECK(fold(a, line(kDocB, kEpoch2024, 30)));
  CHECK(stats::findBook(a, kDocB) == nullptr);
  CHECK_EQ(a.totalSecs, before + 30);
}

void testSubMinuteSessionsStillMarkTheDay() {
  stats::StatsAggregate a;
  stats::reset(a);
  // 30 s rounds to 0 minutes, but the day was read and a streak must see it.
  fold(a, line(kDocA, kEpoch2024, 30));
  CHECK_EQ(stats::minutesOnDay(a, stats::localDayIndex(kEpoch2024, 0)), 1);
  CHECK_EQ(a.daysRead, 1);
}

void testExtremeTimezonesAreClamped() {
  stats::StatsAggregate a;
  stats::reset(a);
  // A corrupt tz must not index outside hourHistogram.
  CHECK(fold(a, line(kDocA, kEpoch2024 + 43200, 600, 100000)));
  CHECK(fold(a, line(kDocB, kEpoch2024 + 43200, 600, -100000)));
  uint32_t histogramTotal = 0;
  for (const uint32_t hour : a.hourHistogram) histogramTotal += hour;
  CHECK_EQ(histogramTotal, 1200);
}

// --- StatsView ---------------------------------------------------------------

void testFormatDuration() {
  char buffer[24];
  stats::formatDuration(buffer, sizeof(buffer), 0);
  CHECK(std::strcmp(buffer, "--") == 0);
  stats::formatDuration(buffer, sizeof(buffer), 47);
  CHECK(std::strcmp(buffer, "47 min") == 0);
  stats::formatDuration(buffer, sizeof(buffer), 60);
  CHECK(std::strcmp(buffer, "1 h 00") == 0);
  stats::formatDuration(buffer, sizeof(buffer), 65);
  CHECK(std::strcmp(buffer, "1 h 05") == 0);
  // Past a hundred hours the minutes are dropped rather than shrunk.
  stats::formatDuration(buffer, sizeof(buffer), 100 * 60 + 30);
  CHECK(std::strcmp(buffer, "100 h") == 0);
}

void testSummaryWithoutAClock() {
  stats::StatsAggregate a;
  stats::reset(a);
  fold(a, line(kDocA, kEpoch2024, 3600));
  // now == 0 is a device that never synced: all-time figures stand, windowed
  // ones stay zero rather than being bucketed against a meaningless day.
  const stats::Summary s = stats::summarize(a, 0, 0, 0, 0);
  CHECK_EQ(s.totalMinutes, 60);
  CHECK_EQ(s.daysRead, 1);
  CHECK_EQ(s.todayMinutes, 0);
  CHECK_EQ(s.weekMinutes, 0);
  CHECK_EQ(s.currentStreak, 0);
}

void testSummaryAverages() {
  stats::StatsAggregate a;
  stats::reset(a);
  // Two hours on day 0, one hour on day 4: five calendar days, two read.
  fold(a, line(kDocA, kEpoch2024, 7200));
  fold(a, line(kDocA, kEpoch2024 + 4 * 86400, 3600));
  const int64_t now = kEpoch2024 + 4 * 86400 + 7200;
  const stats::Summary s = stats::summarize(a, 2, 1, now, 0);
  CHECK_EQ(s.totalMinutes, 180);
  CHECK_EQ(s.avgMinutesPerReadDay, 90);   // 180 / 2 days read
  CHECK_EQ(s.avgMinutesPerCalendarDay, 36);  // 180 / 5 days elapsed
  CHECK_EQ(s.booksFinished, 2);
  CHECK_EQ(s.booksThisYear, 1);
  CHECK_EQ(s.booksStarted, 1);
}

void testMonthMapIsOldestFirst() {
  stats::StatsAggregate a;
  stats::reset(a);
  fold(a, line(kDocA, kEpoch2024, 600));              // 10 min, 2 days ago
  fold(a, line(kDocA, kEpoch2024 + 2 * 86400, 1800)); // 30 min, today
  const int32_t today = stats::localDayIndex(kEpoch2024, 0) + 2;
  uint16_t days[3];
  CHECK_EQ(stats::monthMap(a, today, 3, days, 3), 3);
  CHECK_EQ(days[0], 10);  // oldest
  CHECK_EQ(days[1], 0);
  CHECK_EQ(days[2], 30);  // today, last
}

void testHourBarsScale() {
  stats::StatsAggregate a;
  stats::reset(a);
  uint8_t bars[24];
  // An empty histogram must not divide by zero.
  stats::hourBars(a, 20, bars);
  for (const uint8_t b : bars) CHECK_EQ(b, 0);

  fold(a, line(kDocA, kEpoch2024 + 20 * 3600, 3600));  // an hour at 20:00
  fold(a, line(kDocA, kEpoch2024 + 9 * 3600, 60));     // a minute at 09:00
  stats::hourBars(a, 20, bars);
  CHECK_EQ(bars[20], 20);  // the peak is always full height
  // A tiny hour still gets a pixel: a bar that vanishes reads as no reading.
  CHECK(bars[9] >= 1);
  CHECK_EQ(bars[3], 0);
}

// --- achievements ------------------------------------------------------------

achievements::Inputs inputsWith(uint32_t books, uint32_t minutes, uint32_t streak, uint32_t longestSession,
                                uint32_t languages) {
  achievements::Inputs in;
  in.summary.booksFinished = books;
  in.summary.totalMinutes = minutes;
  in.summary.longestStreak = streak;
  in.summary.longestSessionMinutes = longestSession;
  in.distinctLanguages = languages;
  return in;
}

void testAchievementThresholds() {
  CHECK(!achievements::earned(achievements::Id::FirstBook, inputsWith(0, 0, 0, 0, 0)));
  CHECK(achievements::earned(achievements::Id::FirstBook, inputsWith(1, 0, 0, 0, 0)));
  CHECK(!achievements::earned(achievements::Id::Books10, inputsWith(9, 0, 0, 0, 0)));
  CHECK(achievements::earned(achievements::Id::Books10, inputsWith(10, 0, 0, 0, 0)));
  CHECK(!achievements::earned(achievements::Id::Hours10, inputsWith(0, 599, 0, 0, 0)));
  CHECK(achievements::earned(achievements::Id::Hours10, inputsWith(0, 600, 0, 0, 0)));
  CHECK(achievements::earned(achievements::Id::LongSession, inputsWith(0, 0, 0, 120, 0)));
  CHECK(achievements::earned(achievements::Id::ThreeLanguages, inputsWith(0, 0, 0, 0, 3)));
}

void testStreakAwardUsesTheRecord() {
  // A broken streak must not take an award back: the condition reads the
  // record, so a current run of zero still leaves a past 7-day run earned.
  achievements::Inputs in = inputsWith(0, 0, 7, 0, 0);
  in.summary.currentStreak = 0;
  CHECK(achievements::earned(achievements::Id::Streak7, in));
}

void testNewlyEarnedSkipsWhatIsHeld() {
  const achievements::Inputs in = inputsWith(10, 0, 0, 0, 0);
  bool held[achievements::kCount] = {};
  achievements::Id out[achievements::kCount];
  // Fresh: both the first-book and the ten-book awards land.
  CHECK_EQ(achievements::newlyEarned(in, held, out, achievements::kCount), 2);
  // Already held: nothing is reported twice.
  held[static_cast<size_t>(achievements::Id::FirstBook)] = true;
  held[static_cast<size_t>(achievements::Id::Books10)] = true;
  CHECK_EQ(achievements::newlyEarned(in, held, out, achievements::kCount), 0);
}

void testProgressPercent() {
  CHECK_EQ(achievements::progressPercent(achievements::Id::Books10, inputsWith(5, 0, 0, 0, 0)), 50);
  CHECK_EQ(achievements::progressPercent(achievements::Id::Books10, inputsWith(0, 0, 0, 0, 0)), 0);
  // Never over a hundred, however far past the target the reader is.
  CHECK_EQ(achievements::progressPercent(achievements::Id::Books10, inputsWith(999, 0, 0, 0, 0)), 100);
}

void testAwardKeysAreUniqueAndStable() {
  // A duplicate or empty key would make two awards share a slot in the saved
  // JSON, silently unlocking one with the other.
  for (size_t i = 0; i < achievements::kCount; ++i) {
    const char* a = achievements::key(static_cast<achievements::Id>(i));
    CHECK(a != nullptr && a[0] != '\0');
    for (size_t j = i + 1; j < achievements::kCount; ++j) {
      CHECK(std::strcmp(a, achievements::key(static_cast<achievements::Id>(j))) != 0);
    }
  }
  // Hours are stored in minutes but labelled in hours.
  CHECK_EQ(achievements::target(achievements::Id::Hours50), 50);
  CHECK_EQ(achievements::target(achievements::Id::Books25), 25);
}

}  // namespace

int main() {
  testAcceptsAPlainSession();
  testRejectsWhatCannotBePlaced();
  testLocalDayFollowsTheOffset();
  testTwoSessionsOnOneDayAreOneDayRead();
  testStreaks();
  testOutOfOrderSessions();
  testRingDropsDaysOlderThanTheWindow();
  testWindowQueries();
  testPerBookFigures();
  testProgressGoingBackwardsKeepsTheFurthestPoint();
  testHourHistogram();
  testBookTableOverflowKeepsTheHeaviestBooks();
  testSubMinuteSessionsStillMarkTheDay();
  testExtremeTimezonesAreClamped();
  testFormatDuration();
  testSummaryWithoutAClock();
  testSummaryAverages();
  testMonthMapIsOldestFirst();
  testHourBarsScale();
  testAchievementThresholds();
  testStreakAwardUsesTheRecord();
  testNewlyEarnedSkipsWhatIsHeld();
  testProgressPercent();
  testAwardKeysAreUniqueAndStable();

  if (failures == 0) {
    std::printf("ok stats_rollup  all checks passed\n");
    return 0;
  }
  std::printf("%d check(s) failed\n", failures);
  return 1;
}
