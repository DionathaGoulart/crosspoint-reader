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
  int64_t finishedEpoch = 0;  // 0 when the clock was not set
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

 public:
  static const char* getFilePath() { return "/.crosspoint/finished.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Records a finished book and persists. Re-finishing a book already recorded
  // updates its path and metadata and leaves the original date alone: the
  // first time you reached the end is the interesting one, and re-opening a
  // finished book to check a quote must not rewrite history.
  //
  // Returns true when this call added a book that was not there before, so the
  // caller can decide whether an achievement or a message is due.
  bool markFinished(const std::string& document, const std::string& path, const std::string& title,
                    const std::string& author, const std::string& language, int64_t finishedEpoch);

  bool contains(const std::string& document) const;

  // Newest first.
  const std::vector<FinishedBook>& getBooks() const { return books; }
  size_t count() const { return books.size(); }

  // How many were finished in the calendar year of `epoch`, in local time.
  size_t countInYear(int64_t epoch) const;
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
