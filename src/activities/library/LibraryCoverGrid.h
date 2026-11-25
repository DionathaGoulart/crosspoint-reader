#pragma once

#include <memory>
#include <string>
#include <vector>

#include "components/HomeCoverCache.h"
#include "components/UiAppHost.h"

class GfxRenderer;

// The library's cover shelf: one page of cover art with titles underneath.
//
// Owns everything the view needs that the list view does not -- the PSRAM
// cover cache, the resolved cover paths for the page on screen, and the
// window those paths describe. The activity keeps none of it, so the cost of
// the view exists only while the view does.
//
// Thumbnail resolution and generation run inside the render pass, with the
// render lock already held, like HomeActivity does. That is deliberate: the
// work touches the shared library index handle, the SD card and the
// framebuffer, all of which the render lock protects.
class LibraryCoverGrid {
 public:
  // What the grid needs to know about the shelf behind it. The activity owns
  // the row mapping (pinned recents, search results, folder order), so the
  // grid asks rather than re-deriving it.
  class Source {
   public:
    virtual ~Source() = default;
    virtual int coverCount() const = 0;
    // Absolute path on the card of the book at this display entry.
    virtual bool coverPathFor(int entry, std::string& out) = 0;
    // The text drawn under the cover.
    virtual bool coverLabelFor(int entry, std::string& out) = 0;
    // A short status drawn on the cover ("Rereading"), or nullptr.
    virtual const char* coverTagFor(int) { return nullptr; }
  };

  // Two by two: covers large enough to recognise, and titles wide enough to
  // read in two lines.
  static constexpr uint8_t COLUMNS = 2;
  static constexpr uint8_t ROWS = 2;
  // A page never exceeds the cover cache's slots; build() caps it there.
  static constexpr uint8_t kMaxPageCells = HomeCoverCache::MAX_COVERS;

  explicit LibraryCoverGrid(GfxRenderer& renderer) : renderer(renderer) {}

  void setSource(Source* newSource) { source = newSource; }

  // Cells the last build() drew, for the caller's page-jump arithmetic.
  uint16_t lastPageItems() const { return pageItems; }

  // Drops the resolved page and every cached snapshot. Call whenever the rows
  // behind the grid change meaning: a sort flip, a tab change, a search, or an
  // index rebuild. Without it the cache repaints the previous order's art into
  // cells whose geometry did not change.
  void invalidate();

  // Draws the shelf into `body`, resolving and generating the covers for the
  // page that holds `selectedEntry` first. Returns the top entry now on
  // screen so the caller can keep its own viewport in step.
  int build(UiAppHost::UiScreen& screen, freeink::ui::Rect body, int selectedEntry, freeink::ui::ActionId rowAction);

 private:
  int thumbHeightFor(freeink::ui::Rect body) const;
  int rowHeightFor(int thumbHeight) const;
  // Resolves `count` covers from `first`, generating any that are missing
  // behind a single popup.
  void resolvePage(int first, int count, int thumbHeight);
  static bool paintCover(freeink::ui::DrawTarget& target, freeink::ui::Rect cover,
                         const freeink::ui::CoverGridItem& item, uint16_t index, void* user);
  static freeink::ui::CoverGridItem provideItem(uint16_t index, void* user);

  GfxRenderer& renderer;
  Source* source = nullptr;
  // Allocated on first use: the PSRAM buffer costs nothing until the reader
  // actually opens this view.
  std::unique_ptr<HomeCoverCache> coverCache;
  // Cover BMP paths for the page on screen, indexed from `windowFirst`.
  std::vector<std::string> pageCovers;
  int windowFirst = -1;
  int windowCount = 0;
  int windowThumbHeight = 0;
  uint16_t pageItems = 0;
  // Scratch for the item provider: the label it returns must outlive the call.
  std::string labelScratch;
  // Style for the cover tags, taken from the screen theme in build().
  freeink::ui::TextStyle tagText{};
};
