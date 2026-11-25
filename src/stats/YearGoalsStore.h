#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <vector>

// The yearly book goal as it was in each year, so the retrospective can say
// whether a past year met the target the reader had then, not the one they
// have now. Recorded when the goal changes and when Statistics opens.
class YearGoalsStore : public PersistableStore<YearGoalsStore> {
 public:
  static constexpr size_t MAX_YEARS = 50;
  static constexpr bool kPerProfile = true;

  static const char* getFilePath() { return "/.crosspoint/year_goals.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  void ensureLoaded();
  void reset() {
    entries.clear();
    loaded = false;
  }

  // 0 when nothing was recorded for that year.
  uint16_t booksTarget(int year) const;
  // Saves only when the value changes.
  void record(int year, uint16_t booksTarget);
  // record() for the current local year; does nothing while the clock is unset.
  void recordCurrent(uint16_t booksTarget);

 private:
  YearGoalsStore() = default;
  ~YearGoalsStore() = default;
  friend class PersistableStore<YearGoalsStore>;

  struct Entry {
    int16_t year;
    uint16_t books;
  };
  std::vector<Entry> entries;
  bool loaded = false;
};

#define YEAR_GOALS YearGoalsStore::getInstance()
