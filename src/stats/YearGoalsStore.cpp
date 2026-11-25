#include "YearGoalsStore.h"

#include <TrustedTime.h>

#include <algorithm>
#include <ctime>

void YearGoalsStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["years"].to<JsonArray>();
  for (const Entry& entry : entries) {
    JsonObject obj = arr.add<JsonObject>();
    obj["year"] = entry.year;
    obj["books"] = entry.books;
  }
}

bool YearGoalsStore::fromJson(JsonVariantConst doc) {
  entries.clear();
  loaded = true;
  for (JsonObjectConst obj : doc["years"].as<JsonArrayConst>()) {
    if (entries.size() >= MAX_YEARS) break;
    const int year = obj["year"] | 0;
    if (year < 2000 || year > 9999) continue;
    entries.push_back(Entry{static_cast<int16_t>(year), obj["books"] | static_cast<uint16_t>(0)});
  }
  return true;
}

void YearGoalsStore::ensureLoaded() {
  if (loaded || loadFromFile()) return;  // fromJson() sets `loaded`
  // No file yet is an empty store. A file that could not be read or parsed
  // leaves the store unloaded, so no save replaces it with a partial list.
  loaded = !profileFileExists(getFilePath(), kPerProfile);
}

uint16_t YearGoalsStore::booksTarget(const int year) const {
  for (const Entry& entry : entries) {
    if (entry.year == year) return entry.books;
  }
  return 0;
}

void YearGoalsStore::record(const int year, const uint16_t booksTarget) {
  if (year < 2000 || year > 9999) return;
  ensureLoaded();
  if (!loaded) return;
  for (Entry& entry : entries) {
    if (entry.year != year) continue;
    if (entry.books == booksTarget) return;
    entry.books = booksTarget;
    saveToFile();
    return;
  }
  // Oldest year goes first when the list is full.
  if (entries.size() >= MAX_YEARS) {
    entries.erase(std::min_element(entries.begin(), entries.end(),
                                   [](const Entry& a, const Entry& b) { return a.year < b.year; }));
  }
  entries.push_back(Entry{static_cast<int16_t>(year), booksTarget});
  saveToFile();
}

void YearGoalsStore::recordCurrent(const uint16_t booksTarget) {
  const int64_t now = trustedtime::trustedNow();
  if (now <= 0) return;
  const time_t at = static_cast<time_t>(now);
  struct tm local{};
  localtime_r(&at, &local);
  record(local.tm_year + 1900, booksTarget);
}
