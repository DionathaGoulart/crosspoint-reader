#pragma once

#include <memory>

#include "Achievements.h"
#include "StatsAggregate.h"
#include "StatsView.h"

// One call that gets a screen everything it needs: the folded aggregate, the
// derived summary, and the achievement inputs.
//
// The aggregate is ~22 KB and lives in the returned holder, on the heap. A
// screen keeps the Snapshot alive for as long as it draws from it and lets it
// go afterwards; nothing here is cached between calls, because the alternative
// is 22 KB resident for a screen most readers open rarely.
namespace statsService {

struct Snapshot {
  std::unique_ptr<stats::StatsAggregate> aggregate;
  stats::Summary summary{};
  achievements::Inputs inputs{};
  // False when the aggregate could not be read or allocated. The summary is
  // then all zeros, which a screen shows as an empty state rather than an error.
  bool valid = false;
  // True when the clock has never been set, so the windowed figures (today,
  // week, streak) are zero for that reason and not for lack of reading. Worth
  // saying on screen: it is a fixable condition, not an empty history.
  bool clockUnset = false;
};

Snapshot load();

// Just the derived figures, for a screen that draws targets rather than the
// whole statistics surface. Skips the achievement inputs -- the distinct
// languages walk over every finished book and the std::set it builds -- and
// releases the ~22 KB aggregate before returning, so nothing of it is held
// while the caller redraws.
//
// The home screen is entered on every return from a book, the library or
// settings, so this runs far more often than load() was written for.
stats::Summary summaryOnly();

// The reader's current UTC offset in minutes, the same way the session log
// records it. Exposed because a screen formatting dates needs the same offset
// the figures were bucketed with.
int utcOffsetMinutes(int64_t at);

// Re-derives the achievements from `snapshot` and records any newly earned.
// Returns how many were newly unlocked. Safe to call whenever a screen opens.
size_t refreshAchievements(const Snapshot& snapshot, achievements::Id* out = nullptr, size_t capacity = 0);

}  // namespace statsService
