#pragma once

#include <I18n.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "stats/StatsView.h"

// Durations with their unit in the interface language.
//
// The split between this and StatsView is deliberate: StatsView decides the
// thresholds and is compiled without I18n so the statistics arithmetic can be
// driven from a host test, and the words live here. Same shape as the
// `tr(key)`-as-format-string pattern used across the activities.
//
// Translators own these three formats. Each carries a fixed number of %u --
// one, one, then two -- and changing that count is undefined behaviour in
// snprintf, as it is for every other format string in the translation files.
namespace statsText {

// "1 h 05" above an hour, "47 min" below it, "--" for nothing read yet.
// Fetches the formats per call: the reader can change language from Settings
// without a reboot, so a cached set would keep the old unit until one.
inline void duration(char* out, const size_t size, const uint32_t minutes) {
  if (out == nullptr || size == 0) return;
  const stats::DurationParts parts = stats::splitDuration(minutes);
  switch (parts.unit) {
    case stats::DurationParts::Minutes:
      std::snprintf(out, size, tr(STR_STATS_DURATION_MINUTES), static_cast<unsigned>(parts.minutes));
      return;
    case stats::DurationParts::Hours:
      std::snprintf(out, size, tr(STR_STATS_DURATION_HOURS), static_cast<unsigned>(parts.hours));
      return;
    case stats::DurationParts::HoursAndMinutes:
      std::snprintf(out, size, tr(STR_STATS_DURATION_HOURS_MINUTES), static_cast<unsigned>(parts.hours),
                    static_cast<unsigned>(parts.minutes));
      return;
    case stats::DurationParts::Zero:
      break;
  }
  // A dash is not a unit, so it needs no translation and no key.
  std::snprintf(out, size, "--");
}

}  // namespace statsText
