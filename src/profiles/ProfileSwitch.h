#pragma once

#include <cstdint>

// Switching the reader without a reboot.
//
// profilePath only decides where a file lives; everything already loaded still
// holds the previous reader's values. So a switch has two halves: move the
// pointer, then reload every store that follows the reader. Miss one and the
// device shows two people's data at once, which is the exact thing profiles
// exist to prevent.
namespace profiles {

// Points storage at `id` and reloads what belongs to a reader: settings and
// their reading preferences, recent books, finished books, awards, and the
// statistics rollup. Also reapplies the theme, because a profile may carry its
// own and the UI has to be rebuilt before the next frame.
//
// False when the id is unknown or the choice could not be persisted, in which
// case nothing was reloaded and the previous profile is still active.
//
// Call from the main task only, between activities: it reloads the settings the
// render path reads, so a switch during a frame would draw half of each
// reader's preferences.
bool switchTo(uint8_t id);

}  // namespace profiles
