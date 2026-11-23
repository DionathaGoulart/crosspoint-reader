#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>

#include "Achievements.h"

// When each achievement was earned, in /.crosspoint/achievements.json.
//
// Only the date is stored. The condition itself is re-derived from the
// aggregate, so the file stays tiny and a reader who deletes it loses only the
// dates -- everything re-unlocks on the next evaluation.
class AchievementStore : public PersistableStore<AchievementStore> {
 private:
  int64_t unlockedAt[achievements::kCount] = {};

  AchievementStore() = default;
  ~AchievementStore() = default;

  friend class PersistableStore<AchievementStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/achievements.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool isUnlocked(achievements::Id id) const;
  int64_t unlockedEpoch(achievements::Id id) const;

  // Re-derives every condition and records the date of any newly earned one.
  // Persists only when something changed. `out` receives the newly earned ids,
  // so the caller can show them; pass nullptr to just record.
  //
  // `now` of 0 means the clock is unset: the unlock is still recorded, with a
  // zero date, because losing the fact to save the date would be the wrong
  // trade. A screen shows such a one as earned without a day.
  size_t evaluate(const achievements::Inputs& inputs, int64_t now, achievements::Id* out = nullptr,
                  size_t capacity = 0);

  size_t unlockedCount() const;
};

#define ACHIEVEMENTS AchievementStore::getInstance()
