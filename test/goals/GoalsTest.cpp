// Reading goals are arithmetic over a summary the firmware already builds, so
// what can break is the edges: a target of zero, a goal overshot, the rounding
// of a partial bar. Every case here is one of those.
#include <cassert>
#include <cstdio>

#include "stats/Goals.h"
#include "stats/StatsView.h"

namespace {

int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                 \
    }                                                             \
  } while (0)

#define CHECK_EQ(actual, expected)                                                                 \
  do {                                                                                             \
    const long long a_ = static_cast<long long>(actual);                                           \
    const long long e_ = static_cast<long long>(expected);                                         \
    if (a_ != e_) {                                                                                \
      std::printf("FAIL %s:%d  %s == %lld, expected %lld\n", __FILE__, __LINE__, #actual, a_, e_); \
      ++failures;                                                                                  \
    }                                                                                              \
  } while (0)

stats::Summary summaryWith(const uint32_t today, const uint32_t week, const uint32_t streak,
                           const uint32_t booksThisYear) {
  stats::Summary s;
  s.todayMinutes = today;
  s.weekMinutes = week;
  s.currentStreak = streak;
  s.booksThisYear = booksThisYear;
  return s;
}

// A zero target is the only "off" value, so nothing downstream needs a separate
// enabled flag. Everything else in Progress must stay zero, or a screen that
// trusts `enabled` would still draw a bar.
void testZeroTargetDisables() {
  const goals::Targets targets;  // all zero
  const auto summary = summaryWith(120, 600, 9, 4);

  for (size_t i = 0; i < goals::kCount; ++i) {
    const auto progress = goals::progressFor(static_cast<goals::Id>(i), targets, summary);
    CHECK(!progress.enabled);
    CHECK(!progress.met);
    CHECK_EQ(progress.percent, 0);
    CHECK_EQ(progress.current, 0);
    CHECK_EQ(progress.target, 0);
  }
  CHECK(!goals::anyEnabled(targets));
}

// Each goal must read its own field. A copy-paste slip here would show the
// daily figure under the weekly target and still look plausible on screen.
void testEachGoalReadsItsOwnField() {
  goals::Targets targets;
  targets.dailyMinutes = 60;
  targets.weeklyMinutes = 300;
  targets.streakDays = 30;
  targets.booksPerYear = 12;
  const auto summary = summaryWith(42, 210, 7, 3);

  CHECK_EQ(goals::currentFor(goals::Id::DailyMinutes, summary), 42);
  CHECK_EQ(goals::currentFor(goals::Id::WeeklyMinutes, summary), 210);
  CHECK_EQ(goals::currentFor(goals::Id::StreakDays, summary), 7);
  CHECK_EQ(goals::currentFor(goals::Id::BooksPerYear, summary), 3);

  CHECK_EQ(goals::targetFor(goals::Id::DailyMinutes, targets), 60);
  CHECK_EQ(goals::targetFor(goals::Id::WeeklyMinutes, targets), 300);
  CHECK_EQ(goals::targetFor(goals::Id::StreakDays, targets), 30);
  CHECK_EQ(goals::targetFor(goals::Id::BooksPerYear, targets), 12);

  CHECK(goals::anyEnabled(targets));
}

// 42 of 60 is 70%. Integer division truncates, which is the right direction for
// a progress bar: it must never report a goal as fuller than it is.
void testPartialProgressTruncates() {
  goals::Targets targets;
  targets.dailyMinutes = 60;

  const auto progress = goals::progressFor(goals::Id::DailyMinutes, targets, summaryWith(42, 0, 0, 0));
  CHECK(progress.enabled);
  CHECK(!progress.met);
  CHECK_EQ(progress.current, 42);
  CHECK_EQ(progress.target, 60);
  CHECK_EQ(progress.percent, 70);

  // 59/60 is 98.33%: it must not round up to 100 and read as met.
  const auto almost = goals::progressFor(goals::Id::DailyMinutes, targets, summaryWith(59, 0, 0, 0));
  CHECK(!almost.met);
  CHECK_EQ(almost.percent, 98);

  // One minute in on a 60-minute goal is 1%, not 0: a bar that shows nothing
  // after real reading reads as "today did not count".
  const auto barely = goals::progressFor(goals::Id::DailyMinutes, targets, summaryWith(1, 0, 0, 0));
  CHECK_EQ(barely.percent, 1);
}

// Exactly on target counts as met, and past it the bar caps instead of
// overrunning its box.
void testMetAndOvershootClamp() {
  goals::Targets targets;
  targets.dailyMinutes = 60;

  const auto exact = goals::progressFor(goals::Id::DailyMinutes, targets, summaryWith(60, 0, 0, 0));
  CHECK(exact.met);
  CHECK_EQ(exact.percent, 100);

  const auto over = goals::progressFor(goals::Id::DailyMinutes, targets, summaryWith(600, 0, 0, 0));
  CHECK(over.met);
  CHECK_EQ(over.percent, 100);
  // The real figure survives the clamp: only the bar is capped, so a screen can
  // still print "600/60".
  CHECK_EQ(over.current, 600);
}

// Nothing read yet is 0%, not a division fault.
void testZeroCurrentWithLiveTarget() {
  goals::Targets targets;
  targets.streakDays = 30;

  const auto progress = goals::progressFor(goals::Id::StreakDays, targets, summaryWith(0, 0, 0, 0));
  CHECK(progress.enabled);
  CHECK(!progress.met);
  CHECK_EQ(progress.percent, 0);
  CHECK_EQ(progress.target, 30);
}

// A single live goal is enough for a screen to have something to draw.
void testAnyEnabledFindsASingleGoal() {
  goals::Targets targets;
  CHECK(!goals::anyEnabled(targets));
  targets.booksPerYear = 1;
  CHECK(goals::anyEnabled(targets));
}

// The windowed figures are zero when the clock was never set (StatsView leaves
// them so deliberately). A goal must then read as untouched, never as met.
void testUnsetClockLeavesGoalsUnmet() {
  goals::Targets targets;
  targets.dailyMinutes = 30;
  targets.weeklyMinutes = 150;

  stats::Summary unset;  // what summarize() returns for now <= 0
  const auto daily = goals::progressFor(goals::Id::DailyMinutes, targets, unset);
  const auto weekly = goals::progressFor(goals::Id::WeeklyMinutes, targets, unset);
  CHECK(daily.enabled && !daily.met);
  CHECK(weekly.enabled && !weekly.met);
  CHECK_EQ(daily.percent, 0);
  CHECK_EQ(weekly.percent, 0);
}

}  // namespace

int main() {
  testZeroTargetDisables();
  testEachGoalReadsItsOwnField();
  testPartialProgressTruncates();
  testMetAndOvershootClamp();
  testZeroCurrentWithLiveTarget();
  testAnyEnabledFindsASingleGoal();
  testUnsetClockLeavesGoalsUnmet();

  if (failures != 0) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all goal checks passed\n");
  return 0;
}
