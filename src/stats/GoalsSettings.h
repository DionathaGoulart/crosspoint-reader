#pragma once

#include "CrossPointSettings.h"
#include "Goals.h"

// The reader's configured goals, read out of settings.
//
// Deliberately not part of Goals.h: that header is pure arithmetic over a
// summary and builds on the host with nothing but a compiler (test/goals),
// which is what makes the edge cases testable. Pulling CrossPointSettings in
// there would end that. Every screen that draws a goal goes through here.
inline goals::Targets goalTargetsFromSettings() {
  goals::Targets targets;
  targets.dailyMinutes = SETTINGS.goalDailyMinutes;
  targets.weeklyMinutes = SETTINGS.goalWeeklyMinutes;
  targets.streakDays = SETTINGS.goalStreakDays;
  targets.booksPerYear = SETTINGS.goalBooksPerYear;
  return targets;
}
