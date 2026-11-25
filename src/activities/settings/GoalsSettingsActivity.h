#pragma once
#include "activities/UiListActivity.h"

// Reading goals under General settings: a daily and a weekly minute target, a
// streak to keep, and a book count for the year.
//
// Each row steps through a short list of presets instead of opening a slider.
// Four presses to cross a range beats a dialog on a button-only device, and it
// keeps "off" in the same cycle, so turning a goal back off needs no separate
// control. The preset tables live in the .cpp.
//
// Nothing here measures anything: the targets are read back by src/stats/Goals.h,
// which derives progress from the summary the firmware already folds.
class GoalsSettingsActivity final : public UiListActivity {
 public:
  explicit GoalsSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int ITEM_COUNT = 4;

  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override { return ITEM_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // Row values are formatted per frame, so each needs storage whose pointer
  // stays valid across the build. Sized for "1 h 30" and "%u books" alike.
  char rowValues_[ITEM_COUNT][24]{};
  freeink::ui::ListItem rowItems_[ITEM_COUNT]{};
};
