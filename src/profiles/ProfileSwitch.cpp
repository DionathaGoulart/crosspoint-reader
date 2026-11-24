#include "ProfileSwitch.h"

#include <Logging.h>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "profiles/ProfileStore.h"
#include "stats/AchievementStore.h"
#include "stats/FinishedBooksStore.h"

namespace profiles {

bool switchTo(const uint8_t id) {
  if (PROFILES.activeId() == id) return true;
  // setActive() persists the choice and moves profilePath, so everything below
  // resolves to the new reader's files. It is first because a failure here must
  // leave the device exactly as it was.
  if (!PROFILES.setActive(id)) {
    LOG_ERR("PROFILE", "Could not switch to profile %u", static_cast<unsigned>(id));
    return false;
  }

  // Settings first: the theme is one of the reader's own preferences, and
  // UITheme::reload() below reads it.
  SETTINGS.loadFromFile();
  RECENT_BOOKS.loadFromFile();
  FINISHED_BOOKS.loadFromFile();
  ACHIEVEMENTS.loadFromFile();

  // Statistics need no reload: statsService reads the rollup and the session
  // log from disk each time it is asked, and both now resolve to this reader's
  // files. Deliberately not calling statsRollup::invalidate(), which would
  // delete the rollup that has just become the new reader's.

  UITheme::getInstance().reload();
  LOG_INF("PROFILE", "Switched to profile %u", static_cast<unsigned>(id));
  return true;
}

}  // namespace profiles
