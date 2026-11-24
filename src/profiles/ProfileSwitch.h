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
// Call from the main task, holding a RenderLock. Nothing here enforces that,
// and the cost of getting it wrong is not a half-drawn frame: UITheme::reload()
// destroys the theme object the render task calls virtual methods on, and the
// four store loads read the card the render task is reading fonts and covers
// from. ProfilesSettingsActivity::switchToProfile() is the only caller and
// takes the lock.
bool switchTo(uint8_t id);

}  // namespace profiles
