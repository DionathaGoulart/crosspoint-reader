#pragma once

#include <LibraryIndexFile.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "LibraryCoverGrid.h"
#include "RecentBooksStore.h"
#include "activities/UiTabListActivity.h"
#include "components/OptionPopup.h"

// One Library screen: every indexed book on the card, shown by recency, title,
// author, or the folder it sits in, as a list of names or a shelf of covers. The Recent shelf orders by file
// modification time (when a book landed on the card) and pins the recently OPENED books from RecentBooksStore on top,
// so active reads and fresh arrivals share one list.
//
// The two-slot row is the whole point rather than a styling choice: the problem
// being solved is "I cannot find my books because I do not know the authors",
// and that is answered by a column the eye can sweep, not by a tidier filename.
//
// Rows render through fui::list on the UiTabListActivity ring (0 = the sort
// strip, 1..N = the books), which is what brings touch to rows and tabs. Titles
// are truncated to one line by the widget — more books on the screen, even if
// half a name is hidden.
//
// Only the visible window of rows is materialized per render (strings and
// ListItems for at most one page). The ordinary shelf therefore keeps one page
// of strings; an active search additionally uses one fallible uint16_t slot per
// indexed book so an allocation failure remains recoverable on the C3.
class LibraryListActivity final : public UiTabListActivity, private LibraryCoverGrid::Source {
 public:
  LibraryListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 protected:
  // --- UiListActivity / UiTabListActivity contract ---------------------------
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
#if FREEINK_CAP_TOUCH
  bool canSwipeDelete(int index) const override { return !groupsCollapsed && index >= 0 && index < listCount(); }
  void swipeDelete(int index) override;
#endif
  int tabCount() const override;
  int activeTab() const override;
  const char* tabLabel(int index) const override;
  freeink::ui::TabIndicator tabIndicator(int index) const override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  void navigateButtons() override;
  // The FreeInkUI header owns both the title and search touch target.
  void drawChrome() override {}
  void drawFooter() override;
  // OptionPopup is a self-contained modal: it owns rendering (and the button
  // hints) whenever it is up.
  void render(RenderLock&& lock) override;

 private:
  // The screen's own actions, after the base's ACTION_ROW / ACTION_TAB.
  static constexpr freeink::ui::ActionId ACTION_SEARCH = ACTION_TAB_USER;
  static constexpr freeink::ui::ActionId ACTION_REBUILD = ACTION_SEARCH + 1;
  static constexpr freeink::ui::ActionId ACTION_BACK = ACTION_REBUILD + 1;

  // Walk the card and write a fresh index. Blocking, with a popup: at ~70 books
  // it is well under a second, and it only runs when the index is missing or the
  // user asks.
  bool rebuildIndex();

  // Input
  void openSelectedBook();
  void openSearch();
  // Shared tail of row activation and the options menu's Open entry.
  void openBookByPath(const std::string& path);
  void promptRebuildIndex();
  void resetAfterRebuild();
  // Recent-row long-press menu: open / remove from recents / delete / rebuild.
  void showRecentBookOptions(int entry);
  void promptRemoveRecentBook(const std::string& path, const std::string& title);
  // Long-press delete owns the gesture where grouping does not apply: the
  // Recent sort, degraded lists, and any active search result.
  bool deleteEligible() const;
  // Resolves the row's path and title, then confirms via promptDeleteBookByPath.
  void promptDeleteBook(int entry);
  void promptDeleteBookByPath(const std::string& path, const std::string& title);
  bool collapseGroups(int bookEntry);
  void expandGroup(int groupEntry);
  void restoreExpandedList();
  void selectTab(int index, bool toggleIfActive);
  void toggleSortDirection();
  // Sub-screens act on button press, so a button still held when we resume must
  // not also act here. Records what to swallow on the next release.
  void swallowHeldReleases();
  // Staged back-out shared by Button::Back and the header back arrow.
  void handleBackAction();
  static void searchActionTrampoline(const freeink::ui::ActionEvent& event, void* user);
  static void rebuildActionTrampoline(const freeink::ui::ActionEvent& event, void* user);
  static void backActionTrampoline(const freeink::ui::ActionEvent& event, void* user);

  // Data
  void applyFilter();
  int bookRowCount() const;
  int rowFor(int entry) const;
  // fileName, when asked for, is the on-card name the row's icon derives from
  // (the display title may come from metadata and carry no extension).
  // folderId, when asked for, is the row's folder in the index. Reported here
  // because this already has the record in hand: deriving it again per row
  // would re-read it, and resolving the path per row would walk the folder
  // section on the render task.
  // `author` may be null, and callers that do not draw one should pass null:
  // resolving it costs two extra index seeks and, past the small-string
  // limit, a heap allocation.
  bool rowTextFor(int entry, std::string& title, std::string* author, std::string* fileName = nullptr,
                  uint16_t* folderId = nullptr);
  uint32_t titleInitialFor(int entry);
  bool buildGroupStarts();
  // Folders tab: display order for every indexed book, grouped by the folder
  // it sits in on the card and title-sorted inside each folder.
  //
  // A counting sort over ClixRecord::folderId, which the index already stores
  // per book, so this needs no new index field and no format bump. One u16 per
  // book (at most 8 KiB at CLIX_MAX_RECORDS) held only while the tab is open.
  bool buildFolderOrder();
  bool folderTabActive() const;
  int groupForBook(int bookEntry) const;
  bool groupable() const;

  // Screen building
  void buildHeader(UiScreen& screen);
  // Cover view: the same rows as the list, drawn as a grid of cover art.
  // Gated on the setting and on PSRAM, since the cover cache lives there.
  bool coverViewActive() const;

  // --- LibraryCoverGrid::Source --------------------------------------------
  // The grid asks for rows through these rather than re-deriving the mapping:
  // a pinned recent, a search hit and a folder row all resolve differently,
  // and rowFor() alone is wrong for the first of them.
  int coverCount() const override;
  bool coverPathFor(int entry, std::string& out) override;
  bool coverLabelFor(int entry, std::string& out) override;
  // Materializes ListItems and their strings for the visible window only.
  void buildRows(UiScreen& screen);
  static void formatInitialHeading(uint32_t initial, std::string& out);
  void formatAuthorHeading(const std::string& author, std::string& out) const;
  void drawPositionReadout() const;
  void drawHoldHelp() const;
  const char* headerTitle() const override;

  // Ring 0 is the strip; the selected BOOK is ring - 1, with the strip keeping
  // row 0 as the working selection exactly as the pre-ring code did.
  int selectedEntry() const;
  bool tabsFocused() const { return ringPos() == 0; }

  // --- pinned recently-opened overlay ---------------------------------------
  // On the unfiltered Recent shelf the RecentBooksStore entries sit on top, in
  // read order; the modification-time list follows with those books skipped.
  // Entries below pinnedCount() are store rows; the rest go through rowFor().
  int pinnedCount() const;
  // Setting: the Recent tab lists opened books only.
  bool openedOnly() const;
  const RecentBook* storeBook(int entry) const;
  // Re-match the store against the index (chunked scan). Call whenever the
  // index or the store changes.
  void resolvePinned();
  // Direction-space translation of the matched rows. Call on sort toggles.
  void refreshOverlap();

  library::LibraryIndexFile index;
  int activeTabIndex = 0;
  library::SortOrder sortOrder = library::SortOrder::RecentDesc;
  // One bit per tab; Recent starts descending (newest first).
  uint8_t descendingTabs = 1u << 0;
  // Set when the walk finished but the sort did not, so the screen can say the
  // order is discovery order rather than silently showing a wrong one.
  bool degraded = false;

  // Rows surviving the current query, as positions in the active sort order.
  // Empty query means no filtering and this owns no allocation, so the ordinary
  // shelf pays nothing proportional to the library for the feature.
  std::string query;
  // The active query, pre-quoted for the header: headerTitle() returns a
  // stable c_str the render task can hold across a build.
  std::string headerSearchTitle;
  std::unique_ptr<uint16_t[]> filtered;
  uint16_t filteredCount = 0;
  bool filterFailed = false;

  // Display order for the Folders tab: one row per book, grouped by folder.
  // At most one u16 per indexed book (8 KiB at CLIX_MAX_RECORDS), held only
  // while that tab is open.
  std::unique_ptr<uint16_t[]> folderRows;
  uint16_t folderRowCount = 0;
  // Set when the folder order could not be allocated, so the tab degrades to
  // the plain title order instead of showing an empty shelf.
  bool folderOrderFailed = false;

  // One start row per group. Grouping is only offered for the sorted <=512-book
  // index, so this fallible allocation is at most 1 KiB and is reused after its
  // first successful allocation.
  std::unique_ptr<uint16_t[]> groupStarts;
  uint16_t groupCapacity = 0;
  uint16_t groupCount = 0;
  bool groupsCollapsed = false;
  freeink::ui::ListNav expandedNav;

  // Visible-window row storage, reused across renders (buildRows). Bounded by
  // the densest page, never by the library. Headings get their own storage:
  // the surname-first inversion must not overwrite the author slot, whose raw
  // value the next row's group comparison reads.
  std::vector<freeink::ui::ListItem> winItems;
  std::vector<std::string> winTitles;
  std::vector<std::string> winAuthors;
  std::vector<std::string> winHeaders;

  // Pinned overlay state: per store entry its RecentAsc row (0xFFFF when the
  // book is not in the index), and the current-direction rows to skip, sorted
  // ascending, so unpinned entries map to sort rows with a <=10-step walk.
  uint16_t pinnedAscRows[RecentBooksStore::MAX_RECENT_BOOKS] = {};
  uint16_t overlapRows[RecentBooksStore::MAX_RECENT_BOOKS] = {};
  uint8_t pinnedTotal = 0;
  uint8_t overlapCount = 0;

  bool lockNextConfirmRelease = false;
  bool lockNextBackRelease = false;

  // Row options modal (Recent long-press menu); owned here so it outlives the
  // touch event that opened it.
  OptionPopup optionPopup;

  // Cover shelf. Holds no PSRAM until the reader opens the view, and dies
  // with the activity.
  LibraryCoverGrid coverGrid{renderer};
};
