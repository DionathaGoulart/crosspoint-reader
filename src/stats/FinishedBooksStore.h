#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>
#include <vector>

// Books the reader actually finished, with the date they were finished on.
//
// Why a store of its own rather than a flag beside the book: deleting a book
// deletes its whole cache directory, clippings sidecars included
// (removeBookFile() in src/util/BookCacheUtils.cpp). A book you read and then
// removed from the card has to keep counting, so the record cannot live with
// the book. Title and author are copied in for the same reason -- the shelf of
// finished books must still have something to show once the file is gone.
//
// The document id is the one readingSessionLog writes, so a row here joins
// straight onto the per-book figures in StatsAggregate.
struct FinishedBook {
  std::string document;  // 32-hex KOReader document id
  std::string path;      // where it was when finished; may no longer exist
  std::string title;
  std::string author;
  std::string language;
  // The FIRST time the reader reached the end. Kept for the life of the entry:
  // when someone first finished a book is the fact worth having.
  int64_t finishedEpoch = 0;  // 0 when the clock was not set

  // How many times this book has been read to the end. Zero means the entry
  // exists for a rating alone, so this -- not the date -- is what "finished"
  // means. An entry written before this field existed is migrated to 1 on load.
  uint16_t timesRead = 0;
  // The most recent finish, where finishedEpoch is the first.
  int64_t lastFinishedEpoch = 0;
  // -1 disliked, 0 unrated, +1 liked. A rating alone creates an entry, because
  // an opinion about a book you abandoned is still an opinion.
  int8_t rating = 0;
  // Set only by the reader asking to reread, and cleared when the end is next
  // reached. Without it, opening a finished book to check one page and paging
  // to the end would count as another reading.
  bool rereading = false;
};

class FinishedBooksStore : public PersistableStore<FinishedBooksStore> {
 public:
  // Two a week for five years. Past this the oldest entries are dropped, which
  // keeps the JSON bounded; the session log still holds the full history.
  static constexpr size_t MAX_BOOKS = 500;

 private:
  std::vector<FinishedBook> books;

  FinishedBooksStore() = default;
  ~FinishedBooksStore() = default;

  friend class PersistableStore<FinishedBooksStore>;

  // The entry for a book, created from its details when it has none. Null
  // only when the document id is empty, which means the book could not be
  // identified at all.
  FinishedBook* entryFor(const std::string& document, const std::string& path, const std::string& title,
                         const std::string& author, const std::string& language);

 public:
  // Books this reader finished, and the count the yearly goal measures. Follows the active profile; see
  // lib/Serialization/ProfilePath.h.
  static constexpr bool kPerProfile = true;

  static const char* getFilePath() { return "/.crosspoint/finished.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Forgets everything held in memory, without touching the card. See
  // RecentBooksStore::reset() for why profiles::switchTo() needs this.
  void reset() { books.clear(); }

  bool contains(const std::string& document) const;
  const FinishedBook* find(const std::string& document) const;

  // Records reaching the end. Creates the entry on a first finish; on a book
  // already recorded it counts another reading only when rereading was armed,
  // which is what keeps "read one page of an old favourite" from counting.
  // Returns true when the reading count went up.
  bool markRead(const std::string& document, const std::string& path, const std::string& title,
                const std::string& author, const std::string& language, int64_t epoch);

  // Arms or disarms a reread. Creates the entry if the book has no record yet,
  // so a book finished before this feature existed can still be reread.
  bool setRereading(const std::string& document, const std::string& path, const std::string& title,
                    const std::string& author, const std::string& language, bool rereading);

  // -1, 0 or +1; anything else is refused. Creates the entry when needed.
  bool setRating(const std::string& document, const std::string& path, const std::string& title,
                 const std::string& author, const std::string& language, int8_t rating);

  // Newest first. Includes entries that exist only for a rating, so a caller
  // showing "finished books" filters on timesRead.
  const std::vector<FinishedBook>& getBooks() const { return books; }
  // Books actually read to the end. Not books.size(): an entry can exist for a
  // rating on a book that was never finished, and counting those would inflate
  // the yearly goal and the awards.
  size_t finishedCount() const;

  // Books that count toward the yearly goal for the calendar year of `now`, in
  // local time. One per book: rereading it in a later year counts again for
  // that year, reading it twice inside one year does not.
  //
  // Named for the goal rather than for a year because only the first and the
  // most recent finish carry a date. Asked about the current year -- the only
  // thing the goal wants -- the most recent finish is by definition the
  // relevant one, so the answer is exact. Asked about a year in the middle of
  // three or more readings it would undercount, which is why the parameter is
  // `now` and not an arbitrary epoch.
  size_t countTowardYearlyGoal(int64_t now) const;
};

#define FINISHED_BOOKS FinishedBooksStore::getInstance()

namespace finishedbooks {

// The document id readingSessionLog writes for this book, so the two agree:
// the binary id when the sync match method is BINARY, otherwise the filename
// one. Mirrors ReaderActivity::flushReaderSession().
std::string documentIdFor(const std::string& bookPath);

// Records the book the reader just reached the end of. Resolves the document
// id and the current time itself, so the call site stays one line.
// Best effort: a book that cannot be identified, or a clock that was never
// set, is simply not recorded.
bool recordFinished(const std::string& bookPath, const std::string& title, const std::string& author,
                    const std::string& language);

}  // namespace finishedbooks
