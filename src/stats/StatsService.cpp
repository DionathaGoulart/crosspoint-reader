#include "StatsService.h"

#include <Logging.h>
#include <Memory.h>
#include <TrustedTime.h>

#include <algorithm>
#include <ctime>
#include <string>

#include "AchievementStore.h"
#include "FinishedBooksStore.h"
#include "StatsRollup.h"

int statsService::utcOffsetMinutes(const int64_t at) {
  const time_t when = static_cast<time_t>(at);
  struct tm local{};
  struct tm utc{};
  localtime_r(&when, &local);
  gmtime_r(&when, &utc);
  // Reading the UTC fields as local time shifts by exactly the zone offset.
  // Same derivation as ReadingSessionLog, so the buckets line up.
  utc.tm_isdst = local.tm_isdst;
  return std::clamp(static_cast<int>((when - mktime(&utc)) / 60), -720, 840);
}

namespace {

// The two entry points below derive the same summary; only what they do with
// the aggregate afterwards differs. Kept in one place so the finished-book
// rules -- finishedCount() rather than size(), and the yearly goal count --
// are stated once and cannot drift apart.
stats::Summary summarizeWith(const stats::StatsAggregate& aggregate, const int64_t now, const int tz) {
  // Every change to the store saves, so the copy in memory is the file. Home
  // asks for this on every entry; without PSRAM it releases the store again.
  FINISHED_BOOKS.ensureLoaded();
  // finishedCount(), not size(): an entry can exist for a rating on a book that
  // was never read, and counting those inflates the yearly goal and the awards.
  const auto finishedCount = static_cast<uint32_t>(FINISHED_BOOKS.finishedCount());
  const uint32_t thisYear = now > 0 ? static_cast<uint32_t>(FINISHED_BOOKS.countTowardYearlyGoal(now)) : 0;
  return stats::summarize(aggregate, finishedCount, thisYear, now, tz);
}

}  // namespace

statsService::Snapshot statsService::load() {
  Snapshot snapshot;
  snapshot.aggregate = makeUniqueNoThrow<stats::StatsAggregate>();
  if (!snapshot.aggregate) {
    LOG_ERR("STATS", "no memory for the statistics aggregate (%u bytes)",
            static_cast<unsigned>(sizeof(stats::StatsAggregate)));
    return snapshot;
  }
  stats::reset(*snapshot.aggregate);
  snapshot.valid = statsRollup::load(*snapshot.aggregate);

  const int64_t now = trustedtime::trustedNow();
  snapshot.clockUnset = now <= 0;
  const int tz = now > 0 ? utcOffsetMinutes(now) : 0;

  snapshot.summary = summarizeWith(*snapshot.aggregate, now, tz);
  return snapshot;
}

stats::Summary statsService::summaryOnly(bool* ok) {
  auto aggregate = makeUniqueNoThrow<stats::StatsAggregate>();
  if (ok) *ok = aggregate != nullptr;
  if (!aggregate) {
    LOG_ERR("STATS", "no memory for the statistics aggregate (%u bytes)",
            static_cast<unsigned>(sizeof(stats::StatsAggregate)));
    return {};
  }
  stats::reset(*aggregate);
  // A failed rollup leaves an empty aggregate, not an empty summary: the
  // finished-book counts come from their own store and are still true. load()
  // reports them in that case, so this must too.
  statsRollup::load(*aggregate);

  const int64_t now = trustedtime::trustedNow();
  const int tz = now > 0 ? utcOffsetMinutes(now) : 0;
  return summarizeWith(*aggregate, now, tz);
}

size_t statsService::refreshAchievements(const Snapshot& snapshot, achievements::Id* out, const size_t capacity) {
  if (!snapshot.valid) return 0;
  ACHIEVEMENTS.loadFromFile();
  return ACHIEVEMENTS.evaluate(achievements::Inputs{snapshot.summary}, trustedtime::trustedNow(), out, capacity);
}
