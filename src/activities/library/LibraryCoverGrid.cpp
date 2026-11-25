#include "LibraryCoverGrid.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "components/CoverTag.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookThumbnail.h"

namespace fui = freeink::ui;

namespace {
constexpr int16_t CELL_GAP = 8;
constexpr int16_t LABEL_GAP = 4;
constexpr uint8_t LABEL_LINES = 2;
constexpr int16_t FRAME_GAP = 4;
constexpr int16_t FRAME_WIDTH = 4;
// Room the selection frame takes around a cover. Reserved inside each cell so
// the frame never runs into the title above it or the tab bar.
constexpr int16_t FRAME_EXTENT = FRAME_GAP + FRAME_WIDTH;
// Between rows: the frame of one row must clear the title of the row above.
constexpr int16_t ROW_GAP = 12;
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
  const int columnWidth = std::max(1, (body.width - CELL_GAP * (COLUMNS - 1)) / COLUMNS);
  const int byWidth = (columnWidth - FRAME_EXTENT * 2) * 3 / 2;
  const int rowHeight = std::max(1, (body.height - ROW_GAP * (ROWS - 1)) / ROWS);
  return std::max(1, std::min(byWidth, rowHeight - (rowHeightFor(0))));
}

int LibraryCoverGrid::rowHeightFor(const int thumbHeight) const {
  return FRAME_EXTENT + thumbHeight + LABEL_GAP + renderer.getLineHeight(SMALL_FONT_ID) * LABEL_LINES;
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
    const Rect popup = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    for (int i = 0; i < cells; ++i) {
      if (!missing[i]) continue;
      if (!source->coverPathFor(first + i, path)) continue;
      bookthumb::ensure(path, thumbHeight, pageCovers[static_cast<size_t>(i)]);
    }
    // The popup published a frame of its own, and the grid paints only its
    // cells, so slices of the popup would stay between the covers. Clear the
    // popup alone: the header and the tab bar are already in the frame, and
    // clearing the whole screen here left the screen without them.
    constexpr int kPopupBleed = 6;
    renderer.fillRect(popup.x - kPopupBleed, popup.y - kPopupBleed, popup.width + kPopupBleed * 2,
                      popup.height + kPopupBleed * 2, false);
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

bool LibraryCoverGrid::paintCover(fui::DrawTarget& target, const fui::Rect cover, const fui::CoverGridItem&,
                                  const uint16_t index, void* user) {
  auto* self = static_cast<LibraryCoverGrid*>(user);
  const int slot = static_cast<int>(index) - self->windowFirst;
  if (!self->coverCache || slot < 0 || slot >= static_cast<int>(self->pageCovers.size())) return false;
  const std::string& coverBmp = self->pageCovers[static_cast<size_t>(slot)];
  if (coverBmp.empty()) return false;
  if (!self->coverCache->paint(cover, static_cast<size_t>(slot),
                               UITheme::getCoverThumbPath(coverBmp, self->windowThumbHeight))) {
    return false;
  }
  if (self->source) drawCoverTag(target, cover, self->source->coverTagFor(static_cast<int>(index)), self->tagText);
  return true;
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
  pageItems = std::min<uint16_t>(fui::coverGridVisibleCells(body, COLUMNS, rowHeight, ROW_GAP),
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
  // Each cover centred in its half, with its title under it at the full
  // column width.
  grid.columnLayout = fui::CoverGridColumnLayout::EqualWidth;
  grid.selectionIndicator = fui::CoverGridSelectionIndicator::CoverFrame;
  grid.selectedCoverFrameGap = FRAME_GAP;
  grid.selectedCoverFrameWidth = FRAME_WIDTH;
  grid.cellInset = fui::Insets{FRAME_EXTENT, 0, 0, 0};
  grid.coverSize.height = static_cast<int16_t>(thumbHeight);
  grid.coverSize.width = static_cast<int16_t>(thumbHeight * 2 / 3);
  grid.rowHeight = rowHeight;
  grid.gap = CELL_GAP;
  grid.rowGap = ROW_GAP;
  // The title is what makes a wall of art navigable, and the only thing a book
  // whose cover failed to generate has left.
  grid.labelHeight = static_cast<int16_t>(renderer.getLineHeight(SMALL_FONT_ID) * LABEL_LINES);
  grid.labelGap = LABEL_GAP;
  grid.titleText = screen.theme().smallText;
  grid.titleText.maxLines = LABEL_LINES;
  grid.labelInset = fui::Insets{0, 4, 0, 4};
  tagText = screen.theme().smallText;
  grid.cellStyles = screen.theme().listRow;
  grid.itemProviderUserData = this;
  grid.itemProvider = &LibraryCoverGrid::provideItem;
  grid.coverPainterUserData = this;
  grid.coverPainter = &LibraryCoverGrid::paintCover;

  fui::coverGrid(screen.frame(), body, grid);
  return static_cast<int>(topIndex);
}
