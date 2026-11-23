#include "StatsActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "components/UITheme.h"
#include "fontIds.h"
#include "stats/AchievementStore.h"
#include "stats/FinishedBooksStore.h"

namespace fui = freeink::ui;

namespace {

// Minutes as a row value, reusing the tile formatter so the tab and the sleep
// screen never disagree about what "1 h 05" means.
std::string duration(const uint32_t minutes) {
  char buffer[24];
  stats::formatDuration(buffer, sizeof(buffer), minutes);
  return buffer;
}

std::string number(const uint32_t value) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned>(value));
  return buffer;
}

std::string formatted(const StrId id, const uint32_t value) {
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), I18N.get(id), static_cast<unsigned>(value));
  return buffer;
}

std::string shortDate(const int64_t epoch) {
  if (epoch <= 0) return "";
  const time_t when = static_cast<time_t>(epoch);
  struct tm local {};
  localtime_r(&when, &local);
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%02d/%02d/%04d", local.tm_mday, local.tm_mon + 1, local.tm_year + 1900);
  return buffer;
}

}  // namespace

void StatsActivity::onEnter() {
  // Loaded once per entry. Folding the session log costs an SD pass, and a
  // repaint must never pay for it.
  snapshot = statsService::load();
  statsService::refreshAchievements(snapshot);
  rebuildRows();
  UiTabListActivity::onEnter();
}

const char* StatsActivity::headerTitle() const { return tr(STR_STATS_TITLE); }

const char* StatsActivity::tabLabel(const int index) const {
  switch (static_cast<Tab>(index)) {
    case Tab::Summary:
      return tr(STR_STATS_TAB_SUMMARY);
    case Tab::Books:
      return tr(STR_STATS_TAB_BOOKS);
    case Tab::Habits:
      return tr(STR_STATS_TAB_HABITS);
    case Tab::Awards:
      return tr(STR_STATS_TAB_AWARDS);
    default:
      return "";
  }
}

int StatsActivity::listCount() const { return static_cast<int>(rowItems.size()); }

void StatsActivity::switchTab(const int direction) {
  const int count = tabCount();
  const int next = ((static_cast<int>(tab) + direction) % count + count) % count;
  tab = static_cast<Tab>(next);
  detailIndex = -1;
  rebuildRows();
  activeNav().selected = -1;
  requestUpdate();
}

void StatsActivity::onTabAction(const int index) {
  if (index == static_cast<int>(tab)) return;
  tab = static_cast<Tab>(index);
  detailIndex = -1;
  rebuildRows();
  activeNav().selected = -1;
  requestUpdate();
}

void StatsActivity::rebuildRows() {
  rowLabels.clear();
  rowValues.clear();
  bookOrder.clear();

  const stats::Summary& s = snapshot.summary;
  const auto add = [this](std::string label, std::string value) {
    rowLabels.push_back(std::move(label));
    rowValues.push_back(std::move(value));
  };

  switch (tab) {
    case Tab::Summary:
      add(tr(STR_STATS_TOTAL), duration(s.totalMinutes));
      add(tr(STR_STATS_TODAY), duration(s.todayMinutes));
      add(tr(STR_STATS_WEEK), duration(s.weekMinutes));
      add(tr(STR_STATS_STREAK), formatted(StrId::STR_STATS_DAYS_UNIT, s.currentStreak));
      add(tr(STR_STATS_RECORD), formatted(StrId::STR_STATS_DAYS_UNIT, s.longestStreak));
      add(tr(STR_STATS_BOOKS_FINISHED), number(s.booksFinished));
      add(tr(STR_STATS_THIS_YEAR), number(s.booksThisYear));
      break;

    case Tab::Books: {
      const auto& books = FINISHED_BOOKS.getBooks();
      for (size_t i = 0; i < books.size(); ++i) {
        const auto* stat = stats::findBook(*snapshot.aggregate, books[i].document.c_str());
        // The title is what the book called itself when it was finished, so a
        // deleted file still shows a name rather than a path that is gone.
        add(books[i].title.empty() ? books[i].path : books[i].title,
            stat != nullptr ? duration(stat->secs / 60) : "");
        bookOrder.push_back(i);
      }
      break;
    }

    case Tab::Habits:
      add(tr(STR_STATS_DAYS_READ), formatted(StrId::STR_STATS_DAYS_UNIT, s.daysRead));
      add(tr(STR_STATS_SESSIONS), number(s.sessionCount));
      add(tr(STR_STATS_LONGEST_SESSION), duration(s.longestSessionMinutes));
      add(tr(STR_STATS_AVG_READ_DAY), duration(s.avgMinutesPerReadDay));
      add(tr(STR_STATS_AVG_CAL_DAY), duration(s.avgMinutesPerCalendarDay));
      add(tr(STR_STATS_BOOKS_STARTED), number(s.booksStarted));
      if (s.bestHour >= 0) {
        add(tr(STR_STATS_BEST_HOUR), formatted(StrId::STR_STATS_HOUR_UNIT, static_cast<uint32_t>(s.bestHour)));
      }
      break;

    case Tab::Awards:
      for (size_t i = 0; i < achievements::kCount; ++i) {
        const auto id = static_cast<achievements::Id>(i);
        const uint32_t goal = achievements::target(id);
        StrId label = StrId::STR_ACH_BOOKS;
        switch (achievements::kindOf(id)) {
          case achievements::Kind::Books:
            label = StrId::STR_ACH_BOOKS;
            break;
          case achievements::Kind::Hours:
            label = StrId::STR_ACH_HOURS;
            break;
          case achievements::Kind::Streak:
            label = StrId::STR_ACH_STREAK;
            break;
          case achievements::Kind::Session:
            label = StrId::STR_ACH_SESSION;
            break;
          case achievements::Kind::Languages:
            label = StrId::STR_ACH_LANGUAGES;
            break;
        }
        std::string value;
        if (ACHIEVEMENTS.isUnlocked(id)) {
          // A date when the clock was set, otherwise just the fact.
          value = shortDate(ACHIEVEMENTS.unlockedEpoch(id));
          if (value.empty()) value = tr(STR_SELECTED);
        } else {
          value = number(achievements::progressPercent(id, snapshot.inputs)) + "%";
        }
        add(formatted(label, goal), std::move(value));
      }
      break;

    default:
      break;
  }

  rowItems.assign(rowLabels.size(), fui::ListItem{});
  for (size_t i = 0; i < rowItems.size(); ++i) {
    rowItems[i].label = rowLabels[i].c_str();
    rowItems[i].value = rowValues[i].empty() ? nullptr : rowValues[i].c_str();
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
}

void StatsActivity::buildScreen(UiScreen& screen) {
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int tabTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int rightInset = renderer.getScreenWidth() - (safeArea.x + safeArea.width);
  int bottomInset = renderer.getScreenHeight() - (safeArea.y + safeArea.height);
  // The Summary tab keeps the lower band for the month map, which the list
  // must not scroll into.
  if (tab == Tab::Summary && !snapshot.clockUnset && snapshot.summary.sessionCount > 0) {
    bottomInset += 132;
  }
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(tabTop), static_cast<int16_t>(rightInset),
                                                static_cast<int16_t>(bottomInset), static_cast<int16_t>(safeArea.x)});

  buildTabBar(screen);

  if (rowItems.empty()) {
    const auto body = screen.body();
    const StrId message = tab == Tab::Books ? StrId::STR_STATS_NO_BOOKS : StrId::STR_STATS_NO_DATA;
    fui::TextStyle style = screen.theme().smallText;
    style.align = fui::TextAlign::Center;
    style.maxLines = 3;
    screen.target().text(fui::makeRect(body.x, body.y + body.height / 3, body.width, 96), I18N.get(message), style);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncTabListViewport(screen, props);
  screen.list(props);
}

void StatsActivity::drawChrome() {
  UiTabListActivity::drawChrome();
  if (tab != Tab::Summary || snapshot.clockUnset || snapshot.summary.sessionCount == 0) return;
  drawMonthMap(renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight - 126);
}

void StatsActivity::drawMonthMap(const int top) const {
  if (!snapshot.aggregate) return;
  uint16_t days[30];
  const size_t count = stats::monthMap(*snapshot.aggregate, snapshot.summary.today, 30, days, 30);
  if (count == 0) return;

  const int pageWidth = renderer.getScreenWidth();
  const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, tr(STR_STATS_MONTH_MAP));
  renderer.drawText(SMALL_FONT_ID, (pageWidth - labelWidth) / 2, top, tr(STR_STATS_MONTH_MAP));

  constexpr int kColumns = 10;
  const int cell = 20;
  const int gap = 6;
  const int gridWidth = kColumns * cell + (kColumns - 1) * gap;
  const int startX = (pageWidth - gridWidth) / 2;
  const int gridTop = top + renderer.getLineHeight(SMALL_FONT_ID) + 8;
  for (size_t i = 0; i < count; ++i) {
    const int x = startX + static_cast<int>(i % kColumns) * (cell + gap);
    const int y = gridTop + static_cast<int>(i / kColumns) * (cell + gap);
    // Filled is a day with reading; the outline keeps an empty day visible
    // rather than leaving a hole the eye reads as the end of the month.
    if (days[i] > 0) {
      renderer.fillRect(x, y, cell, cell, true);
    } else {
      renderer.drawRect(x, y, cell, cell, true);
    }
  }
}

void StatsActivity::activateIndex(const int index) {
  if (tab != Tab::Books) return;
  if (index < 0 || static_cast<size_t>(index) >= bookOrder.size()) return;
  detailIndex = index;
  requestUpdate();
}

bool StatsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (detailIndex >= 0) {
      detailIndex = -1;
      requestUpdate();
      return true;
    }
    finish();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (detailIndex >= 0) {
      detailIndex = -1;
      requestUpdate();
      return true;
    }
    if (ringPos() == 0) {
      switchTab();
    } else {
      activateIndex(ringPos() - 1);
    }
    return true;
  }
  return false;
}

void StatsActivity::render(RenderLock&& lock) {
  if (detailIndex < 0) {
    UiTabListActivity::render(std::move(lock));
    return;
  }
  renderBookDetail();
}

void StatsActivity::renderBookDetail() {
  const auto& books = FINISHED_BOOKS.getBooks();
  const size_t index = bookOrder[static_cast<size_t>(detailIndex)];
  if (index >= books.size()) {
    detailIndex = -1;
    return;
  }
  const FinishedBook& book = books[index];
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 book.title.empty() ? book.path.c_str() : book.title.c_str());

  int y = metrics.topPadding + metrics.headerHeight + 18;

  // The cover, when the reader's cache still holds one. Constructing an Epub
  // only derives the cache path from the file name; it parses nothing.
  const std::string coverPath = Epub(book.path, "/.crosspoint").getCoverBmpPath();
  const int coverHeight = pageHeight / 3;
  if (Storage.exists(coverPath.c_str())) {
    HalFile file;
    if (Storage.openFileForRead("STATS", coverPath, file)) {
      Bitmap cover(file);
      if (cover.parseHeaders() == BmpReaderError::Ok) {
        const int width = std::min(cover.getWidth(), pageWidth - 80);
        renderer.drawBitmap(cover, (pageWidth - width) / 2, y, width, coverHeight);
      }
      file.close();
    }
  }
  y += coverHeight + 24;

  if (!book.author.empty()) {
    renderer.drawCenteredText(SMALL_FONT_ID, y, book.author.c_str());
    y += renderer.getLineHeight(SMALL_FONT_ID) + 16;
  }

  const auto* stat = stats::findBook(*snapshot.aggregate, book.document.c_str());
  const int left = 40;
  const auto line = [&](const char* label, const std::string& value) {
    renderer.drawText(SMALL_FONT_ID, left, y, label);
    const int width = renderer.getTextWidth(SMALL_FONT_ID, value.c_str());
    renderer.drawText(SMALL_FONT_ID, pageWidth - left - width, y, value.c_str());
    y += renderer.getLineHeight(SMALL_FONT_ID) + 10;
  };
  if (stat != nullptr) {
    line(tr(STR_STATS_TOTAL), duration(stat->secs / 60));
    line(tr(STR_STATS_SESSIONS), number(stat->sessions));
  }
  if (book.finishedEpoch > 0) line(tr(STR_STATS_BOOKS_FINISHED), shortDate(book.finishedEpoch));

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
