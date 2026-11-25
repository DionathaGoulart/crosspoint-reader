#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstddef>
#include <string>
#include <vector>

// The reader's "to read" list: books marked to read next, in the order they
// were added.
//
// Keyed by path rather than by document id: the library matches it against
// the index by path hash, and computing a binary document id would read the
// whole book file. A book that is moved on the card drops off the list.
struct QueuedBook {
  std::string path;
};

class ReadingQueueStore : public PersistableStore<ReadingQueueStore> {
 public:
  // A list longer than this is not a queue any more.
  static constexpr size_t MAX_BOOKS = 200;
  static constexpr bool kPerProfile = true;

  static const char* getFilePath() { return "/.crosspoint/queue.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Loads from the card the first time it is asked for. A save before the
  // first load would otherwise replace the file with an empty list.
  void ensureLoaded();
  // Forgets what is in memory; see RecentBooksStore::reset().
  void reset() {
    std::vector<QueuedBook>().swap(books);
    loaded = false;
  }

  bool contains(const std::string& path) const;
  // Both save on change and return false when nothing changed.
  bool add(const std::string& path);
  bool remove(const std::string& path);

  // Oldest first: the next book to read is at the front.
  const std::vector<QueuedBook>& getBooks() const { return books; }

 private:
  ReadingQueueStore() = default;
  ~ReadingQueueStore() = default;
  friend class PersistableStore<ReadingQueueStore>;

  std::vector<QueuedBook> books;
  bool loaded = false;
};

#define READING_QUEUE ReadingQueueStore::getInstance()
