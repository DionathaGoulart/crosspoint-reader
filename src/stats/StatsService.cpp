#include "StatsService.h"

#include <Logging.h>
#include <Memory.h>
#include <TrustedTime.h>

#include <algorithm>
#include <ctime>
#include <set>
#include <string>

#include "AchievementStore.h"
#include "FinishedBooksStore.h"
#include "StatsRollup.h"

int statsService::utcOffsetMinutes(const int64_t at) {
  const time_t when = static_cast<time_t>(at);
  struct tm local {};
  struct tm utc {};
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

  // Distinct languages among finished books, for the matching achievement.
  // An unlabelled book counts as no language rather than as its own.
  std::set<std::string> languages;
  for (const auto& book : finished) {
    if (!book.language.empty()) languages.insert(book.language.substr(0, 2));
  }
  snapshot.inputs.summary = snapshot.summary;
  snapshot.inputs.distinctLanguages = static_cast<uint32_t>(languages.size());
  return snapshot;
}

size_t statsService::refreshAchievements(const Snapshot& snapshot, achievements::Id* out, const size_t capacity) {
  if (!snapshot.valid) return 0;
  ACHIEVEMENTS.loadFromFile();
  return ACHIEVEMENTS.evaluate(snapshot.inputs, trustedtime::trustedNow(), out, capacity);
}
