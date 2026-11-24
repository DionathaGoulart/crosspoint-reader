#include "ProfilesSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "profiles/ProfileStore.h"
#include "profiles/ProfileSwitch.h"

namespace fui = freeink::ui;

ProfilesSettingsActivity::ProfilesSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Profiles", renderer, mappedInput) {}

const char* ProfilesSettingsActivity::headerTitle() const { return tr(STR_PROFILES); }

void ProfilesSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  nav.selected = 0;
  rebuildRows();
}

void ProfilesSettingsActivity::rebuildRows() {
  const auto& profiles = PROFILES.all();
  const size_t rows = profiles.size() + 1;  // plus the row that adds one

  // Labels and subtitles are rebuilt wholesale, and ListItem holds raw
  // pointers into them, so the strings are sized up front: a reallocation
  // mid-build would leave the items pointing at freed text.
  rowIds_.clear();
  rowLabels_.clear();
  rowSubtitles_.clear();
  rowItems_.clear();
  rowIds_.reserve(profiles.size());
  rowLabels_.reserve(rows);
  rowSubtitles_.reserve(rows);
  rowItems_.reserve(rows);

  for (const Profile& profile : profiles) {
    rowIds_.push_back(profile.id);
    // An empty name is the default profile, which is named in the reader's
    // language rather than carrying an English word on the card.
    rowLabels_.push_back(profile.name.empty() ? tr(STR_PROFILE_DEFAULT) : profile.name);
    rowSubtitles_.push_back(profile.id == PROFILES.activeId() ? tr(STR_PROFILE_IN_USE) : std::string());
  }
  rowLabels_.push_back(tr(STR_PROFILE_NEW));
  rowSubtitles_.push_back(std::string());

  for (size_t i = 0; i < rows; ++i) {
    fui::ListItem item;
    item.label = rowLabels_[i].c_str();
    if (!rowSubtitles_[i].empty()) item.subtitle = rowSubtitles_[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
}

void ProfilesSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

bool ProfilesSettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void ProfilesSettingsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

void ProfilesSettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(rowItems_.size())) return;
  if (index == static_cast<int>(rowIds_.size())) {
    promptNewProfile();
    return;
  }
  showProfileOptions(index);
}

void ProfilesSettingsActivity::showProfileOptions(const int row) {
  if (row < 0 || row >= static_cast<int>(rowIds_.size())) return;
  const uint8_t id = rowIds_[static_cast<size_t>(row)];
  const bool isActive = id == PROFILES.activeId();
  const bool isDefault = id == profilePath::DEFAULT_ID;

  // Built per press rather than kept, because which entries apply depends on
  // the row: the profile in use cannot be switched to, and the default profile
  // cannot be deleted because its files are the card's own.
  std::vector<const char*> options;
  enum Choice : uint8_t { Use, Rename, Delete };
  std::vector<Choice> choices;
  options.reserve(3);
  choices.reserve(3);
  if (!isActive) {
    // An imperative, not the "In use" badge: this entry only exists for
    // profiles that are not in use, so the badge named the wrong one.
    options.push_back(tr(STR_PROFILE_USE));
    choices.push_back(Use);
  }
  options.push_back(tr(STR_RENAME));
  choices.push_back(Rename);
  if (!isDefault) {
    options.push_back(tr(STR_DELETE));
    choices.push_back(Delete);
  }

  optionPopup.show(tr(STR_PROFILES), rowLabels_[static_cast<size_t>(row)].c_str(), options.data(),
                   static_cast<int>(options.size()), 0, [this, id, choices](const int chosen) {
                     if (chosen < 0 || chosen >= static_cast<int>(choices.size())) return;
                     switch (choices[static_cast<size_t>(chosen)]) {
                       case Use:
                         switchToProfile(id);
                         break;
                       case Rename:
                         promptRename(id);
                         break;
                       case Delete:
                         promptDelete(id);
                         break;
                     }
                   });
  requestUpdate();
}

void ProfilesSettingsActivity::promptNewProfile() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_PROFILE_NAME), "",
                                                           ProfileStore::MAX_NAME_LENGTH, InputType::Text);
  if (!keyboard) {
    LOG_ERR("PROFILE", "OOM: name keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const std::string name = std::get<KeyboardResult>(result.data).text;
    // create() refuses an empty or duplicate name; the row list simply does not
    // grow, which is the honest feedback on a screen with no room for an error
    // line.
    if (!PROFILES.create(name)) LOG_ERR("PROFILE", "Could not create profile");
    rebuildRows();
    requestUpdate();
  });
}

void ProfilesSettingsActivity::promptRename(const uint8_t id) {
  const Profile* profile = PROFILES.find(id);
  if (profile == nullptr) return;
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_PROFILE_NAME), profile->name,
                                                           ProfileStore::MAX_NAME_LENGTH, InputType::Text);
  if (!keyboard) {
    LOG_ERR("PROFILE", "OOM: rename keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this, id](const ActivityResult& result) {
    if (result.isCancelled) return;
    if (!PROFILES.rename(id, std::get<KeyboardResult>(result.data).text)) {
      LOG_ERR("PROFILE", "Could not rename profile");
    }
    rebuildRows();
    requestUpdate();
  });
}

void ProfilesSettingsActivity::promptDelete(const uint8_t id) {
  const Profile* profile = PROFILES.find(id);
  if (profile == nullptr) return;
  // The body says what goes, because statistics and bookmarks are not
  // recoverable and the profile's name alone does not say that.
  // Question as the heading, subject and consequence in the body, like every
  // other ConfirmationActivity caller. The reader's name alone as a caption,
  // with a sentence in the third person under it, asked nothing.
  const std::string body = profile->name + "\n" + tr(STR_PROFILE_DELETE_BODY);
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_ITEM_CONFIRM), body,
                                                              tr(STR_CANCEL), tr(STR_DELETE));
  if (!confirmation) {
    LOG_ERR("PROFILE", "OOM: delete confirmation");
    return;
  }
  startActivityForResult(std::move(confirmation), [this, id](const ActivityResult& result) {
    if (result.isCancelled) return;
    // remove() leaves the active profile first when it is the one going, so
    // nothing in flight writes into a directory being removed.
    // Leave it first if it is the one in use. remove() refuses the active
    // profile on purpose: only switchTo() reloads the stores, and deleting
    // without that would leave this reader's recents, awards and settings in
    // memory, to be written over the default profile's files on the next save.
    // Taking the render lock here is safe: ActivityManager releases its own
    // before invoking a result handler, exactly so handlers can take one.
    if (id == PROFILES.activeId()) switchToProfile(profilePath::DEFAULT_ID);
    if (!PROFILES.remove(id)) LOG_ERR("PROFILE", "Could not delete profile");
    rebuildRows();
    requestUpdate();
  });
}

void ProfilesSettingsActivity::switchToProfile(const uint8_t id) {
  // The render lock is the point. switchTo() ends in UITheme::reload(), which
  // destroys the theme object the render task may be calling virtual methods
  // on, and on the way there it reloads four stores from the card while the
  // render task reads fonts and covers from it. This callback runs from
  // loop(), which ActivityManager does NOT hold the lock across.
  {
    RenderLock lock(*this);
    if (!profiles::switchTo(id)) return;
  }
  rebuildRows();
  requestUpdate();
}
