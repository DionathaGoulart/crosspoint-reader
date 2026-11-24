#include "Goals.h"

namespace goals {

uint16_t targetFor(const Id id, const Targets& targets) {
  switch (id) {
    case Id::DailyMinutes:
      return targets.dailyMinutes;
    case Id::WeeklyMinutes:
      return targets.weeklyMinutes;
    case Id::StreakDays:
      return targets.streakDays;
    case Id::BooksPerYear:
      return targets.booksPerYear;
    case Id::Count:
      break;
  }
  return 0;
}

uint32_t currentFor(const Id id, const stats::Summary& summary) {
  switch (id) {
    case Id::DailyMinutes:
      return summary.todayMinutes;
    case Id::WeeklyMinutes:
      return summary.weekMinutes;
    case Id::StreakDays:
      return summary.currentStreak;
    case Id::BooksPerYear:
      return summary.booksThisYear;
    case Id::Count:
      break;
  }
  return 0;
}

Progress progressFor(const Id id, const Targets& targets, const stats::Summary& summary) {
  Progress out;
  const uint16_t target = targetFor(id, targets);
  if (target == 0) return out;

  out.enabled = true;
  out.target = target;
  out.current = currentFor(id, summary);
  out.met = out.current >= out.target;
  if (out.met) {
    out.percent = 100;
    return out;
  }
  // 64-bit multiply: current is a u32 of minutes, which overflows a 32-bit
  // product past ~42.9 million. No real figure reaches that, but the cast costs
  // nothing and removes the question.
  const uint64_t scaled = static_cast<uint64_t>(out.current) * 100u / out.target;
  out.percent = static_cast<uint8_t>(scaled);
  return out;
}

bool anyEnabled(const Targets& targets) {
  for (size_t i = 0; i < kCount; ++i) {
    if (targetFor(static_cast<Id>(i), targets) != 0) return true;
  }
  return false;
}

}  // namespace goals
