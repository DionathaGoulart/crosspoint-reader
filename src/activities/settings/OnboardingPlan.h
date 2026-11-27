#pragma once

#include <cstddef>
#include <cstdint>

// Which pages the setup guide shows. Kept free of settings and storage so a
// host test can pin it.
namespace onboarding {

// Full walks every page: first boot, or "Setup guide" in Settings.
// ProfileOnly is for a new profile. It shows only what belongs to that
// reader, because the other settings are shared by the whole device: changing
// them here would change them for everyone.
enum class Mode : uint8_t { Full, ProfileOnly };

// Theme is device-wide, so ProfileOnly never shows it.
// Summary lists the device-wide choices once more, so one last look can still change them.
enum class Step : uint8_t { Welcome, Theme, Reading, Controls, Sleep, Goals, Summary };

constexpr size_t MAX_STEPS = 7;

// Writes the pages in order and returns how many.
inline size_t stepsFor(const Mode mode, Step (&out)[MAX_STEPS]) {
  if (mode == Mode::ProfileOnly) {
    out[0] = Step::Reading;
    out[1] = Step::Goals;
    return 2;
  }
  out[0] = Step::Welcome;
  out[1] = Step::Theme;
  out[2] = Step::Reading;
  out[3] = Step::Controls;
  out[4] = Step::Sleep;
  out[5] = Step::Goals;
  out[6] = Step::Summary;
  return MAX_STEPS;
}

}  // namespace onboarding
