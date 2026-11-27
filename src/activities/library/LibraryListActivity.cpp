#include "LibraryListActivity.h"

#include <BoardConfig.h>
#include <Epub.h>
#include <FreeInkUIIcon.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryText.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/reader/ProgressFile.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/icons/headerIcons.h"
#include "components/icons/listIcons.h"
#include "components/icons/search32.h"
#include "fontIds.h"
#include "stats/FinishedBooksStore.h"
#include "stats/ReadingQueueStore.h"
#include "util/BookCacheUtils.h"
#include "util/BookProgress.h"
#include "util/BookThumbnail.h"
#include "util/BusyIndicator.h"

namespace fui = freeink::ui;

namespace {
constexpr int SIDE_PADDING = 12;

// Side clearance for the footer text in the panel's bottom corners.
int bottomCornerClearance() {
  const auto& board = BoardConfig::ACTIVE;
  return board.bottomCornerRadius != 0 ? board.bottomCornerRadius : board.cornerRadius;
}

constexpr unsigned long LONG_PRESS_MS = 1000;

constexpr int RECENT_TAB = 0;
constexpr int TITLE_TAB = 1;
constexpr int QUEUE_TAB = 2;
constexpr int READ_TAB = 3;

constexpr int TAB_SLOTS = READ_TAB + 1;

constexpr bool isDescending(const library::SortOrder order) {
  return order == library::SortOrder::RecentDesc || order == library::SortOrder::TitleDesc ||
         order == library::SortOrder::AuthorDesc;
}

constexpr bool isRecentSort(const library::SortOrder order) {
  return order == library::SortOrder::RecentAsc || order == library::SortOrder::RecentDesc;
}

// The queue and the read shelf keep their own order (when added, when
// finished); they walk the title order only to have a stable base to filter.
constexpr bool isListTab(const int tab) { return tab == QUEUE_TAB || tab == READ_TAB; }

constexpr library::SortOrder orderForTab(const int tab, const uint8_t descendingTabs) {
  const bool descending = (descendingTabs & (1u << tab)) != 0;
  if (tab == TITLE_TAB) return descending ? library::SortOrder::TitleDesc : library::SortOrder::TitleAsc;
  if (isListTab(tab)) return library::SortOrder::TitleAsc;
  return descending ? library::SortOrder::RecentDesc : library::SortOrder::RecentAsc;
}

const char* tabLabelFor(const int tab) {
  if (tab == TITLE_TAB) return tr(STR_LIBRARY_TAB_TITLE);
  if (tab == QUEUE_TAB) return tr(STR_LIBRARY_TAB_QUEUE);
  if (tab == READ_TAB) return tr(STR_LIBRARY_TAB_READ);
  return tr(STR_LIBRARY_TAB_RECENT);
}

}  // namespace

LibraryListActivity::LibraryListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiTabListActivity("Library", renderer, mappedInput, true) {
  // Four short tab labels: a full-slot pill would stretch across a quarter of
  // the screen, so cap it at the label plus padding (slots stay put). Raising
  // this truncates the labels on a 480 px portrait panel; at 6 the
  // Portuguese "Últimos" already lost its last letters.
  tabPillMaxPad = 2;
}

void LibraryListActivity::onEnter() {
  // One lock across the base lifecycle AND the data phase: the base onEnter
  // schedules a paint, and the render task must not read the index or the
  // filter before they are in place. The rebuild also needs the lock: the
  // render task's SD-loaded fonts read glyph data at draw time, and the walk
  // needs the card to itself.
  RenderLock lock(*this);
  UiTabListActivity::onEnter();
  app.on(ACTION_SEARCH, &LibraryListActivity::searchActionTrampoline, this);
  app.on(ACTION_REBUILD, &LibraryListActivity::rebuildActionTrampoline, this);
  app.on(ACTION_BACK, &LibraryListActivity::backActionTrampoline, this);

  // Recent is backed by the resident store. Prune before opening the index so
  // its persistence write never overlaps the long-lived index reader.
  if (RECENT_BOOKS.pruneMissing()) RECENT_BOOKS.saveToFile();

  // Rebuild when the index is missing, invalid, or was built with the other
  // metadata mode. Otherwise entering the screen stays instant.
  const bool readMetadata = SETTINGS.libraryUseMetadata != 0;
  const bool rebuildNeeded = library::isLibraryIndexDirty() || !index.open(library::libraryIndexPath()) ||
                             index.header().metadataEnabled != readMetadata;
  if (rebuildNeeded) {
    index.close();
    rebuildIndex();
    if (!index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot open library index");
  }
  degraded = index.isOpen() && index.ranksDegraded();
  if (index.isOpen() && index.dedupDegraded()) {
    LOG_ERR("LIB", "index was built without duplicate detection");
  }
  // The tabs split the shelf by status, so the finished and queued books are
  // needed before the first row is drawn.
  loadShelfSets();
  resolvePinned();
  applyFilter();

  // Entered while Confirm was still held (typical when launched from the home
  // menu): ignore its release, or we would open whatever sits at row 0.
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate(true);
}

void LibraryListActivity::onExit() {
  index.close();
  Activity::onExit();
}

bool LibraryListActivity::rebuildIndex() {
  library::BuildStats stats;
  BusyIndicator busy(renderer, tr(STR_LIBRARY_REBUILDING));
  const bool ok =
      library::buildLibraryIndex("/", stats, SETTINGS.libraryUseMetadata != 0, BusyIndicator::onProgress, &busy);
  if (!ok) {
    LOG_ERR("LIB", "index build failed");
    return false;
  }
  LOG_INF("LIB", "reconciled: %u unchanged, %u added, %u renamed, %u removed, %u enriched (%u dup, %u unreadable)",
          static_cast<unsigned>(stats.unchanged), static_cast<unsigned>(stats.added),
          static_cast<unsigned>(stats.renamed), static_cast<unsigned>(stats.removed),
          static_cast<unsigned>(stats.enriched), static_cast<unsigned>(stats.duplicatesDropped),
          static_cast<unsigned>(stats.unreadableSkipped));
  if (stats.dedupDegraded) LOG_ERR("LIB", "rebuild completed without duplicate detection");
  return true;
}

void LibraryListActivity::swallowHeldReleases() {
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  lockNextBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
}

int LibraryListActivity::selectedEntry() const {
  const int entry = ringPos() - 1;
  return entry < 0 ? 0 : entry;
}

// The pinned overlay applies only to the shelf that reads as "what am I up
// to": the unfiltered Recent sort, newest first. A search result is a flat
// list the reader narrowed down on purpose, and the ascending toggle asks for
// oldest-first, which pinned fresh reads would contradict.
int LibraryListActivity::pinnedCount() const {
  if (openedOnly()) return pinnedTotal;
  if (activeTabIndex != RECENT_TAB || !query.empty() || collectionKind != Collection::None || !isDescending(sortOrder))
    return 0;
  return pinnedTotal;
}

// With the setting on, the unfiltered Recent tab is the store alone, in either
// direction. Search still covers the whole index.
bool LibraryListActivity::openedOnly() const {
  return SETTINGS.libraryRecentOpenedOnly != 0 && activeTabIndex == RECENT_TAB && query.empty() &&
         collectionKind == Collection::None;
}

// Store book behind a pinned entry; the ascending toggle reads the store backwards.
const RecentBook* LibraryListActivity::storeBook(const int entry) const {
  const auto& books = RECENT_BOOKS.getBooks();
  const int count = static_cast<int>(pinnedTotal);
  if (entry < 0 || entry >= count) return nullptr;
  const size_t store = pinnedStore[isDescending(sortOrder) ? entry : count - 1 - entry];
  return store < books.size() ? &books[store] : nullptr;
}

void LibraryListActivity::resolvePinned() {
  const auto& books = RECENT_BOOKS.getBooks();
  pinnedTotal = 0;
  uint64_t hashes[RecentBooksStore::MAX_RECENT_BOOKS];
  for (size_t i = 0; i < books.size() && pinnedTotal < RecentBooksStore::MAX_RECENT_BOOKS; i++) {
    const std::string& path = books[i].path;
    const uint64_t hash = library::clixPathHash(path.data(), path.size());
    hashes[pinnedTotal] = hash;
    pinnedStore[pinnedTotal++] = static_cast<uint8_t>(i);
  }
  for (int i = 0; i < pinnedTotal; i++) pinnedAscRows[i] = 0xFFFF;
  if (pinnedTotal > 0 && index.isOpen()) {
    library::BookIdentity identities[RecentBooksStore::MAX_RECENT_BOOKS];
    for (int i = 0; i < pinnedTotal; i++) {
      const std::string& path = books[pinnedStore[i]].path;
      identities[i].pathHash = hashes[i];
      // Size is only a lookup prefilter; 0 (stat failed, e.g. the index handle
      // is the card's one open reader) falls back to hash-only matching.
      identities[i].fileSize = 0;
      HalFile file;
      if (Storage.openFileForRead("LIB", path.c_str(), file)) {
        identities[i].fileSize = static_cast<uint32_t>(file.fileSize());
      }
    }
    if (!index.recentRowsFor(identities, pinnedTotal, pinnedAscRows)) {
      // Without the match the overlay would duplicate every pinned book that is
      // also in the index; better to drop the pins than to show doubles.
      LOG_ERR("LIB", "recent-book lookup failed; overlay disabled");
      pinnedTotal = 0;
    }
  }
  refreshOverlap();
}

void LibraryListActivity::refreshOverlap() {
  overlapCount = 0;
  const int total = static_cast<int>(index.bookCount());
  for (int i = 0; i < pinnedTotal; i++) {
    if (pinnedAscRows[i] == 0xFFFF || pinnedAscRows[i] >= total) continue;
    const uint16_t row =
        isDescending(sortOrder) ? static_cast<uint16_t>(total - 1 - pinnedAscRows[i]) : pinnedAscRows[i];
    overlapRows[overlapCount++] = row;
  }
  std::sort(overlapRows, overlapRows + overlapCount);
}

void LibraryListActivity::openSelectedBook() {
  std::string path;
  std::string title;
  if (selectedEntry() < pinnedCount()) {
    const RecentBook* book = storeBook(selectedEntry());
    if (!book) return;
    path = book->path;
    title = book->title;
  } else {
    if (!index.isOpen()) return;
    const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(selectedEntry())));
    if (ordinal == 0xFFFF) return;

    library::ClixRecord record{};
    if (!index.readRecord(ordinal, record) || !index.readPath(record, path)) {
      LOG_ERR("LIB", "cannot resolve path for row %d", selectedEntry());
      return;
    }
  }

  // A book already read opens at its last page, and opening it does not count
  // as another reading. Ask which one is meant, so a reread is a choice made
  // here and not only through the options menu. Open is first, so a stray
  // press never discards the saved place.
  if (tagForHash(library::clixPathHash(path.data(), path.size())) != shelfstatus::Tag::Read) {
    openBookByPath(path);
    return;
  }
  if (title.empty() && selectedEntry() >= pinnedCount() && !rowTextFor(selectedEntry(), title, nullptr)) title.clear();
  const char* options[] = {tr(STR_OPEN), tr(STR_BOOK_REREAD)};
  app.clearTapFlash();
  optionPopup.show(tr(STR_LIBRARY), title.c_str(), options, 2, 0, [this, path, title](const int choice) {
    swallowHeldReleases();
    if (choice == 0) {
      openBookByPath(path);
    } else if (choice == 1) {
      startReread(path, title);
    }
  });
}

// Shared by row activation and the options menu: the reader screen this opens
// has its own surfaces; a lingering tap flash would gray an unrelated element
// there. The index handle is released first — on hardware only one reader can
// hold a file open at a time, and the reader is about to open files of its own.
void LibraryListActivity::openBookByPath(const std::string& path) {
  app.clearTapFlash();
  index.close();
  onSelectBook(path);
}

void LibraryListActivity::activateIndex(const int index) {
  if (groupsCollapsed) {
    expandGroup(index);
  } else {
    openSelectedBook();
  }
}

// Row long-press opens the row options menu on every tab; a collapsed group
// list has no book to act on, so there it expands the group instead.
void LibraryListActivity::onRowLongPress(const int index) {
  if (groupsCollapsed) {
    activateIndex(index);
  } else {
    showBookOptions(index);
  }
}

// Row options menu (button hold and touch long-press). The first rows of the
// Recent shelf may come from RecentBooksStore; only those can be removed from
// recents.
void LibraryListActivity::showBookOptions(const int entry) {
  if (entry < 0 || entry >= listCount()) return;

  std::string path;
  std::string title;
  const bool isStoreRow = entry < pinnedCount();
  if (isStoreRow) {
    const RecentBook* book = storeBook(entry);
    if (!book) return;
    path = book->path;
    title = book->title;
  } else {
    if (!index.isOpen()) return;
    const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
    library::ClixRecord record{};
    if (ordinal == 0xFFFF || !index.readRecord(ordinal, record) || !index.readPath(record, path) ||
        !rowTextFor(entry, title, nullptr)) {
      LOG_ERR("LIB", "cannot resolve row %d", entry);
      return;
    }
  }

  // Labels and actions are built as one pair of lists rather than a switch over
  // indices that shift with the row type. Which entries apply depends on both
  // the row (only a recent can be un-pinned) and the book (only one that has
  // been read can be reread), and an index switch over that is the same
  // "two lists must agree by counting" hazard that put the wrong destination
  // behind the home screen's icons.
  enum Action : uint8_t {
    Open,
    Queue,
    Unqueue,
    Reread,
    Like,
    Dislike,
    Group,
    BrowseSeries,
    BrowseGenre,
    Unpin,
    Delete,
    Rebuild
  };
  std::vector<const char*> options;
  std::vector<Action> actions;
  // Reserved: eleven is the most this can hold, and growing by doubling would
  // be four allocations each on a button press.
  options.reserve(11);
  actions.reserve(11);
  const auto add = [&options, &actions](const char* label, const Action action) {
    options.push_back(label);
    actions.push_back(action);
  };

  FINISHED_BOOKS.ensureLoaded();
  const std::string document = finishedbooks::documentIdFor(path);
  const FinishedBook* record = document.empty() ? nullptr : FINISHED_BOOKS.find(document);
  const bool hasBeenRead = record != nullptr && record->timesRead > 0;
  const int8_t rating = record != nullptr ? record->rating : 0;
  const bool queued = READING_QUEUE.contains(path);
  const shelfstatus::Tag status = tagForHash(library::clixPathHash(path.data(), path.size()));

  add(tr(STR_OPEN), Open);
  // A read book may be queued to read again. One being reread is not waiting.
  if (queued) {
    add(tr(STR_QUEUE_REMOVE), Unqueue);
  } else if (status != shelfstatus::Tag::Reading && status != shelfstatus::Tag::Rereading) {
    add(tr(STR_QUEUE_ADD), Queue);
  }
  // Rereading is only offered for a book already finished: on anything else it
  // would mean nothing, and the counter it arms would never fire.
  if (hasBeenRead) add(tr(STR_BOOK_REREAD), Reread);
  // Each rating entry toggles, so the same press that sets it clears it. The
  // label says which way it will go.
  add(rating > 0 ? tr(STR_BOOK_UNLIKE) : tr(STR_BOOK_LIKE), Like);
  add(rating < 0 ? tr(STR_BOOK_UNDISLIKE) : tr(STR_BOOK_DISLIKE), Dislike);
  if (groupable()) add(tr(STR_LIBRARY_GROUP_BY_LETTER), Group);
  if (collectionKind == Collection::None && !degraded) {
    add(tr(STR_LIBRARY_BY_SERIES), BrowseSeries);
    add(tr(STR_LIBRARY_BY_GENRE), BrowseGenre);
  }
  if (isStoreRow) add(tr(STR_REMOVE_FROM_RECENTS), Unpin);
  add(tr(STR_DELETE), Delete);
  add(tr(STR_LIBRARY_REBUILD), Rebuild);

  app.clearTapFlash();
  optionPopup.show(tr(STR_LIBRARY), title.c_str(), options.data(), static_cast<int>(options.size()), 0,
                   [this, entry, path, title, actions, document, rating](const int choice) {
                     swallowHeldReleases();
                     if (choice < 0 || choice >= static_cast<int>(actions.size())) return;
                     switch (actions[static_cast<size_t>(choice)]) {
                       case Open:
                         openBookByPath(path);
                         break;
                       case Queue:
                         if (READING_QUEUE.add(path)) refreshShelf();
                         break;
                       case Unqueue:
                         if (READING_QUEUE.remove(path)) refreshShelf();
                         break;
                       case Reread:
                         startReread(path, title);
                         break;
                       case Like:
                         setBookRating(path, title, document, rating > 0 ? 0 : 1);
                         break;
                       case Dislike:
                         setBookRating(path, title, document, rating < 0 ? 0 : -1);
                         break;
                       case Group:
                         collapseGroups(entry);
                         break;
                       case BrowseSeries:
                         openCollectionPicker(Collection::Series);
                         break;
                       case BrowseGenre:
                         openCollectionPicker(Collection::Genre);
                         break;
                       case Unpin:
                         promptRemoveRecentBook(path, title);
                         break;
                       case Delete:
                         promptDeleteBookByPath(path, title);
                         break;
                       case Rebuild:
                         promptRebuildIndex();
                         break;
                     }
                   });
  requestUpdate();
}

// True when the book belongs to the picked series or genre. The record's
// folded series prefix rules out most books without an SD read.
bool LibraryListActivity::inCollection(const library::ClixRecord& record) {
  std::string value;
  if (collectionKind == Collection::Series) {
    if (record.seriesKeyLen == 0) return false;
    const size_t prefix = std::min<size_t>(record.seriesKeyLen, collectionFold.size());
    if (memcmp(record.seriesKey, collectionFold.data(), prefix) != 0) return false;
    return index.readSeries(record, value) && library::fold(value) == collectionFold;
  }
  if (record.genreHash != library::genreHashOf(collectionFold)) return false;
  return index.readGenre(record, value) && library::fold(value) == collectionFold;
}

// One pass over the index in title order, counting books per series or genre.
// Capped so a card full of one-off subjects cannot grow the list without bound.
void LibraryListActivity::openCollectionPicker(const Collection kind) {
  struct Entry {
    std::string fold;
    std::string name;
    uint16_t count;
  };
  constexpr size_t MAX_COLLECTIONS = 128;
  std::vector<Entry> entries;
  entries.reserve(32);
  const int total = static_cast<int>(index.bookCount());
  std::string value;
  // The render task reads the same index file: scan under the lock.
  RenderLock scanLock(*this);
  for (int ordinal = 0; ordinal < total; ordinal++) {
    if ((ordinal & 0x3F) == 0x3F) delay(1);
    library::ClixRecord record{};
    if (!index.readRecord(static_cast<uint16_t>(ordinal), record)) continue;
    if (kind == Collection::Series && record.seriesKeyLen == 0) continue;
    const bool found = kind == Collection::Series ? index.readSeries(record, value) : index.readGenre(record, value);
    if (!found) continue;
    const std::string key = library::fold(value);
    if (key.empty()) continue;
    // Newest entry first: the books of a series are usually read together.
    Entry* hit = nullptr;
    for (size_t i = entries.size(); i-- > 0;) {
      if (entries[i].fold == key) {
        hit = &entries[i];
        break;
      }
    }
    if (hit) {
      hit->count++;
    } else if (entries.size() < MAX_COLLECTIONS) {
      entries.push_back(Entry{key, value, 1});
    }
  }
  const char* title = kind == Collection::Series ? tr(STR_LIBRARY_BY_SERIES) : tr(STR_LIBRARY_BY_GENRE);
  if (entries.empty()) {
    const char* ok[] = {tr(STR_OK_BUTTON)};
    optionPopup.showMessage(title, tr(STR_LIBRARY_NO_COLLECTIONS), ok, 1, 0, [](int) {});
    scanLock.unlock();
    requestUpdate();
    return;
  }
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.fold < b.fold; });
  std::vector<std::string> labels;
  labels.reserve(entries.size());
  for (const Entry& e : entries) labels.push_back(e.name + " (" + std::to_string(e.count) + ")");
  auto picked = std::make_shared<std::vector<Entry>>(std::move(entries));
  optionPopup.show(kind == Collection::Series ? StrId::STR_LIBRARY_BY_SERIES : StrId::STR_LIBRARY_BY_GENRE,
                   std::move(labels), 0, [this, kind, picked](const int choice) {
                     swallowHeldReleases();
                     if (choice < 0 || choice >= static_cast<int>(picked->size())) return;
                     {
                       RenderLock lock(*this);
                       collectionKind = kind;
                       collectionName = (*picked)[static_cast<size_t>(choice)].name;
                       collectionFold = (*picked)[static_cast<size_t>(choice)].fold;
                       applyFilter();
                     }
                     auto& nav = activeNav();
                     nav.selected = 0;
                     nav.top = 0;
                     requestUpdate();
                   });
  scanLock.unlock();
  requestUpdate();
}

int LibraryListActivity::ShelfSet::find(const uint64_t hash) const {
  if (!entries || count == 0) return -1;
  const ShelfEntry* begin = entries.get();
  const ShelfEntry* end = begin + count;
  const ShelfEntry* it =
      std::lower_bound(begin, end, hash, [](const ShelfEntry& e, const uint64_t h) { return e.hash < h; });
  return it != end && it->hash == hash ? it->rank : -1;
}

void LibraryListActivity::loadShelfSets() {
  FINISHED_BOOKS.ensureLoaded();
  READING_QUEUE.ensureLoaded();
  const auto& finished = FINISHED_BOOKS.getBooks();
  const auto& queue = READING_QUEUE.getBooks();

  // Sized to the stores (at most 500 + 200 entries, 16 bytes each), held only
  // while the library is open.
  const auto fill = [](ShelfSet& set, const size_t capacity, auto&& each) {
    set.entries.reset();
    set.count = 0;
    if (capacity == 0) return;
    set.entries = makeUniqueNoThrow<ShelfEntry[]>(capacity);
    if (!set.entries) {
      LOG_ERR("LIB", "OOM: shelf set (%u books)", static_cast<unsigned>(capacity));
      return;
    }
    each([&set](const std::string& path, const uint16_t rank) {
      set.entries[set.count++] = ShelfEntry{library::clixPathHash(path.data(), path.size()), rank};
    });
    std::sort(set.entries.get(), set.entries.get() + set.count,
              [](const ShelfEntry& a, const ShelfEntry& b) { return a.hash < b.hash; });
  };

  size_t readCount = 0;
  size_t rereadCount = 0;
  for (const FinishedBook& book : finished) {
    if (book.path.empty()) continue;
    if (book.rereading) ++rereadCount;
    if (book.timesRead > 0) ++readCount;
  }
  // getBooks() is newest first, so the rank orders the Read tab by finish.
  fill(readSet, readCount, [&finished](auto&& put) {
    uint16_t rank = 0;
    for (const FinishedBook& book : finished) {
      if (!book.path.empty() && book.timesRead > 0) put(book.path, rank++);
    }
  });
  fill(rereadSet, rereadCount, [&finished](auto&& put) {
    uint16_t rank = 0;
    for (const FinishedBook& book : finished) {
      if (!book.path.empty() && book.rereading) put(book.path, rank++);
    }
  });
  const auto& recent = RECENT_BOOKS.getBooks();
  fill(readingSet, recent.size(), [&recent](auto&& put) {
    uint16_t rank = 0;
    for (const RecentBook& book : recent) {
      if (!book.path.empty()) put(book.path, rank++);
    }
  });
  fill(queueSet, queue.size(), [&queue](auto&& put) {
    uint16_t rank = 0;
    for (const QueuedBook& book : queue) put(book.path, rank++);
  });
}

shelfstatus::Tag LibraryListActivity::tagForHash(const uint64_t hash) const {
  return shelfstatus::classify(rereadSet.find(hash) >= 0, readSet.find(hash) >= 0, readingSet.find(hash) >= 0,
                               queueSet.find(hash) >= 0);
}

bool LibraryListActivity::anyShelfStatus() const {
  return readSet.count + rereadSet.count + readingSet.count + queueSet.count > 0;
}

void LibraryListActivity::refreshShelf() {
  {
    // The render task reads the sets, the row array and the index file.
    RenderLock lock(*this);
    loadShelfSets();
    resolvePinned();
    applyFilter();
  }
  closeRouting();
  auto& nav = activeNav();
  const int count = listCount();
  if (count == 0) {
    nav.selected = 0;
  } else if (nav.selected > count) {
    nav.selected = count;
  }
  nav.followOnBuild = true;
  requestUpdate();
}

bool LibraryListActivity::pathForEntry(const int entry, std::string& path) {
  if (entry < pinnedCount()) {
    const RecentBook* book = storeBook(entry);
    if (!book) return false;
    path = book->path;
    return true;
  }
  const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
  library::ClixRecord record{};
  return ordinal != 0xFFFF && index.readRecord(ordinal, record) && index.readPath(record, path);
}

int LibraryListActivity::cachedProgress(const std::string& path) {
  for (const auto& entry : progressCache) {
    if (entry.first == path) return entry.second;
  }
  // Loading opens the book, so only a few per screen: the rest fill in on a
  // later draw instead of stalling this one.
  if (progressLoadsLeft <= 0) return -2;
  progressLoadsLeft--;
  constexpr size_t MAX_CACHED = 24;
  if (progressCache.empty()) progressCache.reserve(MAX_CACHED);
  if (progressCache.size() >= MAX_CACHED) progressCache.clear();
  const int percent = loadBookProgress(path);
  progressCache.emplace_back(path, percent);
  return percent;
}

const char* LibraryListActivity::visibleTagLabel(const shelfstatus::Tag tag) const {
  if (tag == shelfstatus::Tag::Queued && activeTabIndex == QUEUE_TAB) return nullptr;
  if (tag == shelfstatus::Tag::Read && activeTabIndex == READ_TAB) return nullptr;
  return shelfstatus::label(tag);
}

#if FREEINK_CAP_TOUCH
void LibraryListActivity::swipeDelete(const int entry) {
  activeNav().selected = entry + 1;
  if (entry < pinnedCount()) {
    const RecentBook* book = storeBook(entry);
    if (book) promptDeleteBookByPath(book->path, book->title);
  } else {
    promptDeleteBook(entry);
  }
}
#endif

// Manual index refresh, same card discipline as the onEnter rebuild: the walk
// wants the card to itself, and the render task must not read the index (or
// the filter) around it.
void LibraryListActivity::promptRebuildIndex() {
  RenderLock lock(*this);
  index.close();
  rebuildIndex();
  if (!index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot open library index");
  resetAfterRebuild();
  requestUpdate(true);
}

void LibraryListActivity::resetAfterRebuild() {
  // Sort positions, group starts and pinned rows all point into the old
  // order. applyFilter() drops the cover page with them.
  loadShelfSets();
  resolvePinned();
  applyFilter();
  auto& nav = activeNav();
  const int count = listCount();
  if (count == 0) {
    nav.selected = 0;
  } else if (nav.selected > count) {
    nav.selected = count;
  }
  nav.followOnBuild = true;
}

void LibraryListActivity::promptRemoveRecentBook(const std::string& path, const std::string& title) {
  const bool reopenIndex = index.isOpen();
  index.close();
  auto confirmation =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_REMOVE_FROM_RECENTS), title);
  if (!confirmation) {
    LOG_ERR("LIB", "OOM: recent removal confirmation");
    if (reopenIndex && !index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot reopen library index");
    return;
  }

  startActivityForResult(std::move(confirmation), [this, path, reopenIndex](const ActivityResult& result) {
    swallowHeldReleases();
    if (reopenIndex && !index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot reopen library index");
    if (!result.isCancelled && RECENT_BOOKS.removeByPath(path)) {
      // Entries below the removed pin all shift up, so the resolved cover page
      // no longer describes the rows it was built for.
      {
        RenderLock lock(*this);
        resolvePinned();
        applyFilter();
      }
      closeRouting();
      auto& nav = activeNav();
      const int count = listCount();
      if (count == 0) {
        nav.selected = 0;
      } else if (nav.selected > count) {
        nav.selected = count;
      }
      nav.followOnBuild = true;
    }
  });
}

void LibraryListActivity::promptDeleteBook(const int entry) {
  if (!index.isOpen() || entry < 0 || entry >= bookRowCount()) return;
  const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
  if (ordinal == 0xFFFF) return;

  std::string path;
  library::ClixRecord record{};
  if (!index.readRecord(ordinal, record) || !index.readPath(record, path)) {
    LOG_ERR("LIB", "cannot resolve path for row %d", entry);
    return;
  }
  std::string title;
  rowTextFor(entry, title, nullptr);
  promptDeleteBookByPath(path, title);
}

void LibraryListActivity::promptDeleteBookByPath(const std::string& path, const std::string& title) {
  // The dialog and the delete both want the card; reopen when we resume.
  index.close();
  auto confirmation =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_ITEM_CONFIRM), title);
  if (!confirmation) {
    LOG_ERR("LIB", "OOM: delete confirmation");
    if (!index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot reopen library index");
    return;
  }

  startActivityForResult(std::move(confirmation), [this, path](const ActivityResult& result) {
    swallowHeldReleases();
    {
      // Same lock rationale as onEnter: the walk wants the card to itself, and
      // the render task must not read the index (or the filter) around the
      // rebuild.
      RenderLock lock(*this);
      if (!result.isCancelled) {
        LOG_DBG("LIB", "deleting %s", path.c_str());
        if (!removeBookFile(path)) LOG_ERR("LIB", "cannot delete %s", path.c_str());
        RECENT_BOOKS.removeByPath(path);  // saves on removal
        rebuildIndex();
      }
      if (!index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot reopen library index");
      if (!result.isCancelled) {
        resetAfterRebuild();
      }
    }
    if (!result.isCancelled) {
      closeRouting();
      requestUpdate(true);
    }
  });
}

void LibraryListActivity::openSearch() {
  app.clearTapFlash();
  // No key filtering here on purpose. Greying out the letters that lead nowhere
  // was built, tested on device and removed: a letter you can see but cannot
  // reach reads as a broken keyboard, and the eye keeps returning to it.
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_LIBRARY_SEARCH), query, 48,
                                                           InputType::Text);
  if (!keyboard) {
    LOG_ERR("LIB", "OOM: search keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    swallowHeldReleases();
    if (result.isCancelled) return;
    {
      RenderLock lock(*this);
      query = std::get<KeyboardResult>(result.data).text;
      applyFilter();
    }
    auto& nav = activeNav();
    if (!query.empty() && filteredCount == 0 && !degraded) {
      // Up from the tab bar reopens Search even with no results.
      nav.selected = 0;
    } else {
      // A non-empty result belongs to the list: land on
      // its first surviving row, not on the strip.
      nav.selected = 1;
    }
    nav.top = 0;
    requestUpdate();
  });
}

void LibraryListActivity::stepTab(const int direction) {
  const int next = (activeTab() + (direction > 0 ? 1 : TAB_SLOTS - 1)) % TAB_SLOTS;
  selectTab(next, false);
}

void LibraryListActivity::onTabAction(const int index) {
  app.clearTapFlash();
  selectTab(index, true);
}

void LibraryListActivity::selectTab(const int index, const bool toggleIfActive) {
#if FREEINK_CAP_TOUCH
  closeSwipeDelete();
#endif
  if (index < 0 || index >= TAB_SLOTS) return;
  if (toggleIfActive && index == activeTab()) descendingTabs ^= static_cast<uint8_t>(1u << index);
  {
    // The render task reads the row array and the index file.
    RenderLock lock(*this);
    sortOrder = orderForTab(index, descendingTabs);
    activeTabIndex = index;
    // The filter and the overlap rows hold positions in the old order, so they
    // must be rebuilt, the overlap first: the filter skips those rows.
    refreshOverlap();
    applyFilter();
  }
  // Tab changes happen only while the bar owns focus. A tab's remembered row
  // must not pull focus back into the list after the switch.
  auto& nav = activeNav();
  nav.selected = 0;
  nav.top = 0;
  requestUpdate();
}

void LibraryListActivity::toggleSortDirection() { selectTab(activeTab(), true); }

int LibraryListActivity::tabCount() const { return TAB_SLOTS; }

int LibraryListActivity::activeTab() const { return activeTabIndex; }

const char* LibraryListActivity::tabLabel(const int index) const { return tabLabelFor(index); }

fui::TabIndicator LibraryListActivity::tabIndicator(const int index) const {
  if (index != activeTab()) return fui::TabIndicator::None;
  const bool descending = isListTab(index) ? (descendingTabs & (1u << index)) != 0 : isDescending(sortOrder);
  return descending ? fui::TabIndicator::Down : fui::TabIndicator::Up;
}

int LibraryListActivity::bookRowCount() const {
  if (openedOnly()) return pinnedTotal;
  const int pinned = pinnedCount();
  // Pinned books already in the index are skipped below the pins, not
  // doubled; pinned books the index missed still show.
  if (identityRows) return static_cast<int>(index.bookCount()) + (pinned > 0 ? pinned - overlapCount : 0);
  return pinned + static_cast<int>(filteredCount);
}

int LibraryListActivity::listCount() const { return groupsCollapsed ? static_cast<int>(groupCount) : bookRowCount(); }

// Entry position on screen to row position in the sort order. Entries below
// pinnedCount() belong to the store and must not reach this.
int LibraryListActivity::rowFor(const int entry) const {
  if (identityRows) {
    // No filtering: the sort order itself, walking past the pinned books' own
    // rows, so the plain shelf costs nothing proportional to the library.
    int row = entry - pinnedCount();
    for (int i = 0; i < overlapCount; i++) {
      if (overlapRows[i] <= row) row++;
    }
    return row;
  }
  const int row = entry - pinnedCount();
  if (row < 0 || row >= static_cast<int>(filteredCount) || !filtered) return 0;
  return filtered[row];
}

bool LibraryListActivity::groupable() const {
  // Only the title shelf is in letter order; the others keep their own.
  return !degraded && activeTabIndex == TITLE_TAB && bookRowCount() > 0;
}

uint32_t LibraryListActivity::titleInitialFor(const int entry) {
  const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
  library::ClixRecord record{};
  if (ordinal == 0xFFFF || !index.readRecord(ordinal, record)) return 0;
  // A saga sits under its series name, so its letter is the series' letter.
  if (record.seriesKeyLen > 0)
    return library::foldedGroupInitial(std::string_view(record.seriesKey, record.seriesKeyLen));
  return library::foldedGroupInitial(std::string_view(record.fold, record.foldLen));
}

bool LibraryListActivity::buildGroupStarts() {
  const int count = bookRowCount();
  if (count <= 0) return false;
  if (groupCapacity < count) {
    auto starts = makeUniqueNoThrow<uint16_t[]>(static_cast<size_t>(count));
    if (!starts) {
      LOG_ERR("LIB", "cannot allocate %u-byte group map", static_cast<unsigned>(count * sizeof(uint16_t)));
      return false;
    }
    groupStarts = std::move(starts);
    groupCapacity = static_cast<uint16_t>(count);
  }

  groupCount = 0;
  uint32_t previousInitial = 0;
  for (int entry = 0; entry < count; entry++) {
    const uint32_t initial = titleInitialFor(entry);
    const bool startsGroup = entry == 0 || initial != previousInitial;
    previousInitial = initial;
    if (startsGroup) groupStarts[groupCount++] = static_cast<uint16_t>(entry);
  }
  LOG_DBG("LIB", "group map: %u groups, %u bytes", static_cast<unsigned>(groupCount),
          static_cast<unsigned>(groupCapacity * sizeof(uint16_t)));
  return groupCount > 0;
}

int LibraryListActivity::groupForBook(const int bookEntry) const {
  int group = 0;
  while (group + 1 < groupCount && groupStarts[group + 1] <= bookEntry) group++;
  return group;
}

bool LibraryListActivity::collapseGroups(const int bookEntry) {
  if (!groupable() || !buildGroupStarts()) return false;
  expandedNav = activeNav();
  groupsCollapsed = true;
  auto& nav = activeNav();
  nav.reset(groupForBook(bookEntry) + 1);
  requestUpdate();
  return true;
}

void LibraryListActivity::expandGroup(const int groupEntry) {
  if (!groupsCollapsed || groupEntry < 0 || groupEntry >= groupCount) return;
  const int bookEntry = groupStarts[groupEntry];
  groupsCollapsed = false;
  activeNav() = expandedNav;
  auto& nav = activeNav();
  nav.selected = bookEntry + 1;
  nav.top = bookEntry;
  nav.followOnBuild = true;
  requestUpdate();
}

void LibraryListActivity::restoreExpandedList() {
  if (!groupsCollapsed) return;
  groupsCollapsed = false;
  activeNav() = expandedNav;
  requestUpdate();
}

// One pass over the sort order, keeping the rows the active tab shows. No
// index, no cache: at the 4096-book format cap this is 4096 record reads and
// comparisons of at most 96 bytes. The result array is allocated once with the
// exact upper bound and fails back to an explicit message rather than letting
// vector growth abort the firmware.
void LibraryListActivity::applyFilter() {
  // Every caller changes which book a row maps to, and the cover cache keys
  // its snapshots by slot and rect rather than by book.
  coverGrid.invalidate();
  groupsCollapsed = false;
  groupCount = 0;
  filtered.reset();
  filteredCount = 0;
  filterFailed = false;
  identityRows = false;
  // The header shows the active query in place of the screen title, so the
  // reader can see what narrowed the list without reopening the keyboard.
  headerSearchTitle = collectionName;
  if (!query.empty()) headerSearchTitle += (headerSearchTitle.empty() ? "" : " ") + ("“" + query + "”");
  if (openedOnly()) return;
  // Recent and Name with nothing to hide and nothing typed are the sort order
  // itself: no allocation and no pass over the index.
  if (query.empty() && collectionKind == Collection::None && !isListTab(activeTabIndex) &&
      (activeTabIndex != TITLE_TAB || readSet.count == 0)) {
    identityRows = true;
    return;
  }

  const int total = static_cast<int>(index.bookCount());
  if (total <= 0) return;

  auto matches = makeUniqueNoThrow<uint16_t[]>(static_cast<size_t>(total));
  if (!matches) {
    LOG_ERR("LIB", "cannot allocate %u-byte row buffer", static_cast<unsigned>(total * sizeof(uint16_t)));
    filterFailed = true;
    return;
  }

  const std::string needle = query.empty() ? std::string() : library::fold(query);
  // The pinned books are drawn above the list; overlapRows is sorted, so one
  // cursor walks it alongside the rows.
  const bool skipPinned = pinnedCount() > 0;
  int overlapCursor = 0;
  // The path hash costs a read per row, so it is read only when a row can be
  // hidden by its status. A search covers every book, read ones included.
  // Name hides finished books; Recent shows every book; a search covers all.
  // A picked series or genre shows all its books, read or not, like a search.
  const bool inCollectionView = collectionKind != Collection::None;
  const bool hidesFinished = activeTabIndex == TITLE_TAB && needle.empty() && readSet.count > 0 && !inCollectionView;
  const bool needsStatus = (isListTab(activeTabIndex) && !inCollectionView) || hidesFinished;
  const ShelfSet* rankSet = inCollectionView              ? nullptr
                            : activeTabIndex == QUEUE_TAB ? &queueSet
                            : activeTabIndex == READ_TAB  ? &readSet
                                                          : nullptr;

  uint16_t matchCount = 0;
  std::string author;
  for (int row = 0; row < total; row++) {
    // Let the idle task run on a large card: this pass reads the SD per row.
    if ((row & 0x3F) == 0x3F) delay(1);
    if (skipPinned) {
      while (overlapCursor < overlapCount && overlapRows[overlapCursor] < row) overlapCursor++;
      if (overlapCursor < overlapCount && overlapRows[overlapCursor] == row) continue;
    }
    const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(row));
    library::ClixRecord record{};
    if (ordinal == 0xFFFF || !index.readRecord(ordinal, record)) continue;

    if (needsStatus) {
      uint64_t hash = 0;
      const shelfstatus::Tag tag = index.readPathHash(record, hash) ? tagForHash(hash) : shelfstatus::Tag::None;
      const bool finished = tag == shelfstatus::Tag::Read || tag == shelfstatus::Tag::Rereading;
      if (activeTabIndex == READ_TAB) {
        // Read and rereading books.
        if (!finished) continue;
      } else if (activeTabIndex == QUEUE_TAB) {
        // A read book may be queued to read again; one already being read or
        // reread is not waiting.
        if (queueSet.find(hash) < 0 || tag == shelfstatus::Tag::Rereading || tag == shelfstatus::Tag::Reading) continue;
      } else if (finished) {
        // Name leaves out finished and rereading books.
        continue;
      }
    }

    if (collectionKind != Collection::None && !inCollection(record)) continue;

    if (!needle.empty() && !library::matchesQuery(std::string_view(record.fold, record.foldLen), needle)) {
      // The stored fold covers the title only, so the author has to be read and
      // folded here. That is the search most worth having: the reader who knows
      // the author usually also knows where the book is, while "emily" finding
      // Alice Hunter is the case the shelf exists to answer.
      author.clear();
      if (!index.readAuthor(record, author) || !library::matchesQuery(library::fold(author), needle)) continue;
    }
    matches[matchCount++] = static_cast<uint16_t>(row);
  }

  if (rankSet && matchCount > 1) {
    // The queue reads in the order books were added, the read shelf newest
    // finished first; the tab toggle reverses either. One more hash read per
    // kept row, bounded by the queue and finished-book caps.
    const bool reversed = (descendingTabs & (1u << activeTabIndex)) != 0;
    const auto rankOf = [this, rankSet](const uint16_t row) {
      const uint16_t ordinal = index.ordinalForRow(sortOrder, row);
      library::ClixRecord record{};
      uint64_t hash = 0;
      if (ordinal == 0xFFFF || !index.readRecord(ordinal, record) || !index.readPathHash(record, hash)) return 0xFFFF;
      const int rank = rankSet->find(hash);
      return rank < 0 ? 0xFFFF : rank;
    };
    // At most the queue cap or the finished cap: a small, fallible side table
    // so the sort does not re-read the index on every comparison.
    auto ranks = makeUniqueNoThrow<uint32_t[]>(matchCount);
    if (ranks) {
      for (uint16_t i = 0; i < matchCount; i++) {
        ranks[i] = (static_cast<uint32_t>(rankOf(matches[i])) << 16) | matches[i];
      }
      std::sort(ranks.get(), ranks.get() + matchCount);
      for (uint16_t i = 0; i < matchCount; i++) {
        matches[i] = static_cast<uint16_t>(ranks[reversed ? matchCount - 1 - i : i] & 0xFFFF);
      }
    } else {
      LOG_ERR("LIB", "no memory to order %u rows; keeping title order", static_cast<unsigned>(matchCount));
    }
  }
  filtered = std::move(matches);
  filteredCount = matchCount;
}

// Staged back-out, shared by the Back button and the header's back arrow:
// clear the search, expand collapsed groups, return focus to the tabs, then
// leave for home.
void LibraryListActivity::handleBackAction() {
  auto& nav = activeNav();
  if (collectionKind != Collection::None) {
    {
      RenderLock lock(*this);
      collectionKind = Collection::None;
      collectionName.clear();
      collectionFold.clear();
      applyFilter();
    }
    nav.selected = 0;
    nav.top = 0;
    requestUpdate();
  } else if (!query.empty()) {
    {
      RenderLock lock(*this);
      query.clear();
      applyFilter();
    }
    nav.selected = 0;
    nav.top = 0;
    requestUpdate();
  } else if (groupsCollapsed) {
    restoreExpandedList();
  } else if (!tabsFocused() && !degraded) {
    // Keep the current list and viewport while returning focus to the tabs.
    nav.selected = 0;
    requestUpdate();
  } else {
    onGoHome();
  }
}

void LibraryListActivity::searchActionTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<LibraryListActivity*>(user)->openSearch();
}

void LibraryListActivity::backActionTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<LibraryListActivity*>(user)->handleBackAction();
}

void LibraryListActivity::rebuildActionTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<LibraryListActivity*>(user)->promptRebuildIndex();
}

// Title and author for one entry, read straight from the index. Only ever
// called for rows about to be drawn, so at most a screenful of strings exists
// at once.
bool LibraryListActivity::rowTextFor(const int entry, std::string& title, std::string* author, std::string* fileName,
                                     shelfstatus::Tag* tag) {
  title.clear();
  if (author) author->clear();
  if (fileName) fileName->clear();
  if (tag) *tag = shelfstatus::Tag::None;
  // No book has a status: skip the hash reads below.
  if (!anyShelfStatus()) tag = nullptr;
  if (entry < pinnedCount()) {
    const RecentBook* book = storeBook(entry);
    if (!book) return false;
    title = book->title;
    if (author) *author = book->author;
    if (fileName) *fileName = book->path;
    if (tag) *tag = tagForHash(library::clixPathHash(book->path.data(), book->path.size()));
    return true;
  }
  const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
  library::ClixRecord record{};
  if (ordinal != 0xFFFF && index.readRecord(ordinal, record)) {
    // The build already decided both fields — from the book's own metadata when
    // it has any, and with one spelling chosen per author across the library.
    // Re-parsing the name here would throw that away, and only works while the
    // name still looks like "Title - Author".
    if (author && !index.readAuthor(record, *author)) author->clear();
    // The stored title when the book gave one, the filename otherwise.
    if (!index.readTitle(record, title) || title.empty()) index.readName(record, title);
    if (fileName) index.readName(record, *fileName);
    uint64_t hash = 0;
    if (tag && index.readPathHash(record, hash)) *tag = tagForHash(hash);
  }
  if (title.empty()) title = tr(STR_LIBRARY_UNKNOWN_TITLE);
  return true;
}

bool LibraryListActivity::handleCustomInput() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;

  if (lockNextConfirmRelease && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lockNextConfirmRelease = false;
    return true;
  }
  if (lockNextBackRelease && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockNextBackRelease = false;
    return true;
  }

  return false;
}

bool LibraryListActivity::handleButtons() {
  const int count = listCount();

  // Every hold action fires at the threshold, mid-hold, including the ones
  // that open a dialog (remove-recent, delete). The release that follows is
  // armed as suppressed by wasLongPressed() and consumed globally by
  // ActivityManager::loop() before any activity runs, so it cannot land in
  // the freshly opened confirmation and select its default.
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, LONG_PRESS_MS)) {
    if (tabsFocused()) {
      if (!degraded) toggleSortDirection();
    } else if (count > 0) {
      onRowLongPress(selectedEntry());
    }
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    handleBackAction();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (tabsFocused()) {
      stepTab(1);
      return true;
    }
    if (count > 0) activateIndex(selectedEntry());
    return true;
  }

  return false;
}

void LibraryListActivity::navigateButtons() {
  const int count = listCount();
  if (mappedInput.wasPressed(MappedInputManager::Button::NavNext) ||
      mappedInput.wasPressed(MappedInputManager::Button::NavPrevious)) {
    navigationStartedOnTabs = tabsFocused();
  }
  buttonNavigator.onNextPress([this, count] {
    if (count > 0) moveRingTo(ringPos() == count ? 1 : ringPos() + 1);
  });
  buttonNavigator.onPreviousPress([this, count] {
    if ((!navigationStartedOnTabs || degraded) && count > 0) {
      moveRingTo(ringPos() <= 1 ? count : ringPos() - 1);
    }
  });
  // Search is an activation: defer it so holding Previous can still step tabs.
  buttonNavigator.onPreviousRelease([this] {
    if (navigationStartedOnTabs && tabsFocused() && !degraded) openSearch();
  });
  // A held button steps tabs while the strip has focus (the base behaviour
  // Settings keeps) and page-jumps once the selection is down in the rows,
  // where fast travel through a long shelf is what a hold means.
  buttonNavigator.onNextContinuous([this, count] {
    if (navigationStartedOnTabs) {
      activeNav().selected = 0;
      stepTab(1);
    } else if (count > 0) {
      moveRingTo(ButtonNavigator::nextPageIndex(selectedEntry(), count, activeNav().pageRows()) + 1);
    }
  });
  buttonNavigator.onPreviousContinuous([this, count] {
    if (navigationStartedOnTabs) {
      activeNav().selected = 0;
      stepTab(-1);
    } else if (count > 0) {
      moveRingTo(ButtonNavigator::previousPageIndex(selectedEntry(), count, activeNav().pageRows()) + 1);
    }
  });
}

void LibraryListActivity::buildRows(UiScreen& screen) {
  auto& nav = activeNav();
  const int count = listCount();
  const bool grouped = activeTabIndex == TITLE_TAB;

  fui::ListProps props;
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 1;
  // Breathing room between rows; the dense theme default packs the two-line
  // rows edge-to-edge.
  props.rowGap = std::max<int16_t>(screen.theme().listRowGap, 6);
  props.headerUnderline = false;
  syncTabListViewport(screen, props);

  // Keep one extra entry in the reusable window for a clipped trailing row.
  const size_t cap = static_cast<size_t>(nav.visibleRows > 0 ? nav.visibleRows : 1) + 1;
  if (winTitles.size() < cap) winTitles.resize(cap);
  if (winAuthors.size() < cap) winAuthors.resize(cap);
  if (winValues.size() < cap) winValues.resize(cap);
  progressLoadsLeft = 6;
  if (!groupsCollapsed && winHeaders.size() < cap) winHeaders.resize(cap);
  winItems.clear();
  if (winItems.capacity() < cap) winItems.reserve(cap);

  int rows = 0;
  int headers = 0;
  uint32_t previousInitial = 0;
  std::string rowFile;
  rowFile.reserve(128);
  // Capture this after syncTabListViewport(), which may clamp nav.top.
  const int windowStart = static_cast<int>(props.topIndex);
  for (int entry = windowStart; entry < count && rows < static_cast<int>(cap); entry++) {
    std::string& title = winTitles[static_cast<size_t>(rows)];
    std::string& author = winAuthors[static_cast<size_t>(rows)];
    fui::ListItem item;
    if (groupsCollapsed) {
      formatInitialHeading(titleInitialFor(groupStarts[entry]), title);
    } else {
      shelfstatus::Tag tag = shelfstatus::Tag::None;
      if (!rowTextFor(entry, title, &author, &rowFile, &tag)) continue;
      if (grouped) {
        const uint32_t initial = titleInitialFor(entry);
        if (rows == 0 || initial != previousInitial) {
          std::string& heading = winHeaders[static_cast<size_t>(headers++)];
          formatInitialHeading(initial, heading);
          item.sectionHeading = heading.c_str();
        }
        previousInitial = initial;
      }
      if (!author.empty()) item.subtitle = author.c_str();
      // The status sits at the row's right edge, where the cover view puts it
      // on the art.
      item.value = visibleTagLabel(tag);
      // A book in progress shows how far along it is instead of the word.
      std::string fullPath;
      if (tag == shelfstatus::Tag::Reading && item.value && pathForEntry(entry, fullPath)) {
        const int percent = cachedProgress(fullPath);
        if (percent >= 0) {
          std::string& text = winValues[static_cast<size_t>(rows)];
          text = std::to_string(percent) + "%";
          item.value = text.c_str();
        }
      }
    }

    item.label = title.c_str();
    // Group headings stay bare; every book row gets its file-type icon.
    if (!groupsCollapsed && !rowFile.empty()) item.icon = listIconFor(UITheme::getFileIcon(rowFile), 32);
    item.actionValue = static_cast<int16_t>(entry);
    winItems.push_back(item);
    rows++;
  }

  props.items = winItems.data();
  props.itemsWindowFirst = static_cast<uint16_t>(windowStart);
  props.itemsWindowCount = static_cast<uint16_t>(winItems.size());
#if FREEINK_CAP_TOUCH
  configureSwipeDelete(screen, props);
#endif
  screen.list(props);
#if defined(ENABLE_SERIAL_LOG) && LOG_LEVEL >= 2
  const int next = nav.drawnRows;
  const auto body = screen.body();
  LOG_DBG("LIB", "page tab=%d top=%d full=%d loaded=%d body=%d..%d next=%d title=%s", activeTabIndex, windowStart,
          nav.drawnRows, rows, body.y, body.bottom(),
          next < rows ? winItems[static_cast<size_t>(next)].actionValue : -1,
          next < rows ? winItems[static_cast<size_t>(next)].label : "<none>");
  LOG_DBG("LIB", "page first=%d title=%s", rows > 0 ? winItems[0].actionValue : -1,
          rows > 0 ? winItems[0].label : "<none>");
#endif
}

void LibraryListActivity::formatInitialHeading(uint32_t initial, std::string& out) {
  out.clear();
  if (initial == 0) {
    out.push_back('#');
    return;
  }
  if (initial >= 'a' && initial <= 'z') initial -= 'a' - 'A';
  utf8AppendCodepoint(initial, out);
}

void LibraryListActivity::buildHeader(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = screen.theme();
  fui::HeaderProps header;
  header.title = headerTitle();
  header.titleText = theme.titleText;
  header.titleText.align = theme.headerTitleAlign;
  header.sidePadding = theme.headerSidePadding;
  header.minTouchSize = theme.minTouchSize;
  header.styles = theme.popup;
  if (header.styles.normal.border.kind == fui::PaintKind::None && theme.headerUnderline > 0) {
    header.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    header.styles.normal.borderWidth = theme.headerUnderline;
  }
  header.trailingStyles = fui::plainStyles(fui::Paint::solid(fui::Color::Black));
  header.borderEdges = fui::EdgeBottom;
  // Same battery/clock band as every GUI.drawHeader screen; the header
  // heights are unified across themes, so the buttons derive from the band.
  GUI.applyHeaderStatus(renderer, header);
  if (mappedInput.hasTouch()) {
    header.leadingIcon = fui::bitmapFromIcon(icon_header_back_32);
    header.leadingAction = ACTION_BACK;
  }
  if (!degraded) {
    // Keep both touch actions together on the right; button boards reach
    // rebuild through the row options menu.
    header.trailingIcon = fui::bitmapFromIcon(icon_search_32);
    header.trailingAction = ACTION_SEARCH;
    if (mappedInput.hasTouch()) {
      header.trailingAdjacentIcon = fui::bitmapFromIcon(icon_refresh_cw_32);
      header.trailingAdjacentAction = ACTION_REBUILD;
    }
    // Vertical placement comes from applyHeaderStatus: buttons center on the
    // unified band.
  }
  const auto frameRect = screen.frame().screen();
  // Header and tabs share a screen-relative boundary, independent of bezel insets.
  const fui::Rect band = BaseTheme::headerBand(renderer,
                                               fui::Rect{frameRect.x, static_cast<int16_t>(metrics.topPadding),
                                                         frameRect.width, static_cast<int16_t>(metrics.headerHeight)},
                                               header);
  fui::header(screen.frame(), band, header);
}

int LibraryListActivity::coverCount() const { return listCount(); }

bool LibraryListActivity::coverPathFor(const int entry, std::string& out) {
  out.clear();
  if (entry < 0 || entry >= listCount()) return false;
  // Pinned recents are store rows, not index rows: rowFor() is documented as
  // not taking them. The gate is pinnedCount(), which is zero outside the
  // descending Recent shelf -- storeBook() alone answers on every tab, and
  // would hand the shelf a recent book's art under another book's title.
  if (entry < pinnedCount()) {
    const RecentBook* pinned = storeBook(entry);
    if (!pinned) return false;
    out = pinned->path;
    return !out.empty();
  }
  const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
  library::ClixRecord record{};
  if (ordinal == 0xFFFF || !index.readRecord(ordinal, record)) return false;
  return index.readPath(record, out) && !out.empty();
}

bool LibraryListActivity::coverLabelFor(const int entry, std::string& out) {
  out.clear();
  // No author: this runs once per visible cell inside the render pass with the
  // render lock held, and reading one costs two more index seeks plus a heap
  // allocation for anything past the small-string limit -- all to discard it.
  if (!rowTextFor(entry, out, nullptr)) return false;
  // A book with no title metadata falls back to its filename, which in a cell a
  // third of the screen wide is one unbreakable word: the wrapper hard-splits
  // it, and "Duna.epub" reads as "Duna.epu" over a lone "b". The extension
  // identifies nothing on a shelf where everything is a book.
  //
  // Grid only. The list view has the width to show the name whole, and the
  // other places this fallback surfaces -- list rows, the delete confirmation,
  // the home's featured card -- are wide enough not to split.
  FsHelpers::stripBookExtension(out);
  return true;
}

// "38%" for a book that is started and not finished, shown when it has no status tag.
// Finished books carry the Read tag, and an unopened book has no percentage.
const char* LibraryListActivity::coverProgressFor(const int entry) {
  if (entry < 0 || entry >= listCount()) return nullptr;
  std::string path;
  if (!coverPathFor(entry, path)) return nullptr;
  const int percent = cachedProgress(path);
  if (percent <= 0 || percent >= 100) return nullptr;
  // Read at once by the grid's tag painter, so one buffer is enough.
  static char label[8];
  snprintf(label, sizeof(label), "%d%%", percent);
  return label;
}

const char* LibraryListActivity::coverTagFor(const int entry) {
  const char* status = coverStatusFor(entry);
  return status != nullptr ? status : coverProgressFor(entry);
}

const char* LibraryListActivity::coverStatusFor(const int entry) {
  if (!anyShelfStatus() || entry < 0 || entry >= listCount()) return nullptr;
  if (entry < pinnedCount()) {
    const RecentBook* book = storeBook(entry);
    return book ? visibleTagLabel(tagForHash(library::clixPathHash(book->path.data(), book->path.size()))) : nullptr;
  }
  const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
  library::ClixRecord record{};
  uint64_t hash = 0;
  if (ordinal == 0xFFFF || !index.readRecord(ordinal, record) || !index.readPathHash(record, hash)) return nullptr;
  return visibleTagLabel(tagForHash(hash));
}

bool LibraryListActivity::coverViewActive() const {
  return SETTINGS.libraryCoverView != 0 && UITheme::supportsCoverGrid() && !degraded && !groupsCollapsed &&
         bookRowCount() > 0;
}

void LibraryListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // The position readout owns the line above the hints; rows must not overlap
  // it.
  constexpr int READOUT_GAP = 4;  // the list's fold sits just above the readout's line box
  const int16_t readoutReserved = static_cast<int16_t>(renderer.getLineHeight(SMALL_FONT_ID) + READOUT_GAP);
  const Rect area = UITheme::getContentArea(renderer);
  const auto bottomInset = static_cast<int16_t>(renderer.getScreenHeight() - area.y - area.height);
  buildHeader(screen);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                  static_cast<int16_t>(bottomInset + metrics.buttonHintsHeight + readoutReserved), 0});

  if (!degraded) buildTabBar(screen);
  if (bookRowCount() == 0) {
    const char* message = tr(STR_LIBRARY_NO_RESULTS);
    if (filterFailed) {
      message = tr(STR_LIBRARY_SEARCH_UNAVAILABLE);
    } else if (openedOnly() && index.bookCount() > 0) {
      message = tr(STR_LIBRARY_NO_OPENED);
    } else if (query.empty() && activeTabIndex == QUEUE_TAB) {
      message = tr(STR_LIBRARY_QUEUE_EMPTY);
    } else if (query.empty() && activeTabIndex == READ_TAB) {
      message = tr(STR_LIBRARY_READ_EMPTY);
    } else if (query.empty() && index.bookCount() > 0) {
      // Every book on the card has been read.
      message = tr(STR_LIBRARY_ALL_READ);
    } else if (query.empty()) {
      message = tr(STR_LIBRARY_EMPTY);
    }
    screen.centeredText(message);
    return;
  }
  if (coverViewActive()) {
    // The grid owns its covers and viewport; it returns the top entry so the
    // list nav stays in step for the readout and for page jumps.
    coverGrid.setSource(this);
    // One progress read per cell on the page; results are cached for later draws.
    progressLoadsLeft = LibraryCoverGrid::COLUMNS * LibraryCoverGrid::ROWS;
    auto& nav = activeNav();
    const auto body = screen.body();
    nav.top = coverGrid.build(screen, body, selectedEntry(), ACTION_ROW);
    // The loading popup was drawn over the header and tabs. Another pass clears
    // the screen and paints everything again; the covers are cached by then, so
    // it shows no popup.
    if (coverGrid.takePopupShown()) nav.rebuildNeeded = true;
    // screen.list() is what normally fills these, and the shelf never calls it.
    // Without them pageRows() is 1, so holding Next steps one cover instead of
    // a page -- the one thing a 4,000-book shelf needs.
    nav.drawnRows = nav.visibleRows = static_cast<int>(coverGrid.lastPageItems());
    return;
  }
  buildRows(screen);
}

// "12/69 books" at the bottom right: which book is selected, out of how many.
//
// NOT a page count. How many rows fit varies with the view (author headings
// consume band height), so a page total grows and shrinks as you scroll. The
// book position is stable by construction, and it answers the question the
// reader actually has: how far in am I, and how much is left.
void LibraryListActivity::drawPositionReadout() const {
  const int count = listCount();
  if (count <= 0) return;

  char buf[32];
  const char* positionFormat = groupsCollapsed ? tr(STR_LIBRARY_GROUP_POSITION) : tr(STR_LIBRARY_POSITION);
  snprintf(buf, sizeof(buf), positionFormat, selectedEntry() + 1, count);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getTextWidth(SMALL_FONT_ID, buf);
  // Bottom-right of the viewable area, clear of the bezel and rounded corner.
  const Rect area = UITheme::getContentArea(renderer);
  const int right = std::max(renderer.getScreenWidth() - area.x - area.width, bottomCornerClearance());
  const int x = renderer.getScreenWidth() - right - width - SIDE_PADDING;
  const int y = area.y + area.height - metrics.buttonHintsHeight - renderer.getLineHeight(SMALL_FONT_ID);
  renderer.drawText(SMALL_FONT_ID, x, y, buf, true);
}

const char* LibraryListActivity::headerTitle() const {
  if (!headerSearchTitle.empty()) return headerSearchTitle.c_str();
  return degraded ? tr(STR_LIBRARY_TITLE_UNSORTED) : tr(STR_LIBRARY);
}

void LibraryListActivity::drawHoldHelp() const {
  if (mappedInput.hasTouch() || groupsCollapsed) return;
  const char* help = nullptr;
  if (tabsFocused() && !degraded)
    help = tr(STR_LIBRARY_HOLD_SORT);
  else if (!tabsFocused() && listCount() > 0)
    help = tr(STR_LIBRARY_HOLD_OPTIONS);
  if (!help) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const Rect area = UITheme::getContentArea(renderer);
  const int left = std::max(area.x, bottomCornerClearance());
  const int y = area.y + area.height - metrics.buttonHintsHeight - lineHeight;
  GUI.drawHelpText(renderer,
                   Rect{left + SIDE_PADDING, y, renderer.getScreenWidth() / 2 - left - SIDE_PADDING, lineHeight}, help);
}

// OptionPopup is a self-contained modal: it owns the whole frame (hints
// included) whenever it is up, mirroring the FileBrowser pattern.
void LibraryListActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiTabListActivity::render(std::move(lock));
}

void LibraryListActivity::drawFooter() {
  drawPositionReadout();
  drawHoldHelp();

  const bool backGoesHome = tabsFocused() && !groupsCollapsed && query.empty();
  const char* backLabel = backGoesHome ? tr(STR_HOME) : tr(STR_BACK);
  const char* confirmLabel = groupsCollapsed ? tr(STR_SELECT) : tr(STR_OPEN);
  const bool canSearch = tabsFocused() && !degraded;
  const auto labels = mappedInput.mapLabels(backLabel, tabsFocused() ? tr(STR_TOGGLE) : confirmLabel,
                                            canSearch ? tr(STR_SEARCH) : tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void LibraryListActivity::startReread(const std::string& path, const std::string& title) {
  const std::string document = finishedbooks::documentIdFor(path);
  if (document.empty()) {
    LOG_ERR("LIB", "Cannot identify book for reread");
    return;
  }
  std::string author;
  // Arms the counter: reaching the end of a book already read counts another
  // reading only when this is set, which is what keeps dipping into an old
  // favourite from inflating the count.
  if (!FINISHED_BOOKS.setRereading(document, path, title, author, std::string(), true)) {
    LOG_ERR("LIB", "Could not arm reread");
    return;
  }
  // A book being reread is no longer waiting in the queue.
  READING_QUEUE.remove(path);

  // Back to the first page. Reaching the end does not clear the saved
  // position, so without this the book would reopen where it finished and the
  // reader would have to page back by hand. The parsed layout, the cover and
  // the thumbnails are untouched -- only this reader's place in the book goes.
  const std::string cachePath = FsHelpers::hasXtcExtension(path) ? Xtc(path, "/.crosspoint").getCachePath()
                                                                 : Epub(path, "/.crosspoint").getCachePath();
  const std::string progress = ProgressFile::pathIn(cachePath);
  if (Storage.exists(progress.c_str()) && !Storage.remove(progress.c_str())) {
    // The reread is still armed, so the count will be right; the reader just
    // starts where they left off and can navigate back themselves.
    LOG_ERR("LIB", "Could not clear progress for reread: %s", progress.c_str());
  }
  openBookByPath(path);
}

void LibraryListActivity::setBookRating(const std::string& path, const std::string& title, const std::string& document,
                                        const int8_t rating) {
  if (document.empty()) {
    LOG_ERR("LIB", "Cannot identify book to rate");
    return;
  }
  std::string author;
  if (!FINISHED_BOOKS.setRating(document, path, title, author, std::string(), rating)) {
    LOG_ERR("LIB", "Could not set rating");
  }
  requestUpdate();
}
