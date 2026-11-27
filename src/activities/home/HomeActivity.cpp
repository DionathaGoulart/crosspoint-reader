#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>
#include <Memory.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "HapticFeedback.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "activities/stats/StatsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookProgress.h"
#include "util/BookThumbnail.h"
#include "util/BusyIndicator.h"

// The menu order lives in two lists that have to agree: this file's
// (indexToMenuItem, getMenuItemCount) and CoverGridHomeUi's tab icons. They
// disagreed once, and the home then opened Statistics from the transfer icon
// and could not reach Settings at all. Adding an entry below without adding
// its tab icon, or the reverse, now fails to compile here.
static_assert(CoverGridHomeUi::TAB_POSITIONS == kHomeBaseMenuItems + 1,
              "CoverGridHomeUi's tab icons must cover the base menu plus the plugins/OPDS slot");

int HomeActivity::getMenuItemCount() const {
  int count = kHomeBaseMenuItems;
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (slotItem() != HomeMenuItem::NONE) {
    count++;
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(maxBooks);

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::fillCoverGridFromLibrary() {
  if (recentBooks.size() >= static_cast<size_t>(CoverGridHomeUi::MAX_BOOKS)) return;
  // Keep the index and record together off the task stack; reuse for every row.
  struct LibraryReader {
    library::LibraryIndexFile index;
    library::ClixRecord record;
  };
  auto reader = makeUniqueNoThrow<LibraryReader>();
  if (!reader) {
    LOG_ERR("HOME", "OOM: library index");
    return;
  }
  auto& index = reader->index;
  auto& record = reader->record;
  if (!index.open(library::libraryIndexPath())) {
    index.close();
    BusyIndicator busy(renderer, tr(STR_LIBRARY_REBUILDING));
    library::BuildStats stats;
    if (!library::buildLibraryIndex("/", stats, SETTINGS.libraryUseMetadata != 0, BusyIndicator::onProgress, &busy) ||
        !index.open(library::libraryIndexPath())) {
      LOG_ERR("HOME", "Cannot populate cover grid from library");
      return;
    }
  }
  const auto slots = static_cast<size_t>(CoverGridHomeUi::MAX_BOOKS);
  for (uint16_t row = 0; row < index.bookCount() && recentBooks.size() < slots; ++row) {
    RecentBook book;
    if (!index.readRecord(index.ordinalForRow(library::SortOrder::RecentDesc, row), record) ||
        !index.readPath(record, book.path))
      continue;
    if (std::any_of(recentBooks.begin(), recentBooks.end(),
                    [&](const RecentBook& existing) { return existing.path == book.path; }) ||
        RecentBooksStore::isMissing(book))
      continue;
    if (!index.readTitle(record, book.title) && !index.readName(record, book.title)) continue;
    index.readAuthor(record, book.author);
    if (index.ioFailed()) break;
    recentBooks.push_back(std::move(book));
  }
}

void HomeActivity::resolveGridCoverPaths() {
  for (auto& book : recentBooks) {
    if (!book.coverBmpPath.empty()) continue;
    // Constructors only derive cache paths; no metadata parsing or image generation.
    // Keep these large objects off the task stack and release each before the next book.
    if (FsHelpers::hasReflowableBookExtension(book.path)) {
      auto epub = makeUniqueNoThrow<Epub>(book.path, "/.crosspoint");
      if (!epub) {
        LOG_ERR("HOME", "OOM: EPUB thumbnail path");
        continue;
      }
      book.coverBmpPath = epub->getThumbBmpPath();
    } else if (FsHelpers::hasXtcExtension(book.path)) {
      auto xtc = makeUniqueNoThrow<Xtc>(book.path, "/.crosspoint");
      if (!xtc) {
        LOG_ERR("HOME", "OOM: XTC thumbnail path");
        continue;
      }
      book.coverBmpPath = xtc->getThumbBmpPath();
    }
  }
}

void HomeActivity::loadGridCover(RecentBook& book, int height, bool& showingLoading, Rect& popupRect) {
  if (!book.coverBmpPath.empty() && Storage.exists(UITheme::getCoverThumbPath(book.coverBmpPath, height).c_str()))
    return;
  // Probe first so the popup goes up before the slow work, not after it:
  // building a thumbnail opens the book and rescales its cover, and that is
  // exactly the wait the popup exists to explain.
  if (bookthumb::isCached(book.path, height, book.coverBmpPath)) return;
  if (!showingLoading) {
    showingLoading = true;
    popupRect = GUI.drawBusyPopup(renderer, tr(STR_LOADING_POPUP), 2);
    GUI.fillPopupProgress(renderer, popupRect, 0);
  }
  bookthumb::ensure(book.path, height, book.coverBmpPath);
}

void HomeActivity::loadRecentCovers() {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    // One slot size; generating at any other height would rescale the
    // dithered thumb at draw time and alias badly.
    loadGridCover(book, coverGridUi->thumbHeightFor(), showingLoading, popupRect);
    ++progress;
    if (showingLoading) GUI.fillPopupProgress(renderer, popupRect, progress * 100 / recentBooks.size());
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();
  // Plugin System is the global opt-in. Once enabled, expose the Plugins entry
  // even when no SD plugins are installed yet so Plugin Hub can be installed
  // from inside the Plugins screen (or users can sideload plugins manually).
  hasPlugins = SETTINGS.pluginsEnabled != 0;

  // Screen-lifetime interaction tables and component properties exceed the stack budget.
  coverGridUi = makeUniqueNoThrow<CoverGridHomeUi>(renderer);
  if (!coverGridUi) LOG_ERR("HOME", "OOM: home UI");
  loadRecentBooks(CoverGridHomeUi::MAX_BOOKS);
  hasContinueReading = !recentBooks.empty();
  if (coverGridUi) {
    fillCoverGridFromLibrary();
    // Without PSRAM the home draws no covers, so their paths are not needed.
    if (UITheme::homeShowsCover()) resolveGridCoverPaths();
    const HomeMenuItem slot = slotItem();
    coverGridUi->begin(recentBooks,
                       slot == HomeMenuItem::PLUGINS        ? CoverGridHomeUi::SlotTab::PLUGINS
                       : slot == HomeMenuItem::OPDS_BROWSER ? CoverGridHomeUi::SlotTab::OPDS
                                                            : CoverGridHomeUi::SlotTab::NONE,
                       hasContinueReading);
  }

  const auto base = static_cast<int>(recentBooks.size());
  selectorIndex = initialMenuItem == HomeMenuItem::NONE ? 0 : base + menuItemToIndex(initialMenuItem, slotItem());

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  coverGridUi.reset();
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();

  auto activateSelection = [this] {
    if (selectorIndex < recentBooks.size()) {
      onRecentBookSelected(recentBooks[selectorIndex].path);
      return;
    }
    const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());
    switch (indexToMenuItem(menuIndex, slotItem())) {
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
        break;
      case HomeMenuItem::LIBRARY:
        onLibraryOpen();
        break;
      case HomeMenuItem::STATS:
        activityManager.pushActivity(std::make_unique<StatsActivity>(renderer, mappedInput));
        break;
      case HomeMenuItem::PLUGINS:
        onPluginsOpen();
        break;
      case HomeMenuItem::OPDS_BROWSER:
        onOpdsBrowserOpen();
        break;
      case HomeMenuItem::FILE_TRANSFER:
        onFileTransferOpen();
        break;
      case HomeMenuItem::SETTINGS_MENU:
        onSettingsOpen();
        break;
      default:
        break;
    }
  };

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }

  // Back is otherwise unused on the home menu: open the most recently read
  // book directly (recentBooks is most-recent-first and already pruned of
  // files missing from the SD card).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && hasContinueReading && !recentBooks.empty()) {
    onRecentBookSelected(recentBooks[0].path);
    return;
  }

  const int touched = coverGridUi ? coverGridUi->selectedAction(mappedInput) : -1;
  if (touched >= 0 && touched < menuCount) {
    selectorIndex = touched;
    activateSelection();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
    return;
  }
  // selectorIndex is flat: the book first, then the tab items. Front
  // Left/Right walk the tabs; a press while the book is selected jumps into
  // the tabs first. The side page buttons walk the whole row: book, then tabs.
  const int bookCount = static_cast<int>(recentBooks.size());
  const auto cycleBand = [this](const int base, const int count, const int dir) {
    if (count <= 0) return;
    int idx = selectorIndex - base;
    if (idx < 0 || idx >= count) {
      idx = dir > 0 ? 0 : count - 1;
    } else {
      idx = (idx + count + dir) % count;
    }
    selectorIndex = base + idx;
    requestUpdate();
  };
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up},
                                       [&cycleBand, menuCount] { cycleBand(0, menuCount, -1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down},
                                       [&cycleBand, menuCount] { cycleBand(0, menuCount, +1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [&cycleBand, bookCount, menuCount] {
    cycleBand(bookCount, menuCount - bookCount, -1);
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [&cycleBand, bookCount, menuCount] {
    cycleBand(bookCount, menuCount - bookCount, +1);
  });
}

void HomeActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (!coverGridUi) {
    // Out of memory for the home UI: say so; the buttons still work.
    GUI.drawPopup(renderer, tr(STR_MEMORY_ERROR));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }
  const bool covers = UITheme::homeShowsCover();
  const auto drawHome = [&] {
    renderer.clearScreen();
    coverGridUi->setSelection(selectorIndex);
    coverGridUi->renderUi();
    // Front Left/Right walk the tabs, so their hints read Left/Right; the
    // side page buttons (unhinted) walk the book and the tabs.
    const auto labels = mappedInput.mapLabels(hasContinueReading ? tr(STR_RESUME) : "", tr(STR_SELECT),
                                              tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  };
  drawHome();
  // The cover height is recorded during the draw above; a change (first
  // layout pass, orientation switch) means the path must point at that size
  // and a missing thumb must be generated. Resolve the cover before the panel
  // is touched and redraw into the framebuffer, so Home paints once instead of
  // three e-ink refreshes (placeholder, placeholder, cover).
  if (covers) {
    if (coverGridUi->takeThumbHeightChanged()) {
      coverGridUi->refreshCoverPaths();
      recentsLoaded = false;
    }
    if (!recentsLoaded && !recentsLoading) {
      loadRecentCovers();
      coverGridUi->refreshCoverPaths();
      drawHome();
      if (coverGridUi->takeThumbHeightChanged()) {
        coverGridUi->refreshCoverPaths();
        recentsLoaded = false;
        requestUpdate();
      }
    }
  }
  renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  firstRenderDone = true;
}

void HomeActivity::onRecentBookSelected(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onLibraryOpen() { activityManager.goToLibrary(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onPluginsOpen() { activityManager.goToPlugins(); }
