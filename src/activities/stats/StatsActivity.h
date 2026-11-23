#pragma once

#include <array>
#include <string>
#include <vector>

#include "activities/UiTabListActivity.h"
#include "components/themes/BaseTheme.h"  // Rect
#include "stats/StatsService.h"

// Reading statistics, as four tabs of rows.
//
// Rows rather than a hand-laid grid of cards: UiTabListActivity already owns
// tab navigation, the selection ring, viewport scrolling and the button
// semantics, all of which work on a device with no touchscreen. A bespoke
// layout would re-implement every one of them and be wrong on a panel size
// nobody tested. The one thing drawn by hand is the month map, which no list
// can express.
//
// The snapshot is loaded once per entry, not per render: it is ~22 KB and
// folding the session log on every repaint would make the screen crawl.
class StatsActivity final : public UiTabListActivity {
 public:
  StatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiTabListActivity("Stats", renderer, mappedInput) {}

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  enum class Tab : uint8_t { Summary, Books, Habits, Awards, Count };

  // --- UiTabListActivity contract --------------------------------------------
  int listCount() const override;
  int tabCount() const override { return static_cast<int>(Tab::Count); }
  int activeTab() const override { return static_cast<int>(tab); }
  const char* tabLabel(int index) const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onTabAction(int index) override;
  void stepTab(int direction) override { switchTab(direction); }
  bool handleButtons() override;
  const char* headerTitle() const override;
  void drawChrome() override;

  void switchTab(int direction = 1);
  void rebuildRows();

  // One book's figures, filling the screen. Entered with Confirm on a book row
  // and left with Back; the cover is worth a whole screen on a panel this size,
  // and a thumbnail in a row would be unreadable.
  void renderBookDetail();
  void drawMonthMap(int top) const;
  void drawSummaryPanel() const;
  void drawStatTile(const Rect& box, const char* label, const char* value, bool emphasis = false) const;

  statsService::Snapshot snapshot;
  Tab tab = Tab::Summary;
  // -1 while the list is showing; otherwise the index into finishedBooks.
  int detailIndex = -1;

  // Row storage, rebuilt on tab switch rather than per render.
  std::vector<freeink::ui::ListItem> rowItems;
  std::vector<std::string> rowLabels;
  std::vector<std::string> rowValues;
  // Books in the order the Books tab lists them, newest finished first.
  std::vector<size_t> bookOrder;
};
