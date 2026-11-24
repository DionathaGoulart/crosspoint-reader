#include "LibraryCoverGrid.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookThumbnail.h"

namespace fui = freeink::ui;

namespace {
constexpr int16_t CELL_GAP = 8;
constexpr int16_t LABEL_GAP = 2;
constexpr uint8_t LABEL_LINES = 2;
}  // namespace

void LibraryCoverGrid::invalidate() {
  windowFirst = -1;
  windowCount = 0;
  pageCovers.clear();
  // The cache keys a snapshot by slot and rect, not by book. Slot i is a
  // different book after the rows move, and the cell rects do not change, so
  // the snapshots have to go with the window.
  if (coverCache) coverCache->invalidate();
}

int LibraryCoverGrid::thumbHeightFor(const fui::Rect body) const {
  // Bounded by the column width at the 2:3 cover aspect and by the row height,
  // both measured from the rect the grid is actually given: screen metrics do
  // not know what the tab strip and the footer already took.
  const int columnWidth = std::max(1, (body.width - CELL_GAP * (COLUMNS + 1)) / COLUMNS);
  const int byWidth = columnWidth * 3 / 2;
  const int labelHeight = renderer.getLineHeight(SMALL_FONT_ID) * LABEL_LINES + LABEL_GAP;
  const int rowsWanted = 3;
  const int rowHeight = std::max(1, (body.height - CELL_GAP * (rowsWanted - 1)) / rowsWanted);
  return std::max(1, std::min(byWidth, rowHeight - labelHeight));
}

int LibraryCoverGrid::rowHeightFor(const int thumbHeight) const {
  return thumbHeight + renderer.getLineHeight(SMALL_FONT_ID) * LABEL_LINES + LABEL_GAP;
}

void LibraryCoverGrid::resolvePage(const int first, const int count, const int thumbHeight) {
  if (!source) return;
  if (first == windowFirst && count == windowCount && thumbHeight == windowThumbHeight) return;

  // A moved window remaps every slot, so the previous snapshots are wrong.
  if (coverCache) coverCache->invalidate();
  pageCovers.assign(static_cast<size_t>(count), std::string());

  // One pass to see whether anything has to be built, so the popup appears
  // only for a page that is going to be slow. `missing` is what the second pass
  // works from: an empty path means either "no cover yet" or "this book has
  // none", and without the distinction a coverless book would be reparsed on
  // every return to its page.
  bool needsGeneration = false;
  std::string path;
  bool missing[kMaxPageCells] = {};
  const int cells = std::min(count, static_cast<int>(kMaxPageCells));
  for (int i = 0; i < cells; ++i) {
    if (!source->coverPathFor(first + i, path)) continue;
    if (bookthumb::isCached(path, thumbHeight, pageCovers[static_cast<size_t>(i)])) continue;
    missing[i] = true;
    needsGeneration = true;
  }

  if (needsGeneration) {
    GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    for (int i = 0; i < cells; ++i) {
      if (!missing[i]) continue;
      if (!source->coverPathFor(first + i, path)) continue;
      bookthumb::ensure(path, thumbHeight, pageCovers[static_cast<size_t>(i)]);
    }
    // The popup published a frame of its own. The grid drawn after this paints
    // only its cells -- with SpaceBetween columns the gutters are not touched --
    // so without clearing, slices of the popup stay on screen beside the covers.
    renderer.clearScreen();
  }

  windowFirst = first;
  windowCount = count;
  windowThumbHeight = thumbHeight;
}

fui::CoverGridItem LibraryCoverGrid::provideItem(const uint16_t index, void* user) {
  auto* self = static_cast<LibraryCoverGrid*>(user);
  self->labelScratch.clear();
  if (self->source && !self->source->coverLabelFor(static_cast<int>(index), self->labelScratch)) {
    self->labelScratch.clear();
  }
  return fui::coverGridItem(self->labelScratch.c_str(), index);
}

bool LibraryCoverGrid::paintCover(fui::DrawTarget&, const fui::Rect cover, const fui::CoverGridItem&,
                                  const uint16_t index, void* user) {
  auto* self = static_cast<LibraryCoverGrid*>(user);
  const int slot = static_cast<int>(index) - self->windowFirst;
  if (!self->coverCache || slot < 0 || slot >= static_cast<int>(self->pageCovers.size())) return false;
  const std::string& coverBmp = self->pageCovers[static_cast<size_t>(slot)];
  if (coverBmp.empty()) return false;
  return self->coverCache->paint(cover, static_cast<size_t>(slot),
                                 UITheme::getCoverThumbPath(coverBmp, self->windowThumbHeight));
}

int LibraryCoverGrid::build(UiAppHost::UiScreen& screen, const fui::Rect body, const int selectedEntry,
                            const fui::ActionId rowAction) {
  // A negative entry means the tab strip owns the focus; the list view marks no
  // row then, and the shelf should not frame its first cover either.
  const bool hasSelection = selectedEntry >= 0;
  if (!source) return 0;
  const int count = source->coverCount();
  if (count <= 0) return 0;

  if (!coverCache) {
    coverCache = makeUniqueNoThrow<HomeCoverCache>(renderer);
    if (coverCache) coverCache->begin();
  }
  if (coverCache) coverCache->prepare();

  const int thumbHeight = thumbHeightFor(body);
  const int16_t rowHeight = static_cast<int16_t>(rowHeightFor(thumbHeight));
  // The page the selection falls on, from the rect the grid really has. The
  // widget takes its props by const reference and never advances topIndex, so
  // the viewport has to be computed here or the shelf never scrolls.
  // Capped at the cover cache's slot count: a body tall enough for a fourth row
  // would hand paintCover() slots it cannot snapshot, and those cells would
  // silently fall back to placeholder art.
  pageItems = std::min<uint16_t>(fui::coverGridVisibleCells(body, COLUMNS, rowHeight, CELL_GAP),
                                 static_cast<uint16_t>(HomeCoverCache::MAX_COVERS));
  if (pageItems == 0) return 0;
  const uint16_t topIndex = fui::coverGridTopIndexFor(static_cast<uint16_t>(hasSelection ? selectedEntry : 0),
                                                      static_cast<uint16_t>(count), COLUMNS, pageItems);
  const int visible = std::min<int>(count - topIndex, pageItems);
  resolvePage(static_cast<int>(topIndex), visible, thumbHeight);

  fui::CoverGridProps grid;
  grid.count = static_cast<uint16_t>(count);
  grid.topIndex = topIndex;
  grid.selectedIndex = hasSelection ? static_cast<int16_t>(selectedEntry) : static_cast<int16_t>(-1);
  grid.action = rowAction;
  grid.inputMask = fui::InputTouch | fui::InputLongPress;
  grid.columns = COLUMNS;
  grid.columnLayout = fui::CoverGridColumnLayout::SpaceBetween;
  grid.selectionIndicator = fui::CoverGridSelectionIndicator::CoverFrame;
  grid.selectedCoverFrameGap = 4;
  grid.selectedCoverFrameWidth = 6;
  grid.coverSize.height = static_cast<int16_t>(thumbHeight);
  grid.coverSize.width = static_cast<int16_t>(thumbHeight * 2 / 3);
  grid.rowHeight = rowHeight;
  grid.gap = grid.rowGap = CELL_GAP;
  // The title is what makes a wall of art navigable, and the only thing a book
  // whose cover failed to generate has left.
  grid.labelHeight = static_cast<int16_t>(renderer.getLineHeight(SMALL_FONT_ID) * LABEL_LINES);
  grid.labelGap = LABEL_GAP;
  grid.titleText = screen.theme().smallText;
  grid.titleText.maxLines = LABEL_LINES;
  grid.labelFollowsCover = true;
  grid.cellStyles = screen.theme().listRow;
  grid.itemProviderUserData = this;
  grid.itemProvider = &LibraryCoverGrid::provideItem;
  grid.coverPainterUserData = this;
  grid.coverPainter = &LibraryCoverGrid::paintCover;

  fui::coverGrid(screen.frame(), body, grid);
  return static_cast<int>(topIndex);
}
