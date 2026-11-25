#include "CoverGridHomeUi.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "CoverTag.h"
#include "MappedInputManager.h"
#include "UITheme.h"
#include "icons/blocks.h"
#include "icons/book.h"
#include "icons/folder.h"
#include "icons/library.h"
#include "icons/recent.h"
#include "icons/settings2.h"
#include "icons/transfer.h"
#include "stats/FinishedBooksStore.h"
#include "stats/GoalsSettings.h"
#include "stats/ReadingQueueStore.h"
#include "stats/StatsService.h"
#include "util/BookProgress.h"

namespace fui = freeink::ui;
namespace {
constexpr fui::ActionId SELECT = 1;
// Grid cell padding around each cover; also feeds the screen's horizontal
// inset so the cover columns land on the header chrome's inset line.
constexpr int16_t COVER_CELL_INSET = 6;
constexpr int COVER_ROW_EXTRA_HEIGHT = 8;
}  // namespace

CoverGridHomeUi::CoverGridHomeUi(GfxRenderer& renderer)
    : UiAppHost(renderer), coverCache(renderer), renderer(renderer) {}

int CoverGridHomeUi::bookSlots() { return UITheme::hasGoalsHome() ? 1 : MAX_BOOKS; }

void CoverGridHomeUi::begin(const std::vector<RecentBook>& recent, const SlotTab slot, bool continuing) {
  books = &recent;
  slotTab = slot;
  hasContinueReading = continuing;
  if (!recent.empty()) coverCache.begin();
  resetUi();
  app.on(SELECT, &CoverGridHomeUi::onAction, this);
  app.setScreen(&CoverGridHomeUi::screenFn, this);
  refreshCoverPaths();
  progress = hasContinueReading && !books->empty() ? loadBookProgress(books->front().path) : -1;
  if (progress >= 0) snprintf(progressText, sizeof(progressText), "%d%%", progress);

  goalsMode = UITheme::hasGoalsHome();
  if (goalsMode) {
    // Once per entry, not once per repaint, and only the figures the targets
    // are measured against: the full load() also builds the achievement inputs
    // this screen never reads.
    summary = statsService::summaryOnly();
  }

  // The tags need the finished-books store. The goals home keeps it loaded for
  // its summary anyway; the grid home lets it go again, so up to 500 records
  // are not held through the reader just to label seven covers.
  const bool finishedWasLoaded = FINISHED_BOOKS.isLoaded();
  FINISHED_BOOKS.ensureLoaded();
  READING_QUEUE.ensureLoaded();
  const bool storesReady = FINISHED_BOOKS.isLoaded();
  for (size_t i = 0; i < coverTags.size(); ++i) {
    coverTags[i] = storesReady && i < recent.size() ? shelfstatus::forPath(recent[i].path) : shelfstatus::Tag::None;
  }
  if (!finishedWasLoaded && !goalsMode) FINISHED_BOOKS.reset();
}

void CoverGridHomeUi::refreshCoverPaths() {
  coverCache.invalidate();
  for (size_t i = 0; i < books->size() && i < coverPaths.size(); ++i) refreshCoverPath(i);
}

void CoverGridHomeUi::refreshCoverPath(size_t index) {
  if (index >= books->size() || index >= coverPaths.size()) return;
  coverCache.invalidate(index);
  coverPaths[index] =
      thumbHeight > 0 ? UITheme::getCoverThumbPath((*books)[index].coverBmpPath, thumbHeight) : std::string();
}

int CoverGridHomeUi::thumbHeightFor() const { return thumbHeight > 0 ? thumbHeight : THUMB_HEIGHT; }

bool CoverGridHomeUi::takeThumbHeightChanged() { return std::exchange(thumbHeightChanged, false); }

void CoverGridHomeUi::noteThumbHeight(int slotHeight) {
  // One shared height for every slot (hero and grid covers are the same
  // size). The +8 surplus gives the paint clip a little bleed so the art's
  // rightward nudge never exposes the left edge; covers narrower than the
  // 2:3 slot may show a thin sliver at the sides rather than being cropped.
  const int height = std::max(1, slotHeight + 8);
  if (thumbHeight != height) {
    thumbHeight = height;
    thumbHeightChanged = true;
    refreshCoverPaths();
  }
}

void CoverGridHomeUi::onAction(const fui::ActionEvent& event, void* user) {
  auto& self = *static_cast<CoverGridHomeUi*>(user);
  self.pending = event.value;
  self.app.clearTapFlash();
}

int CoverGridHomeUi::selectedAction(const MappedInputManager& input) {
  pending = -1;
  const auto touch = routeTouch(input);
  return touch.snap.touchReleased ? pending : -1;
}

void CoverGridHomeUi::screenFn(UiScreen& screen, void* user) { static_cast<CoverGridHomeUi*>(user)->draw(screen); }

void CoverGridHomeUi::draw(UiScreen& screen) {
  coverCache.prepare();
  const auto& theme = screen.theme();
  tagText = theme.smallText;
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true);
  // Sides clear the rounded corners like the header, so the grid lines up under it.
  const int corner = BoardConfig::ACTIVE.cornerRadius;
  const auto left = static_cast<int16_t>(std::max(safe.x, corner));
  const auto right = static_cast<int16_t>(std::max(renderer.getScreenWidth() - safe.x - safe.width, corner));
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(safe.y), right,
                                                static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height),
                                                left});
  // Horizontal inset sized so the outer cover columns (content x plus the
  // grid's cell inset) sit on the theme's side-padding line: the heading and
  // tabs then align with the clock and battery, which tuck a few px further
  // in (headerStatusInset's optical bias).
  const int16_t hInset = std::max<int16_t>(
      0, static_cast<int16_t>(UITheme::getInstance().getMetrics().headerSidePadding - COVER_CELL_INSET - left));
  // Drop the status band, hero, and grid down: touch boards get a full step
  // (no button-hint band to fit), button boards a modest nudge. The tabs stay
  // put (bottom-anchored below).
  const int16_t topInset =
      BoardConfig::hasTouch() ? static_cast<int16_t>(theme.spaceLg) : static_cast<int16_t>(theme.spaceSm + 4);
  screen.insetContent(fui::Insets{topInset, hInset, theme.spaceSm, hInset});
  const bool landscape = renderer.getScreenWidth() > renderer.getScreenHeight();
  // Reserve the band's slot in the flow (its content draws at a fixed screen
  // position in drawHeaderBand); the slot doubles as padding above the heading.
  screen.takeTop(UITheme::getInstance().getMetrics().batteryBarHeight, theme.spaceSm);
  // Button boards run tighter above the tabs to make room for the top step.
  const int16_t tabGap = BoardConfig::hasTouch() ? theme.spaceSm : static_cast<int16_t>(4);
  const auto& board = BoardConfig::ACTIVE;
  auto tabRect = screen.takeBottom(UITheme::getInstance().getMetrics().coverGridTabBarHeight, tabGap);
  // The board's tab height sits at the bottom of the theme's slot, then its
  // offset lower; the slot itself is unchanged, so the grid keeps its size.
  const auto tabHeight = static_cast<int16_t>(tabRect.height * board.tabBarHeightPct / 100);
  tabRect.y = static_cast<int16_t>(tabRect.y + tabRect.height - tabHeight + board.tabBarOffsetY);
  tabRect.height = tabHeight;
  // The board's share of the row the tabs spread across, centered.
  const auto fitTabWidth = [&board](fui::Rect rect) {
    const auto width = static_cast<int16_t>(rect.width * board.tabBarWidthPct / 100);
    rect.x = static_cast<int16_t>(rect.x + (rect.width - width) / 2);
    rect.width = width;
    return rect;
  };
  if (books->empty()) {
    // The goals strip still draws: a reader who has set targets but not yet
    // opened a book should see them, not only the empty-shelf message.
    if (goalsMode) drawGoals(screen, screen.takeTop(goalsStripHeight(screen), theme.spaceSm));
    drawTabs(screen, fitTabWidth(tabRect.inset(fui::Insets{0, COVER_CELL_INSET, 0, COVER_CELL_INSET})));
    drawEmpty(screen);
    drawHeaderBand();
    return;
  }
  // Hero cover matches the grid covers: the body splits into three equal
  // cover rows (hero + two grid rows), so the featured section is one row
  // tall and every cover on screen shares one size. The metadata floor still
  // applies when the rows would be too short for the hero's text lines.
  const fui::Rect body = screen.body();
  const int rowGap = std::max<int>(4, body.width / 100);
  grid.gap = grid.rowGap = rowGap;
  const int coverRowHeight = std::max(1, (body.height - theme.spaceSm - 2 * rowGap) / 3 + COVER_ROW_EXTRA_HEIGHT);
  const int16_t featuredHeight = std::min<int>(
      body.height, std::max<int>(coverRowHeight, screen.target().lineHeight(theme.bodyText.font) * (landscape ? 1 : 2) +
                                                     screen.target().lineHeight(theme.smallText.font) * 2 + 32));
  if (goalsMode) {
    // Goals above, cover below: the targets are the reason to look, the cover
    // is the reason to pick the device up. The cover keeps whatever is left,
    // so it stays the largest thing on the screen.
    drawGoals(screen, screen.takeTop(goalsStripHeight(screen), theme.spaceSm));
    // The cover takes everything the goals left, across half the width.
    const fui::Rect coverArea = screen.body();
    drawCurrent(screen, coverArea, coverArea.height, 2);
  } else {
    drawCurrent(screen, screen.takeTop(featuredHeight, theme.spaceSm), coverRowHeight, GRID_COLUMNS);
    drawGrid(screen);
  }
  if (!goalsMode) {
    // The tabs line up with the grid's cover columns. Goals mode draws no
    // grid, so gridBounds is unset there and the tabs keep the rect
    // takeBottom() already gave them.
    tabRect.x = gridBounds.x + grid.cellInset.left;
    tabRect.width = gridBounds.width - grid.cellInset.left - grid.cellInset.right;
  }
  drawTabs(screen, fitTabWidth(tabRect));
  drawHeaderBand();
}

void CoverGridHomeUi::drawHeaderBand() {
  // The stock full-width band at the theme's topPadding, exactly like every
  // pushed screen's header: the battery/clock hold one position across the
  // whole UI, and the grid is widened to meet them (see the hInset above).
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.batteryBarHeight}, nullptr);
}

void CoverGridHomeUi::drawEmpty(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto body = screen.body();
  auto title = theme.titleText;
  title.bold = true;
  title.align = fui::TextAlign::Center;
  auto message = theme.bodyText;
  message.align = fui::TextAlign::Center;
  constexpr int16_t ICON_SIZE = 32;
  const int16_t titleHeight = screen.target().lineHeight(title.font);
  const int16_t messageHeight = screen.target().lineHeight(message.font);
  const int16_t contentHeight = ICON_SIZE + theme.spaceLg + titleHeight + theme.spaceSm + messageHeight;
  int16_t y = body.y + std::max(0, (body.height - contentHeight) / 2);
  renderer.drawIcon(BookIcon, body.x + (body.width - ICON_SIZE) / 2, y, ICON_SIZE);
  y += ICON_SIZE + theme.spaceLg;
  screen.target().text(fui::Rect{body.x, y, body.width, titleHeight}, tr(STR_NO_OPEN_BOOK), title);
  y += titleHeight + theme.spaceSm;
  screen.target().text(fui::Rect{body.x, y, body.width, messageHeight}, tr(STR_START_READING), message);
}

void CoverGridHomeUi::drawCurrent(UiScreen& screen, fui::Rect rect, const int coverRowHeight, const int coverColumns) {
  const auto& theme = screen.theme();
  const auto& book = books->front();
  card.title = book.title.c_str();
  card.author = book.author.empty() ? nullptr : book.author.c_str();
  card.meta = nullptr;
  card.progressLabel = progress >= 0 ? progressText : nullptr;
  card.centerTextOnCover = true;
  card.progress = std::max(0, progress);
  card.progressMax = progress >= 0 ? 100 : 0;
  card.action = SELECT;
  // The featured card's selected state is a slim accent bar drawn after the
  // card (see below), not a bookCard indicator: every ring/outline treatment
  // tried here either overwhelmed the large cover or made the heading above
  // read as misaligned.
  card.state = fui::StateNormal;
  card.styles = theme.listRow;
  card.styles.selected.background = fui::Paint::dither(fui::Color::LightGray);
  // The grid thumbs' selection ring draws with this border: gray like Lyra's
  // selection box, not solid black.
  card.styles.selected.border = fui::Paint::dither(fui::Color::LightGray);
  card.styles.selected.foreground = fui::Paint::solid(fui::Color::Black);
  card.styles.selected.radius = theme.listRowRadius;
  card.styles.active = card.styles.selected;
  card.titleText = theme.bodyText;
  card.titleText.maxLines = renderer.getScreenWidth() > renderer.getScreenHeight() ? 1 : 2;
  card.authorText = theme.smallText;
  card.progressText = theme.smallText;
  card.progressHeight = 6;
  card.padding = fui::Insets{6, 6, 6, 6};
  card.gap = theme.spaceLg + theme.spaceSm;
  // Fit the shared 2:3 cover box within both the row and a grid column. No
  // gap reserve: the cell insets already keep adjacent covers apart, and the
  // SpaceBetween layout absorbs whatever slack remains.
  const int maxCoverWidth = rect.width / coverColumns - 2 * COVER_CELL_INSET;
  card.coverSize.height = std::max(1, std::min(std::min<int>(rect.height, coverRowHeight) - 12, maxCoverWidth * 3 / 2));
  card.coverSize.width = std::max(1, card.coverSize.height * 2 / 3);
  noteThumbHeight(card.coverSize.height);
  gridBounds = layoutGrid(screen.body());
  rect.x = gridBounds.x;
  rect.width = gridBounds.width;
  card.coverPainterUserData = this;
  card.coverPainter = [](fui::DrawTarget& target, fui::Rect cover, const fui::BookCardProps&, void* user) {
    return static_cast<CoverGridHomeUi*>(user)->paintFramedCover(target, cover, 0);
  };
  fui::bookCard(screen.frame(), rect, card);

  if (selected == 0 && !BoardConfig::hasTouch()) {
    // Button boards only: a vertical accent bar left of the card, cover-height
    // and vertically centered on it, marks the featured card as the button
    // cursor without framing the cover. Touch boards tap directly and need no
    // cursor on the hero card.
    const int16_t barH = card.coverSize.height;
    screen.target().fill(
        fui::Rect{static_cast<int16_t>(rect.x - 5), static_cast<int16_t>(rect.y + (rect.height - barH) / 2), 3, barH},
        fui::Paint::dither(fui::Color::LightGray));
  }
}

fui::Rect CoverGridHomeUi::layoutGrid(fui::Rect rect) {
  // Exactly the hero's box: the three-equal-rows split in draw() already
  // guarantees it fits, and sharing the size keeps one cached thumb per book.
  grid.cellInset = fui::Insets{COVER_CELL_INSET, COVER_CELL_INSET, COVER_CELL_INSET, COVER_CELL_INSET};
  grid.coverSize = card.coverSize;
  grid.rowHeight = grid.coverSize.height + 12;
  // Full content width: the SpaceBetween column layout pins the outer covers
  // to the rect edges, so the grid reaches the chrome's inset line instead of
  // centering at its natural width.
  rect.height = GRID_ROWS * grid.rowHeight + (GRID_ROWS - 1) * grid.rowGap;
  return rect;
}

namespace {
// Goal rows, in the order they are drawn. Daily first: it is the one that
// changes between two glances at the home screen.
constexpr goals::Id GOAL_ORDER[] = {goals::Id::DailyMinutes, goals::Id::WeeklyMinutes, goals::Id::StreakDays,
                                    goals::Id::BooksPerYear};
constexpr StrId GOAL_LABELS[] = {StrId::STR_GOAL_DAILY, StrId::STR_GOAL_WEEKLY, StrId::STR_GOAL_STREAK,
                                 StrId::STR_GOAL_BOOKS_YEAR};
static_assert(std::size(GOAL_ORDER) == std::size(GOAL_LABELS));
static_assert(std::size(GOAL_ORDER) == goals::kCount);

constexpr int16_t GOAL_BAR_HEIGHT = 6;
constexpr int16_t GOAL_ROW_GAP = 8;
constexpr int16_t LABEL_FIGURE_GAP = 8;
constexpr int16_t GOAL_LABEL_BAR_GAP = 3;

// "42/60", in whatever unit the goal counts. Raw figures rather than a
// percentage: "3/12" says what is left, "25%" does not. Minutes stay bare
// numbers so the pair reads as one quantity -- statsText::duration() would render
// the left side "--" at zero against a right side of "1 h 00".
void formatGoalValue(char* out, const size_t size, const goals::Progress& progress) {
  std::snprintf(out, size, "%u/%u", static_cast<unsigned>(progress.current), static_cast<unsigned>(progress.target));
}
}  // namespace

int CoverGridHomeUi::goalsStripHeight(UiScreen& screen) {
  const auto& theme = screen.theme();
  const int16_t lineHeight = screen.target().lineHeight(theme.smallText.font);
  const goals::Targets targets = goalTargetsFromSettings();
  int rows = 0;
  for (const goals::Id id : GOAL_ORDER) {
    if (goals::targetFor(id, targets) != 0) ++rows;
  }
  // Nothing set: one line of guidance instead of an empty band, so the theme
  // explains itself rather than looking broken.
  if (rows == 0) return lineHeight + GOAL_ROW_GAP;
  return rows * (lineHeight + GOAL_LABEL_BAR_GAP + GOAL_BAR_HEIGHT + GOAL_ROW_GAP);
}

void CoverGridHomeUi::drawGoals(UiScreen& screen, const fui::Rect rect) {
  const auto& theme = screen.theme();
  auto& target = screen.target();
  const goals::Targets targets = goalTargetsFromSettings();

  if (!goals::anyEnabled(targets)) {
    fui::TextStyle hint = theme.smallText;
    hint.align = fui::TextAlign::Center;
    target.text(rect, tr(STR_GOAL_NONE_SET), hint);
    return;
  }

  const int16_t lineHeight = target.lineHeight(theme.smallText.font);
  int16_t y = rect.y;
  for (size_t i = 0; i < std::size(GOAL_ORDER); ++i) {
    const goals::Id id = GOAL_ORDER[i];
    const auto progress = goals::progressFor(id, targets, summary);
    if (!progress.enabled) continue;

    char value[48];
    formatGoalValue(value, sizeof(value), progress);

    // Label left, figure right, on one line; the bar spans the full width
    // underneath so four goals still read as four rows and not as a table.
    fui::TextStyle figure = theme.smallText;
    figure.align = fui::TextAlign::Right;
    target.text(fui::Rect{rect.x, y, rect.width, lineHeight}, value, figure);
    // The label gets only what the figure leaves. Sharing the full width would
    // let a long translation run underneath the number instead of truncating.
    const auto figureWidth = target.measureText(figure.font, value, figure).width;
    const auto labelWidth = static_cast<int16_t>(std::max(0, rect.width - figureWidth - LABEL_FIGURE_GAP));
    fui::TextStyle label = theme.smallText;
    label.align = fui::TextAlign::Left;
    target.text(fui::Rect{rect.x, y, labelWidth, lineHeight}, I18N.get(GOAL_LABELS[i]), label);

    const int16_t barY = static_cast<int16_t>(y + lineHeight + GOAL_LABEL_BAR_GAP);
    target.stroke(fui::Rect{rect.x, barY, rect.width, GOAL_BAR_HEIGHT}, fui::Paint::solid(fui::Color::Black), 1);
    const auto fill = static_cast<int16_t>(rect.width * progress.percent / 100);
    if (fill > 0) {
      target.fill(fui::Rect{rect.x, barY, fill, GOAL_BAR_HEIGHT}, fui::Paint::solid(fui::Color::Black));
    }
    y = static_cast<int16_t>(barY + GOAL_BAR_HEIGHT + GOAL_ROW_GAP);
  }
}

void CoverGridHomeUi::drawGrid(UiScreen& screen) {
  const auto rect = gridBounds;
  grid.count = books->size() > 1 ? books->size() - 1 : 0;
  grid.columns = GRID_COLUMNS;
  grid.columnLayout = fui::CoverGridColumnLayout::SpaceBetween;
  grid.action = SELECT;
  grid.inputMask = fui::InputTouch;
  grid.selectedIndex = selected > 0 && selected < static_cast<int>(books->size()) ? selected - 1 : -1;
  // Same thick cover ring as the featured card; the dithered Cell background
  // was easy to miss behind a dark cover.
  grid.selectionIndicator = fui::CoverGridSelectionIndicator::CoverFrame;
  // Thick dithered ring sized for the small thumbs: 6px outside the cover,
  // 2px over its edge.
  grid.selectedCoverFrameGap = 6;
  grid.selectedCoverFrameWidth = 8;
  grid.cellStyles = card.styles;
  grid.labelHeight = 0;
  grid.labelGap = 0;
  grid.scrollIndicator = false;
  grid.itemProvider = [](uint16_t index, void*) { return fui::coverGridItem(nullptr, index + 1); };
  grid.coverPainterUserData = this;
  grid.coverPainter = [](fui::DrawTarget& target, fui::Rect cover, const fui::CoverGridItem&, uint16_t index,
                         void* user) {
    return static_cast<CoverGridHomeUi*>(user)->paintFramedCover(target, cover, index + 1);
  };
  fui::coverGrid(screen.frame(), rect, grid);
}

void CoverGridHomeUi::drawTabs(UiScreen& screen, fui::Rect rect) {
  // A tab's value is a menu index, so this order has to be the order of
  // HomeActivity::indexToMenuItem(): file browser, library, statistics, the
  // optional plugins/OPDS slot, file transfer, settings. The statistics entry
  // used to be missing here, which shifted every icon from the slot onwards:
  // the transfer icon opened Statistics, the settings icon opened File
  // Transfer, and Settings itself was left with a navigable position that drew
  // no tab at all. Add an entry to one list and it has to be added to both.
  static constexpr int SLOT_POSITION = 3;
  static constexpr const uint8_t* ICONS[] = {FolderIcon, LibraryIcon,  RecentIcon,
                                             BlocksIcon, TransferIcon, Settings2Icon};
  static constexpr int POSITIONS = static_cast<int>(sizeof(ICONS) / sizeof(ICONS[0]));
  // TAB_POSITIONS is what HomeActivity checks its own menu against; keeping the
  // icon count tied to it means an icon added here without a menu entry, or the
  // reverse, fails to compile instead of drawing the wrong destination.
  static_assert(POSITIONS == static_cast<int>(TAB_POSITIONS), "ICONS must cover exactly TAB_POSITIONS");
  static_assert(POSITIONS <= static_cast<int>(std::tuple_size<decltype(tabItems)>::value),
                "tabItems must hold every tab position");
  int count = 0;
  for (int i = 0; i < POSITIONS; ++i) {
    if (i == SLOT_POSITION && slotTab == SlotTab::NONE) continue;
    auto& tab = tabItems[count];
    // `count`, not `i`: indexToMenuItem() closes the gap left by an absent
    // slot too, so the drawn position and the menu index stay equal.
    tab.value = books->size() + count;
    tab.selected = selected == tab.value;
    tab.label = nullptr;
    ++count;
  }
  tabs.tabs = tabItems.data();
  tabs.count = count;
  tabs.layout = fui::TabBarLayout::SpaceBetween;
  tabs.action = SELECT;
  tabs.inputMask = fui::InputTouch;
  tabs.iconSize = 32;
  tabs.iconPainterUserData = this;
  tabs.iconPainter = [](fui::DrawTarget&, fui::Rect iconRect, const fui::TabItem& tab, uint8_t, void* user) {
    const auto& self = *static_cast<CoverGridHomeUi*>(user);
    const int index = tab.value - static_cast<int>(self.books->size());
    // Back from the drawn position to the position in ICONS, which still has
    // the slot in it when the reader has no plugins and no catalog.
    const int position = self.slotTab == SlotTab::NONE && index >= SLOT_POSITION ? index + 1 : index;
    if (position < 0 || position >= POSITIONS) return false;
    const uint8_t* bitmap = position == SLOT_POSITION && self.slotTab == SlotTab::OPDS ? BookIcon : ICONS[position];
    self.renderer.drawIcon(bitmap, iconRect.x, iconRect.y, iconRect.width);
    return true;
  };
  // No pill fill; the underline marks the selected tab.
  tabs.tabStyles.explicitlySet = true;
  tabs.selectedUnderline = 2;
  tabs.distributedSlotWidth = 0;
  fui::tabBar(screen.frame(), rect, tabs);
}

bool CoverGridHomeUi::paintFramedCover(fui::DrawTarget& target, fui::Rect rect, size_t index) {
  constexpr int16_t SHADOW_OFFSET = 2;
  const auto ink = fui::Paint::solid(fui::Color::Black);
  target.fill(fui::Rect{rect.right(), static_cast<int16_t>(rect.y + SHADOW_OFFSET), SHADOW_OFFSET, rect.height}, ink);
  target.fill(fui::Rect{static_cast<int16_t>(rect.x + SHADOW_OFFSET), rect.bottom(), rect.width, SHADOW_OFFSET}, ink);
  // The false spine draws inside the cover paint (HomeCoverCache), glued to
  // the art's left edge, so it stays aligned whatever each cover's margin is.
  const bool drawn = index < coverPaths.size() && coverCache.paint(rect, index, coverPaths[index]);
  if (drawn && index < coverTags.size()) drawCoverTag(target, rect, shelfstatus::label(coverTags[index]), tagText);
  return drawn;
}
