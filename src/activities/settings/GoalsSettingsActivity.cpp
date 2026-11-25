#include "GoalsSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "stats/Goals.h"
#include "stats/StatsText.h"
#include "stats/StatsView.h"
#include "stats/YearGoalsStore.h"

namespace fui = freeink::ui;

namespace {
enum MenuItem {
  ITEM_DAILY = 0,
  ITEM_WEEKLY,
  ITEM_STREAK,
  ITEM_BOOKS,
};

const StrId menuNames[GoalsSettingsActivity::ITEM_COUNT] = {
    StrId::STR_GOAL_DAILY,
    StrId::STR_GOAL_WEEKLY,
    StrId::STR_GOAL_STREAK,
    StrId::STR_GOAL_BOOKS_YEAR,
};

// Presets, ascending, each starting at 0 = off so one cycle reaches every
// value including "do not track this". Every table ends below the matching
// CrossPointSettings::GOAL_MAX_*, which is what the figures behind the goal can
// actually represent.
constexpr uint16_t dailyMinutes[] = {0, 10, 15, 20, 30, 45, 60, 90, 120};
constexpr uint16_t weeklyMinutes[] = {0, 60, 120, 180, 240, 300, 420, 600, 840};
constexpr uint16_t streakDays[] = {0, 3, 7, 14, 30, 60, 100, 365, 730};
constexpr uint16_t booksPerYear[] = {0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 20, 24, 30, 36, 52, 100};

static_assert(dailyMinutes[std::size(dailyMinutes) - 1] <= CrossPointSettings::GOAL_MAX_DAILY_MINUTES);
static_assert(weeklyMinutes[std::size(weeklyMinutes) - 1] <= CrossPointSettings::GOAL_MAX_WEEKLY_MINUTES);
static_assert(streakDays[std::size(streakDays) - 1] <= CrossPointSettings::GOAL_MAX_STREAK_DAYS);
static_assert(booksPerYear[std::size(booksPerYear) - 1] <= CrossPointSettings::GOAL_MAX_BOOKS_PER_YEAR);

// The next preset above `current`, wrapping to off at the top. Takes the first
// strictly greater entry rather than an index, so a value restored from a
// hand-edited settings.json still lands somewhere sensible instead of being
// treated as "not in the table" and lost.
uint16_t nextPreset(const uint16_t* table, const size_t count, const uint16_t current) {
  for (size_t i = 0; i < count; ++i) {
    if (table[i] > current) return table[i];
  }
  // Past the top of the table the cycle wraps to off. A value above it can only
  // come from a hand-edited settings.json, and wrapping straight to off from
  // there would look like the press did nothing but lose the goal.
  return current > table[count - 1] ? table[count - 1] : table[0];
}

}  // namespace

GoalsSettingsActivity::GoalsSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("GoalsSettings", renderer, mappedInput) {}

void GoalsSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  for (int i = 0; i < ITEM_COUNT; i++) {
    rowItems_[i].label = I18N.get(menuNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

void GoalsSettingsActivity::onExit() {
  // Once on the way out, not per press: cycling the presets would otherwise
  // write the file once for every value passed. The retrospective judges each
  // year by the goal it had.
  YEAR_GOALS.recordCurrent(SETTINGS.goalBooksPerYear);
  UiListActivity::onExit();
}

const char* GoalsSettingsActivity::headerTitle() const { return tr(STR_GOALS); }

void GoalsSettingsActivity::activateIndex(const int index) {
  nav.selected = index;
  app.clearTapFlash();
  switch (index) {
    case ITEM_DAILY:
      SETTINGS.goalDailyMinutes = nextPreset(dailyMinutes, std::size(dailyMinutes), SETTINGS.goalDailyMinutes);
      break;
    case ITEM_WEEKLY:
      SETTINGS.goalWeeklyMinutes = nextPreset(weeklyMinutes, std::size(weeklyMinutes), SETTINGS.goalWeeklyMinutes);
      break;
    case ITEM_STREAK:
      SETTINGS.goalStreakDays = nextPreset(streakDays, std::size(streakDays), SETTINGS.goalStreakDays);
      break;
    case ITEM_BOOKS:
      SETTINGS.goalBooksPerYear = nextPreset(booksPerYear, std::size(booksPerYear), SETTINGS.goalBooksPerYear);
      break;
    default:
      return;
  }
  SETTINGS.saveToFile();
  requestUpdate();
}

void GoalsSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // A zero target is off, and statsText::duration() renders zero as "--", which
  // would read as a broken value rather than a deliberate one.
  const auto writeMinutes = [this](const int row, const uint16_t minutes) {
    if (minutes == 0) {
      std::snprintf(rowValues_[row], sizeof(rowValues_[row]), "%s", tr(STR_STATE_OFF));
      return;
    }
    statsText::duration(rowValues_[row], sizeof(rowValues_[row]), minutes);
  };
  const auto writeCount = [this](const int row, const uint16_t value) {
    if (value == 0) {
      std::snprintf(rowValues_[row], sizeof(rowValues_[row]), "%s", tr(STR_STATE_OFF));
      return;
    }
    std::snprintf(rowValues_[row], sizeof(rowValues_[row]), "%u", static_cast<unsigned>(value));
  };
  const auto writeCounted = [this](const int row, const uint16_t value, const StrId unit) {
    if (value == 0) {
      std::snprintf(rowValues_[row], sizeof(rowValues_[row]), "%s", tr(STR_STATE_OFF));
      return;
    }
    std::snprintf(rowValues_[row], sizeof(rowValues_[row]), I18N.get(unit), static_cast<unsigned>(value));
  };

  writeMinutes(ITEM_DAILY, SETTINGS.goalDailyMinutes);
  writeMinutes(ITEM_WEEKLY, SETTINGS.goalWeeklyMinutes);
  // Its own key, not the stats grid's cramped "%u d": this row has width to
  // spare, and borrowing an achievement's wording would tie two unrelated
  // screens to one string.
  writeCounted(ITEM_STREAK, SETTINGS.goalStreakDays, StrId::STR_GOAL_DAYS_UNIT);
  // The label already says "books"; repeating it reads as a stutter.
  writeCount(ITEM_BOOKS, SETTINGS.goalBooksPerYear);
  for (int i = 0; i < ITEM_COUNT; i++) rowItems_[i].value = rowValues_[i];

  fui::ListProps props;
  props.items = rowItems_;
  props.count = ITEM_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
