#include "Achievements.h"

#include <algorithm>

namespace achievements {
namespace {

struct Threshold {
  // Which figure the condition reads, and the value it has to reach.
  enum class Field : uint8_t { BooksFinished, TotalMinutes, LongestStreak, LongestSessionMinutes } field;
  uint32_t target;
};

constexpr Threshold kThresholds[kCount] = {
    /* FirstBook      */ {Threshold::Field::BooksFinished, 1},
    /* Books10        */ {Threshold::Field::BooksFinished, 10},
    /* Books25        */ {Threshold::Field::BooksFinished, 25},
    /* Books50        */ {Threshold::Field::BooksFinished, 50},
    /* Books100       */ {Threshold::Field::BooksFinished, 100},
    /* Hours10        */ {Threshold::Field::TotalMinutes, 10 * 60},
    /* Hours50        */ {Threshold::Field::TotalMinutes, 50 * 60},
    /* Hours100       */ {Threshold::Field::TotalMinutes, 100 * 60},
    /* Streak7        */ {Threshold::Field::LongestStreak, 7},
    /* Streak30       */ {Threshold::Field::LongestStreak, 30},
    /* Streak100      */ {Threshold::Field::LongestStreak, 100},
    /* LongSession    */ {Threshold::Field::LongestSessionMinutes, 120},
};

// These strings are persisted ids. A key removed from this list is never
// reused for another award, or that award inherits whoever had earned the old
// one.
constexpr const char* kKeys[kCount] = {
    "first_book", "books_10",  "books_25", "books_50",  "books_100",  "hours_10",
    "hours_50",   "hours_100", "streak_7", "streak_30", "streak_100", "session_2h",
};

uint32_t valueOf(const Threshold::Field field, const Inputs& in) {
  switch (field) {
    case Threshold::Field::BooksFinished:
      return in.summary.booksFinished;
    case Threshold::Field::TotalMinutes:
      return in.summary.totalMinutes;
    case Threshold::Field::LongestStreak:
      // The record, not the current run: an achievement that un-earns itself
      // when a streak breaks would be a punishment, not a reward.
      return std::max(in.summary.longestStreak, in.summary.currentStreak);
    case Threshold::Field::LongestSessionMinutes:
      return in.summary.longestSessionMinutes;
  }
  return 0;
}

}  // namespace

bool earned(const Id id, const Inputs& inputs) {
  const size_t index = static_cast<size_t>(id);
  if (index >= kCount) return false;
  const Threshold& t = kThresholds[index];
  return valueOf(t.field, inputs) >= t.target;
}

size_t newlyEarned(const Inputs& inputs, const bool* unlockedBefore, Id* out, const size_t capacity) {
  if (out == nullptr) return 0;
  size_t written = 0;
  for (size_t i = 0; i < kCount && written < capacity; ++i) {
    const Id id = static_cast<Id>(i);
    if (unlockedBefore != nullptr && unlockedBefore[i]) continue;
    if (earned(id, inputs)) out[written++] = id;
  }
  return written;
}

uint8_t progressPercent(const Id id, const Inputs& inputs) {
  const size_t index = static_cast<size_t>(id);
  if (index >= kCount) return 0;
  const Threshold& t = kThresholds[index];
  if (t.target == 0) return 100;
  const uint64_t value = valueOf(t.field, inputs);
  return static_cast<uint8_t>(std::min<uint64_t>(100, value * 100 / t.target));
}

Kind kindOf(const Id id) {
  const size_t index = static_cast<size_t>(id);
  if (index >= kCount) return Kind::Books;
  switch (kThresholds[index].field) {
    case Threshold::Field::BooksFinished:
      return Kind::Books;
    case Threshold::Field::TotalMinutes:
      return Kind::Hours;
    case Threshold::Field::LongestStreak:
      return Kind::Streak;
    case Threshold::Field::LongestSessionMinutes:
      return Kind::Session;
  }
  return Kind::Books;
}

uint32_t target(const Id id) {
  const size_t index = static_cast<size_t>(id);
  if (index >= kCount) return 0;
  const Threshold& t = kThresholds[index];
  // Hours and sessions are stored in minutes; the label wants hours.
  if (t.field == Threshold::Field::TotalMinutes || t.field == Threshold::Field::LongestSessionMinutes) {
    return t.target / 60;
  }
  return t.target;
}

const char* key(const Id id) {
  const size_t index = static_cast<size_t>(id);
  return index < kCount ? kKeys[index] : "";
}

}  // namespace achievements
