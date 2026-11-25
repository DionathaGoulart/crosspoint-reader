#pragma once

#include <array>
#include <string>
#include <tuple>
#include <vector>

#include "HomeCoverCache.h"
#include "RecentBooksStore.h"
#include "UiAppHost.h"
#include "components/bars/tab-bar.h"
#include "components/media/book-card.h"
#include "components/media/cover-grid.h"
#include "stats/ShelfStatus.h"
#include "stats/StatsView.h"

class CoverGridHomeUi final : public UiAppHost {
 public:
  static constexpr int THUMB_HEIGHT = 400;
  static constexpr int GRID_COLUMNS = 3;
  static constexpr int GRID_ROWS = 2;
  static constexpr int MAX_BOOKS = 1 + GRID_COLUMNS * GRID_ROWS;
  // Books this home actually draws. The goals variant shows one cover and a
  // strip of targets, so loading a grid's worth would generate thumbnails
  // nobody sees and let the side keys select books that are not on screen.
  static int bookSlots();
  static_assert(MAX_BOOKS <= HomeCoverCache::MAX_COVERS);
  // Tab positions the home's strip can draw, in the order of
  // HomeActivity::indexToMenuItem(). HomeActivity static_asserts its own
  // menu against this, because it owns that order; this class only draws it.
  static constexpr uint8_t TAB_POSITIONS = 6;
  // What the fourth tab opens; NONE hides it.
  enum class SlotTab : uint8_t { NONE, PLUGINS, OPDS };

  explicit CoverGridHomeUi(GfxRenderer& renderer);
  void begin(const std::vector<RecentBook>& books, SlotTab slotTab, bool hasContinueReading);
  void refreshCoverPaths();
  void setSelection(int selection) { selected = selection; }
  int selectedAction(const MappedInputManager& input);
  // Exact generation height, shared by every slot and recorded during draw.
  // Thumbs must be generated at the drawn size: rescaling a dithered 1-bit
  // image aliases badly.
  int thumbHeightFor() const;
  bool takeThumbHeightChanged();

 private:
  static void screenFn(UiScreen& screen, void* user);
  static void onAction(const freeink::ui::ActionEvent& event, void* user);
  void draw(UiScreen& screen);
  void drawHeaderBand();
  void drawEmpty(UiScreen& screen);
  // coverColumns is how many of these covers the width must hold: the grid
  // home fits three, the goals home gives the single cover half the screen.
  void drawCurrent(UiScreen& screen, freeink::ui::Rect rect, int coverRowHeight, int coverColumns);
  void drawGrid(UiScreen& screen);
  // Goals home only: the reader's targets, where the grid of other books goes.
  void drawGoals(UiScreen& screen, freeink::ui::Rect rect);
  // Height drawGoals needs for the goals currently set.
  int goalsStripHeight(UiScreen& screen);
  freeink::ui::Rect layoutGrid(freeink::ui::Rect rect);
  void drawTabs(UiScreen& screen, freeink::ui::Rect rect);
  bool paintFramedCover(freeink::ui::DrawTarget& target, freeink::ui::Rect rect, size_t index);
  void refreshCoverPath(size_t index);
  void noteThumbHeight(int slotHeight);

  HomeCoverCache coverCache;
  GfxRenderer& renderer;
  const std::vector<RecentBook>* books = nullptr;
  std::array<std::string, MAX_BOOKS> coverPaths;
  // Shelf-status tag per cover, read once in begin().
  std::array<shelfstatus::Tag, MAX_BOOKS> coverTags{};
  freeink::ui::TextStyle tagText{};
  int thumbHeight = 0;
  bool thumbHeightChanged = false;
  int selected = 0;
  int pending = -1;
  int progress = -1;
  SlotTab slotTab = SlotTab::NONE;
  bool hasContinueReading = false;
  // Goals home: the figures the targets are measured against, read once in
  // begin(). Only the Summary is kept -- statsService::load() also builds a
  // ~22 KB aggregate, and holding that for the life of the home screen to
  // redraw four bars would be the most expensive thing on it.
  bool goalsMode = false;
  stats::Summary summary{};
  char progressText[12]{};
  // Component styles and interaction tables stay off the render task's stack.
  freeink::ui::BookCardProps card;
  freeink::ui::CoverGridProps grid;
  freeink::ui::Rect gridBounds{};
  freeink::ui::TabBarProps tabs;
  // Six tab positions: see drawTabs(), which pairs them with
  // HomeActivity::indexToMenuItem(). One is skipped when the reader has
  // neither plugins nor a catalog, so five is the common case.
  std::array<freeink::ui::TabItem, TAB_POSITIONS> tabItems;
};
