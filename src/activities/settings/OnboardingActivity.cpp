#include "OnboardingActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "ButtonRemapActivity.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "GoalsSettingsActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "TextSettingsActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "profiles/ProfileStore.h"

namespace fui = freeink::ui;

namespace {

// Keys of the enum settings each page edits, in row order.
struct StepKeys {
  const char* keys[3];
  size_t count;
};

StepKeys keysFor(const onboarding::Step step) {
  switch (step) {
    case onboarding::Step::Controls:
      return {{"orientation", "sideButtonLayout"}, 2};
    case onboarding::Step::Sleep:
      return {{"sleepScreen"}, 1};
    case onboarding::Step::Summary:
      return {{"uiSkin", "orientation", "sleepScreen"}, 3};
    case onboarding::Step::Theme:
      // The row changes the theme at once, so the pages after it already use the choice.
      return {{"uiSkin"}, 1};
    default:
      return {{}, 0};
  }
}

struct Collector {
  const StepKeys* wanted;
  SettingInfo* out;  // wanted->count entries
  bool found[3];
};

void collect(void* context, SettingInfo& info) {
  auto& c = *static_cast<Collector*>(context);
  if (info.key == nullptr) return;
  for (size_t i = 0; i < c.wanted->count; ++i) {
    if (!c.found[i] && std::strcmp(info.key, c.wanted->keys[i]) == 0) {
      c.out[i] = std::move(info);
      c.found[i] = true;
      return;
    }
  }
}

}  // namespace

OnboardingActivity::OnboardingActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       const onboarding::Mode mode)
    : UiListActivity("Onboarding", renderer, mappedInput), mode_(mode) {
  stepCount_ = onboarding::stepsFor(mode_, steps_);
}

void OnboardingActivity::onEnter() {
  UiListActivity::onEnter();
  stepIndex_ = 0;
  rebuildRows();
}

void OnboardingActivity::goToStep(const size_t index) {
  stepIndex_ = index;
  rebuildRows();
  nav.reset(0);
  requestUpdate();
}

void OnboardingActivity::rebuildRows() {
  // The render task reads these buffers; rebuild them while it is held off.
  RenderLock lock(*this);
  snprintf(title_, sizeof(title_), tr(STR_SETUP_STEP), static_cast<int>(stepIndex_ + 1), static_cast<int>(stepCount_));

  rows_.clear();
  stepSettings_.clear();
  // At most two enum rows, three fixed rows and Next: reserve so the rows are
  // not reallocated while they are filled.
  rows_.reserve(6);
  stepSettings_.reserve(3);
  const onboarding::Step step = steps_[stepIndex_];

  // One pass over the shared list for this page's settings. The list is large,
  // so it is walked once and only the few wanted entries are kept.
  const StepKeys wanted = keysFor(step);
  if (wanted.count > 0) {
    SettingInfo found[3];
    Collector collector{&wanted, found, {false, false, false}};
    forEachBaseSetting(&collect, &collector);
    for (size_t i = 0; i < wanted.count; ++i) {
      if (!collector.found[i]) {
        LOG_ERR("SETUP", "Setting not found: %s", wanted.keys[i]);
        continue;
      }
      if (found[i].type != SettingType::ENUM || found[i].valuePtr == nullptr) continue;
      stepSettings_.push_back(std::move(found[i]));
      rows_.push_back({Kind::EnumSetting, static_cast<uint8_t>(stepSettings_.size() - 1)});
    }
  }

  switch (step) {
    case onboarding::Step::Welcome:
      rows_.push_back({Kind::Language});
      rows_.push_back({Kind::ProfileName});
      break;
    case onboarding::Step::Reading:
      rows_.push_back({Kind::TextSettings});
      break;
    case onboarding::Step::Controls:
      // Touch boards have no front buttons to remap.
      if (!BoardConfig::hasTouch()) rows_.push_back({Kind::RemapFront});
      break;
    case onboarding::Step::Goals:
      rows_.push_back({Kind::Goals});
      break;
    case onboarding::Step::Summary:
      rows_.push_back({Kind::Language});
      break;
    case onboarding::Step::Theme:
    case onboarding::Step::Sleep:
      break;
  }
  rows_.push_back({Kind::Next});

  rowLabels_.clear();
  rowValues_.clear();
  rowItems_.clear();
  rowLabels_.reserve(rows_.size());
  rowValues_.reserve(rows_.size());
  rowItems_.reserve(rows_.size());

  const bool last = stepIndex_ + 1 == stepCount_;
  for (const Row& row : rows_) {
    std::string label;
    std::string value;
    switch (row.kind) {
      case Kind::Language:
        label = tr(STR_LANGUAGE);
        value = I18N.getLanguageName(I18N.getLanguage());
        break;
      case Kind::ProfileName: {
        label = tr(STR_PROFILE_NAME);
        const Profile* profile = PROFILES.find(PROFILES.activeId());
        value = profile == nullptr || profile->name.empty() ? tr(STR_PROFILE_DEFAULT) : profile->name;
        break;
      }
      case Kind::TextSettings:
        label = tr(STR_TEXT_SETTINGS);
        break;
      case Kind::Goals:
        label = tr(STR_GOALS);
        break;
      case Kind::RemapFront:
        label = tr(STR_REMAP_FRONT_BUTTONS);
        break;
      case Kind::EnumSetting: {
        const SettingInfo& setting = stepSettings_[row.setting];
        label = I18N.get(setting.nameId);
        const auto labels = setting.enumLabels();
        const uint8_t current = SETTINGS.*(setting.valuePtr);
        if (current < labels.size()) value = I18N.get(labels[current]);
        break;
      }
      case Kind::Next:
        label = last ? tr(STR_DONE) : tr(STR_SETUP_NEXT);
        break;
    }
    rowLabels_.push_back(std::move(label));
    rowValues_.push_back(std::move(value));
  }

  for (size_t i = 0; i < rows_.size(); ++i) {
    fui::ListItem item;
    item.label = rowLabels_[i].c_str();
    if (!rowValues_[i].empty()) item.value = rowValues_[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
}

void OnboardingActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // A short reassurance under the list, so the first page is not a bare menu.
  const auto& theme = screen.theme();
  fui::TextStyle hint = theme.smallText;
  hint.align = fui::TextAlign::Center;
  hint.maxLines = 3;
  const int16_t hintHeight = static_cast<int16_t>(screen.target().lineHeight(hint.font) * hint.maxLines);
  const fui::Rect hintRect = screen.takeBottom(hintHeight, theme.spaceSm);
  screen.target().text(hintRect, tr(STR_SETUP_INTRO), hint);

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

bool OnboardingActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void OnboardingActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

void OnboardingActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(stepIndex_ > 0 ? tr(STR_BACK) : tr(STR_SETUP_SKIP), tr(STR_SELECT),
                                            tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void OnboardingActivity::onBackButton() {
  // Back steps to the previous page; on the first page it leaves the guide.
  if (stepIndex_ > 0) {
    goToStep(stepIndex_ - 1);
  } else {
    finishSetup();
  }
}

void OnboardingActivity::finishSetup() {
  // The flag is the device's first-boot state. A guide for one new profile
  // must not decide it.
  if (mode_ == onboarding::Mode::Full) {
    APP_STATE.onboardingDone = true;
    APP_STATE.saveToFile();
  }
  finish();
}

void OnboardingActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(rows_.size())) return;
  const Row row = rows_[static_cast<size_t>(index)];
  // The row either opens a screen or a popup, so a lingering tap flash would
  // gray an unrelated element on the next render.
  app.clearTapFlash();
  const auto refresh = [this](const ActivityResult&) {
    rebuildRows();
    requestUpdate();
  };

  switch (row.kind) {
    case Kind::Next:
      if (stepIndex_ + 1 < stepCount_) {
        goToStep(stepIndex_ + 1);
      } else {
        finishSetup();
      }
      break;
    case Kind::Language:
      if (auto activity = makeUniqueNoThrow<LanguageSelectActivity>(renderer, mappedInput)) {
        startActivityForResult(std::move(activity), refresh);
      } else {
        LOG_ERR("SETUP", "OOM: language");
      }
      break;
    case Kind::ProfileName:
      promptProfileName();
      break;
    case Kind::TextSettings:
      if (auto activity = makeUniqueNoThrow<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                  TextSettingsActivity::Tab::Family)) {
        startActivityForResult(std::move(activity), refresh);
      } else {
        LOG_ERR("SETUP", "OOM: text settings");
      }
      break;
    case Kind::Goals:
      if (auto activity = makeUniqueNoThrow<GoalsSettingsActivity>(renderer, mappedInput)) {
        startActivityForResult(std::move(activity), refresh);
      } else {
        LOG_ERR("SETUP", "OOM: goals");
      }
      break;
    case Kind::RemapFront:
      if (auto activity = makeUniqueNoThrow<ButtonRemapActivity>(renderer, mappedInput)) {
        startActivityForResult(std::move(activity), refresh);
      } else {
        LOG_ERR("SETUP", "OOM: button remap");
      }
      break;
    case Kind::EnumSetting:
      openEnumPopup(row.setting);
      break;
  }
}

void OnboardingActivity::openEnumPopup(const size_t settingIndex) {
  if (settingIndex >= stepSettings_.size()) return;
  const SettingInfo& setting = stepSettings_[settingIndex];
  const auto labels = setting.enumLabels();
  const auto valuePtr = setting.valuePtr;
  optionPopup.show(setting.nameId, labels.data(), static_cast<int>(labels.size()), SETTINGS.*valuePtr,
                   [this, valuePtr](const int chosen) {
                     SETTINGS.*valuePtr = static_cast<uint8_t>(chosen);
                     if (valuePtr == &CrossPointSettings::sleepScreen) syncQuickResume(chosen);
                     SETTINGS.saveToFile();
                     rebuildRows();
                     requestUpdate();
                   });
  requestUpdate();
}

// Quick Resume needs its idle-timeout switch on. As in Settings, a switch this
// page turned on is turned off again when the reader picks another screen; one
// the reader had on before stays on.
void OnboardingActivity::syncQuickResume(const int chosen) {
  using Settings = CrossPointSettings;
  const bool timeoutOn =
      SETTINGS.quickResumeSleepScreen == Settings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  if (chosen == Settings::SLEEP_SCREEN_MODE::QUICK_RESUME) {
    if (!timeoutOn) {
      SETTINGS.quickResumeSleepScreen = Settings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
      quickResumeAutoEnabled_ = true;
    }
  } else if (quickResumeAutoEnabled_) {
    SETTINGS.quickResumeSleepScreen = Settings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;
    quickResumeAutoEnabled_ = false;
  }
}

void OnboardingActivity::promptProfileName() {
  const Profile* profile = PROFILES.find(PROFILES.activeId());
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_PROFILE_NAME),
                                                           profile ? profile->name : std::string(),
                                                           ProfileStore::MAX_NAME_LENGTH, InputType::Text);
  if (!keyboard) {
    LOG_ERR("SETUP", "OOM: name keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (!result.isCancelled) {
      const std::string name = std::get<KeyboardResult>(result.data).text;
      // rename() refuses an empty or duplicate name; the row then keeps the
      // old name, which is the feedback a page with no error line can give.
      if (!PROFILES.rename(PROFILES.activeId(), name)) LOG_ERR("SETUP", "Could not rename profile");
    }
    rebuildRows();
    requestUpdate();
  });
}
