#include "AchievementStore.h"

#include <Logging.h>

void AchievementStore::toJson(JsonDocument& doc) const {
  JsonObject obj = doc["unlocked"].to<JsonObject>();
  for (size_t i = 0; i < achievements::kCount; ++i) {
    if (unlockedAt[i] == 0 && !isUnlocked(static_cast<achievements::Id>(i))) continue;
    obj[achievements::key(static_cast<achievements::Id>(i))] = unlockedAt[i];
  }
}

bool AchievementStore::fromJson(JsonVariantConst doc) {
  for (auto& at : unlockedAt) at = 0;
  JsonObjectConst obj = doc["unlocked"].as<JsonObjectConst>();
  for (size_t i = 0; i < achievements::kCount; ++i) {
    const char* k = achievements::key(static_cast<achievements::Id>(i));
    // A key the build no longer knows is ignored rather than fatal, so an
    // older or newer achievements.json never blocks the rest from loading.
    if (obj[k].is<int64_t>()) unlockedAt[i] = obj[k].as<int64_t>();
  }
  return true;
}

bool AchievementStore::isUnlocked(const achievements::Id id) const {
  const size_t index = static_cast<size_t>(id);
  // A zero date still means unlocked: it is what a clock-less unlock records.
  return index < achievements::kCount && unlockedAt[index] != 0;
}

int64_t AchievementStore::unlockedEpoch(const achievements::Id id) const {
  const size_t index = static_cast<size_t>(id);
  return index < achievements::kCount ? unlockedAt[index] : 0;
}

size_t AchievementStore::evaluate(const achievements::Inputs& inputs, const int64_t now, achievements::Id* out,
                                  const size_t capacity) {
  bool before[achievements::kCount];
  for (size_t i = 0; i < achievements::kCount; ++i) before[i] = unlockedAt[i] != 0;

  achievements::Id earned[achievements::kCount];
  const size_t count = achievements::newlyEarned(inputs, before, earned, achievements::kCount);
  if (count == 0) return 0;

  for (size_t i = 0; i < count; ++i) {
    // -1 rather than 0 for a clock-less unlock, so "unlocked" and "unlocked on
    // a known day" stay distinguishable and isUnlocked() still answers true.
    unlockedAt[static_cast<size_t>(earned[i])] = now > 0 ? now : -1;
    if (out != nullptr && i < capacity) out[i] = earned[i];
  }
  saveToFile();
  LOG_INF("ACH", "%u achievement(s) unlocked", static_cast<unsigned>(count));
  return count;
}

size_t AchievementStore::unlockedCount() const {
  size_t count = 0;
  for (const auto at : unlockedAt) {
    if (at != 0) ++count;
  }
  return count;
}
