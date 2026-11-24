// The reread rule and the yearly-goal rule, on the host.
//
// Both are calendar rules over a persisted record, which is where they break:
// which finish is dated, whether a reread was armed, and what an older file
// without the `reads` field means. The store is linked for real; only its
// persistence base, the document-id digest, the credential store and the clock
// are stubbed, so the rules under test are the shipped code.
//
//   test/finished_books/run.sh
#include <HalStorage.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "stats/FinishedBooksStore.h"

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

// Noon UTC on each date, so no zone within +/-12h moves the day.
constexpr int64_t FIRST_READ_2025 = 1761998400;   // 2025-11-01
constexpr int64_t REREAD_2026 = 1793534400;       // 2026-11-01
constexpr int64_t FIRST_READ_2026 = 1772366400;   // 2026-03-01
constexpr int64_t SECOND_READ_2026 = 1782907200;  // 2026-07-01
constexpr int64_t OLD_READ_2025 = 1738411200;     // 2025-02-01
constexpr int64_t NOW_2026 = 1795521600;          // 2026-11-24

// The yearly rule is a calendar rule, so the zone is pinned: localYear() uses
// localtime_r, and a host in a negative offset would put the 1 January
// boundary cases in the previous year.
void begin() {
  setenv("TZ", "UTC", 1);
  tzset();
  fake::reset();
  FINISHED_BOOKS.reset();
}

bool read(const char* doc, int64_t at) {
  return FINISHED_BOOKS.markRead(doc, "/book.epub", "Title", "Author", "pt", at);
}

bool arm(const char* doc) {
  return FINISHED_BOOKS.setRereading(doc, "/book.epub", "Title", "Author", "pt", true);
}

void aRereadInALaterYearCountsForThatYear() {
  begin();
  CHECK(read("a", FIRST_READ_2025));
  CHECK(arm("a"));
  CHECK(read("a", REREAD_2026));

  CHECK_EQ(FINISHED_BOOKS.countTowardYearlyGoal(NOW_2026), 1u);
  CHECK_EQ(FINISHED_BOOKS.find("a")->timesRead, 2);
  // The first finish is kept; only the most recent one moves.
  CHECK_EQ(FINISHED_BOOKS.find("a")->finishedEpoch, FIRST_READ_2025);
  CHECK_EQ(FINISHED_BOOKS.find("a")->lastFinishedEpoch, REREAD_2026);
}

void twoReadingsInOneYearCountOnce() {
  begin();
  CHECK(read("b", FIRST_READ_2026));
  CHECK(arm("b"));
  CHECK(read("b", SECOND_READ_2026));

  CHECK_EQ(FINISHED_BOOKS.find("b")->timesRead, 2);
  CHECK_EQ(FINISHED_BOOKS.countTowardYearlyGoal(NOW_2026), 1u);
}

void aFinishInAnEarlierYearDoesNotCount() {
  begin();
  CHECK(read("c", OLD_READ_2025));
  CHECK_EQ(FINISHED_BOOKS.countTowardYearlyGoal(NOW_2026), 0u);
  CHECK_EQ(FINISHED_BOOKS.finishedCount(), 1u);
}

void allThreeTogether() {
  begin();
  CHECK(read("a", FIRST_READ_2025));
  CHECK(arm("a"));
  CHECK(read("a", REREAD_2026));
  CHECK(read("b", FIRST_READ_2026));
  CHECK(arm("b"));
  CHECK(read("b", SECOND_READ_2026));
  CHECK(read("c", OLD_READ_2025));

  CHECK_EQ(FINISHED_BOOKS.finishedCount(), 3u);
  CHECK_EQ(FINISHED_BOOKS.countTowardYearlyGoal(NOW_2026), 2u);
}

void aRatingAloneIsNeitherFinishedNorCounted() {
  begin();
  CHECK(FINISHED_BOOKS.setRating("d", "/book.epub", "Title", "Author", "pt", 1));

  CHECK(FINISHED_BOOKS.find("d") != nullptr);
  CHECK_EQ(FINISHED_BOOKS.find("d")->rating, 1);
  CHECK_EQ(FINISHED_BOOKS.find("d")->timesRead, 0);
  CHECK_EQ(FINISHED_BOOKS.finishedCount(), 0u);
  CHECK_EQ(FINISHED_BOOKS.countTowardYearlyGoal(NOW_2026), 0u);
}

void reachingTheEndAgainWithoutArmingChangesNothing() {
  begin();
  CHECK(read("a", FIRST_READ_2025));
  // No setRereading(): this is someone revisiting a passage of an old
  // favourite, which is the case the feature exists to not count.
  CHECK(!read("a", REREAD_2026));

  CHECK_EQ(FINISHED_BOOKS.find("a")->timesRead, 1);
  CHECK_EQ(FINISHED_BOOKS.find("a")->lastFinishedEpoch, FIRST_READ_2025);
  CHECK_EQ(FINISHED_BOOKS.countTowardYearlyGoal(NOW_2026), 0u);
}

void armingIsSpentOnOneFinish() {
  begin();
  CHECK(read("a", FIRST_READ_2025));
  CHECK(arm("a"));
  CHECK(read("a", FIRST_READ_2026));
  CHECK(!FINISHED_BOOKS.find("a")->rereading);
  // A third pass to the end, with nothing armed, must not count.
  CHECK(!read("a", SECOND_READ_2026));
  CHECK_EQ(FINISHED_BOOKS.find("a")->timesRead, 2);
}

void anEntryWrittenBeforeTheReadsFieldIsOneReading() {
  begin();
  fake::add(FinishedBooksStore::getFilePath(),
            R"({"books":[{"doc":"a","path":"/book.epub","title":"T","at":1761998400}]})");
  CHECK(FINISHED_BOOKS.loadFromFile());

  const FinishedBook* book = FINISHED_BOOKS.find("a");
  CHECK(book != nullptr);
  CHECK_EQ(book->timesRead, 1);
  // Absent lastAt reads as "same as the first finish", so the book counts for
  // its own year and no other.
  CHECK_EQ(book->lastFinishedEpoch, FIRST_READ_2025);
  CHECK_EQ(FINISHED_BOOKS.finishedCount(), 1u);
}

void readsIsWrittenEvenAtZeroSoARatingStaysARating() {
  begin();
  CHECK(FINISHED_BOOKS.setRating("d", "/book.epub", "T", "A", "pt", -1));
  FINISHED_BOOKS.reset();
  CHECK(FINISHED_BOOKS.loadFromFile());

  // Were "reads" omitted at zero, the reload would read this as a pre-field
  // entry and migrate it to one reading -- turning a book never read into a
  // finished one, and losing the real first finish later.
  CHECK(FINISHED_BOOKS.find("d") != nullptr);
  CHECK_EQ(FINISHED_BOOKS.find("d")->timesRead, 0);
  CHECK_EQ(FINISHED_BOOKS.find("d")->rating, -1);
  CHECK_EQ(FINISHED_BOOKS.finishedCount(), 0u);
}

void resetIsNotUndoneByLoadingAProfileThatHasNoFileYet() {
  begin();
  // The profile switch: this reader finished books, the next one has saved
  // nothing. loadFromFile() returns false without reaching fromJson(), so
  // without reset() the new reader would be shown these entries -- and the
  // first award they earned would persist them into their own file.
  CHECK(read("a", FIRST_READ_2026));
  CHECK_EQ(FINISHED_BOOKS.finishedCount(), 1u);

  fake::reset();  // the new profile's directory is empty
  FINISHED_BOOKS.reset();
  CHECK(!FINISHED_BOOKS.loadFromFile());

  CHECK_EQ(FINISHED_BOOKS.finishedCount(), 0u);
  CHECK_EQ(FINISHED_BOOKS.getBooks().size(), 0u);
  CHECK_EQ(FINISHED_BOOKS.countTowardYearlyGoal(NOW_2026), 0u);
  CHECK(FINISHED_BOOKS.find("a") == nullptr);
}

}  // namespace

int main() {
  aRereadInALaterYearCountsForThatYear();
  twoReadingsInOneYearCountOnce();
  aFinishInAnEarlierYearDoesNotCount();
  allThreeTogether();
  aRatingAloneIsNeitherFinishedNorCounted();
  reachingTheEndAgainWithoutArmingChangesNothing();
  armingIsSpentOnOneFinish();
  anEntryWrittenBeforeTheReadsFieldIsOneReading();
  readsIsWrittenEvenAtZeroSoARatingStaysARating();
  resetIsNotUndoneByLoadingAProfileThatHasNoFileYet();

  if (failures == 0) {
    std::printf("ok finished_books  all checks passed\n");
    return 0;
  }
  std::printf("%d check(s) failed\n", failures);
  return 1;
}
