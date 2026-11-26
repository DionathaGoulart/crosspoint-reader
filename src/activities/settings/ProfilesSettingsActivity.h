#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// Who is reading, under General settings: one row per profile and a row to add
// one.
//
// Selecting a profile opens a short popup -- use, rename, delete -- rather than
// switching straight away or hiding those behind a long press. On a device with
// four buttons and no touch, a hidden gesture is a feature nobody finds, and an
// immediate switch on a stray press would reload every store and redraw the
// whole UI as someone's accident.
//
// The default profile can be renamed but not deleted: its files are the card's
// own, so deleting it would mean deleting everything.
class ProfilesSettingsActivity final : public UiListActivity {
 public:
  explicit ProfilesSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  int listCount() const override { return static_cast<int>(rowItems_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // The popup owns the pass while it is open.
  bool handleCustomInput() override;
  const char* headerTitle() const override;

  // Rebuilt when the profile list changes, not per repaint: the labels own
  // their storage, and a rebuild per frame would churn the heap behind the
  // render lock.
  void rebuildRows();

  // Switching reloads the stores and replaces the theme object, so it runs
  // under the render lock; see the definition.
  bool switchToProfile(uint8_t id);
  void showProfileOptions(int row);
  void promptNewProfile();
  void promptRename(uint8_t id);
  void promptDelete(uint8_t id);

  // Row index to profile id; the last row is the "new profile" action and has
  // no entry here.
  std::vector<uint8_t> rowIds_;
  // Label and subtitle text, kept alive for the ListItem pointers above them.
  std::vector<std::string> rowLabels_;
  std::vector<std::string> rowSubtitles_;
  std::vector<freeink::ui::ListItem> rowItems_;
  OptionPopup optionPopup;
};
