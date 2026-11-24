#pragma once

#include <cstdint>

#include "StatsView.h"

// What the reader has earned, as pure functions of the summary.
//
// Every condition here is derivable from figures the firmware already keeps, so
// nothing new has to be measured and the whole set can be evaluated on the host.
// An achievement whose condition cannot be derived does not belong in this list:
// a number nobody can reproduce is worse than no number.
namespace achievements {

enum class Id : uint8_t {
  FirstBook,
  Books10,
  Books25,
  Books50,
  Books100,
  Hours10,
  Hours50,
  Hours100,
  Streak7,
  Streak30,
  Streak100,
  LongSession,  // a single sitting of two hours
  Count,
};

inline constexpr size_t kCount = static_cast<size_t>(Id::Count);

// Everything a condition may read. Kept separate from Summary so adding an
// input is a visible change rather than a quiet widening of what counts.
struct Inputs {
  stats::Summary summary{};
};

// True when the condition holds now. Conditions are monotone: once true for a
// given history they stay true, which is what lets the store record a date and
// never re-check.
bool earned(Id id, const Inputs& inputs);

// Fills `out` with the ids newly earned, comparing against `unlockedBefore`
// (one bool per id, in Id order). Returns how many were written.
size_t newlyEarned(const Inputs& inputs, const bool* unlockedBefore, Id* out, size_t capacity);

// Progress toward an achievement, 0..100, for a bar next to a locked one.
uint8_t progressPercent(Id id, const Inputs& inputs);

// Stable key used in the saved JSON. Never reorder or rename these: a renamed
// key silently re-locks an achievement the reader already earned.
const char* key(Id id);

// What an achievement is about, and the number it asks for. The UI builds its
// label from these ("%u books", "%u hours", ...) rather than carrying one
// translated string per achievement: a dozen near-identical strings cost
// flash and translator time for no extra meaning.
enum class Kind : uint8_t { Books, Hours, Streak, Session };
Kind kindOf(Id id);
uint32_t target(Id id);

}  // namespace achievements
