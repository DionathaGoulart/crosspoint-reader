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

  FINISHED_BOOKS.loadFromFile();
  const auto& finished = FINISHED_BOOKS.getBooks();
  const uint32_t finishedCount = static_cast<uint32_t>(finished.size());
  const uint32_t thisYear = now > 0 ? static_cast<uint32_t>(FINISHED_BOOKS.countInYear(now)) : 0;

  snapshot.summary = stats::summarize(*snapshot.aggregate, finishedCount, thisYear, now, tz);
  return snapshot;
}

stats::Summary statsService::summaryOnly() {
  auto aggregate = makeUniqueNoThrow<stats::StatsAggregate>();
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
  FINISHED_BOOKS.loadFromFile();
  const auto finishedCount = static_cast<uint32_t>(FINISHED_BOOKS.count());
  const uint32_t thisYear = now > 0 ? static_cast<uint32_t>(FINISHED_BOOKS.countInYear(now)) : 0;
  return stats::summarize(*aggregate, finishedCount, thisYear, now, tz);
}

size_t statsService::refreshAchievements(const Snapshot& snapshot, achievements::Id* out, const size_t capacity) {
  if (!snapshot.valid) return 0;
  ACHIEVEMENTS.loadFromFile();
  return ACHIEVEMENTS.evaluate(achievements::Inputs{snapshot.summary}, trustedtime::trustedNow(), out, capacity);
}
