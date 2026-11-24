#pragma once

#include <cstddef>
#include <cstdint>

#include "StatsView.h"

// Reading goals the reader sets for themselves, as pure functions of the
// summary.
//
// Nothing here measures anything new: every goal reads a field the firmware
// already folds out of the session log (see StatsAggregate.h). That is the
// whole design constraint -- a goal whose progress cannot be derived from the
// existing summary does not belong in this list, because it would mean a new
// measurement, a new cache field, and a new way for the figures to disagree.
//
// Pure, like StatsView and Achievements: no settings, no storage, no renderer.
// The caller fills Targets from CrossPointSettings, so the same arithmetic is
// testable on the host and drawn by both the settings screen and the sleep
// screen without either re-deriving it.
namespace goals {

enum class Id : uint8_t {
  DailyMinutes,
  WeeklyMinutes,
  StreakDays,
  BooksPerYear,
  Count,
};

inline constexpr size_t kCount = static_cast<size_t>(Id::Count);

// A target of zero disables that goal. Zero is the only "off" value, so a
// screen never has to carry a separate enabled flag per goal, and the bottom of
// the settings screen's preset cycle doubles as "do not track this".
struct Targets {
  uint16_t dailyMinutes = 0;
  uint16_t weeklyMinutes = 0;
  uint16_t streakDays = 0;
  uint16_t booksPerYear = 0;
};

struct Progress {
  uint32_t current = 0;
  uint32_t target = 0;
  // False when the target is zero. The remaining fields are then all zero,
  // which a screen shows by drawing nothing rather than an empty bar.
  bool enabled = false;
  bool met = false;
  // 0..100, clamped. Capped at 100 so a bar cannot overrun its box on the day
  // someone reads four times their target.
  uint8_t percent = 0;
};

// The configured target for one goal, or 0 when it is off.
uint16_t targetFor(Id id, const Targets& targets);

// What the reader has actually done, in the same unit as the target.
uint32_t currentFor(Id id, const stats::Summary& summary);

Progress progressFor(Id id, const Targets& targets, const stats::Summary& summary);

// True when at least one goal is configured. A screen with nothing to show
// uses this to fall back instead of drawing an empty row.
bool anyEnabled(const Targets& targets);

}  // namespace goals
