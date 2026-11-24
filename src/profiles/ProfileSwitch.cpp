#include "ProfileSwitch.h"

#include <Logging.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
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

  // Cleared before loading, not after: PersistableStore::loadFromFile() returns
  // before it reaches fromJson() when the file does not exist, and only
  // fromJson() clears what is in memory. A profile that has never saved -- which
  // is every profile the moment it is created -- would otherwise be shown the
  // departing reader's shelf, finished books and awards. Worse for awards:
  // evaluate() saves, so the first award the new reader earns would persist the
  // other reader's unlocks into their file. CrossPointSettings does the
  // equivalent inside its own loadFromFile(), because its absent-key rule is
  // "keep what is in memory" and a blanket clear would drop device settings.
  RECENT_BOOKS.reset();
  FINISHED_BOOKS.reset();
  ACHIEVEMENTS.reset();

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

  // state.json stays device-wide -- it is read before anyone knows which
  // profile is active, and splitting it would need the same overlay machinery
  // settings.json has for one path and two flags. But what is in it right now
  // belongs to the reader who is leaving: without this, the next boot resumes
  // into their book and the sleep screen shows their cover and title, which is
  // the screen that sits visible while the device is idle.
  APP_STATE.openEpubPath.clear();
  APP_STATE.lastSleepFromReader = false;
  APP_STATE.saveToFile();

  UITheme::getInstance().reload();
  LOG_INF("PROFILE", "Switched to profile %u", static_cast<unsigned>(id));
  return true;
}

}  // namespace profiles
