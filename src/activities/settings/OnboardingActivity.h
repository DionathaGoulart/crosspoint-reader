#pragma once

#include <string>
#include <vector>

#include "OnboardingPlan.h"
#include "SettingsList.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// The setup guide: a few short pages that put the main choices in front of a
// new reader, instead of leaving them to be found in Settings.
//
// One activity walks the pages, rather than one pushed activity per page, so
// the stack never grows past the page that is open. Each row changes one
// setting at once and saves it; nothing waits for the last page, so leaving
// halfway keeps what was chosen.
//
// Full mode runs on first boot and from Settings. ProfileOnly runs after a
// new profile is created, and shows only that reader's own settings (see
// OnboardingPlan.h for why).
class OnboardingActivity final : public UiListActivity {
 public:
  OnboardingActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, onboarding::Mode mode);

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  enum class Kind : uint8_t { Language, ProfileName, TextSettings, Goals, RemapFront, EnumSetting, Next };
  struct Row {
    Kind kind;
    // For EnumSetting: index into stepSettings_.
    uint8_t setting = 0;
  };

  int listCount() const override { return static_cast<int>(rows_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void onBackButton() override;
  const char* headerTitle() const override { return title_; }
  void drawFooter() override;

  // Rebuilds the rows, and their text, for the current page. Text is rebuilt
  // wholesale and ListItem holds raw pointers into it, so the vectors are
  // sized first; see ProfilesSettingsActivity::rebuildRows().
  void rebuildRows();
  void goToStep(size_t index);
  void finishSetup();
  void openEnumPopup(size_t settingIndex);
  void syncQuickResume(int chosenSleepScreen);
  void promptProfileName();

  const onboarding::Mode mode_;
  onboarding::Step steps_[onboarding::MAX_STEPS]{};
  size_t stepCount_ = 0;
  size_t stepIndex_ = 0;
  char title_[32]{};
  // True when this guide switched the Quick Resume timeout on by itself.
  bool quickResumeAutoEnabled_ = false;

  std::vector<Row> rows_;
  std::vector<std::string> rowLabels_;
  std::vector<std::string> rowValues_;
  std::vector<freeink::ui::ListItem> rowItems_;
  // The enum settings the current page edits, copied out of the shared list.
  std::vector<SettingInfo> stepSettings_;
  OptionPopup optionPopup;
};
