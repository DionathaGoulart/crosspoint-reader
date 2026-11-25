#include "ReadingQueueStore.h"

#include <Logging.h>

#include <algorithm>

void ReadingQueueStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : books) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = book.path;
  }
}

bool ReadingQueueStore::fromJson(JsonVariantConst doc) {
  books.clear();
  loaded = true;
  JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  books.reserve(std::min(arr.size(), MAX_BOOKS));
  for (JsonObjectConst obj : arr) {
    if (books.size() >= MAX_BOOKS) break;
    QueuedBook book;
    book.path = obj["path"] | "";
    if (book.path.empty()) continue;
    books.push_back(std::move(book));
  }
  LOG_DBG("RQS", "Reading queue loaded (%u books)", static_cast<unsigned>(books.size()));
  return true;
}

void ReadingQueueStore::ensureLoaded() {
  if (loaded || loadFromFile()) return;  // fromJson() sets `loaded`
  // No file yet is an empty store. A file that could not be read or parsed
  // leaves the store unloaded, so no save replaces it with a partial list.
  loaded = !profileFileExists(getFilePath(), kPerProfile);
}

bool ReadingQueueStore::contains(const std::string& path) const {
  return std::any_of(books.begin(), books.end(), [&path](const QueuedBook& book) { return book.path == path; });
}

bool ReadingQueueStore::add(const std::string& path) {
  ensureLoaded();
  if (!loaded || path.empty() || contains(path) || books.size() >= MAX_BOOKS) return false;
  books.push_back(QueuedBook{path});
  saveToFile();
  return true;
}

bool ReadingQueueStore::remove(const std::string& path) {
  ensureLoaded();
  if (!loaded) return false;
  const auto it =
      std::find_if(books.begin(), books.end(), [&path](const QueuedBook& book) { return book.path == path; });
  if (it == books.end()) return false;
  books.erase(it);
  saveToFile();
  return true;
}
