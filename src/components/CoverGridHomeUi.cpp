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
#include "icons/settings2.h"
#include "icons/stats.h"
#include "icons/transfer.h"
#include "stats/FinishedBooksStore.h"
#include "stats/GoalsSettings.h"
#include "stats/ReadingQueueStore.h"
#include "stats/StatsService.h"
#include "util/BookProgress.h"

namespace fui = freeink::ui;
namespace {
constexpr fui::ActionId SELECT = 1;
// Padding around the cover; also feeds the screen's horizontal inset so the
// content lands on the header chrome's inset line.
constexpr int16_t COVER_CELL_INSET = 6;
}  // namespace

CoverGridHomeUi::CoverGridHomeUi(GfxRenderer& renderer)
    : UiAppHost(renderer), coverCache(renderer), renderer(renderer) {}

void CoverGridHomeUi::begin(const std::vector<RecentBook>& recent, const SlotTab slot, bool continuing) {
  books = &recent;
  slotTab = slot;
  hasContinueReading = continuing;
  showCovers = UITheme::supportsCoverGrid();
  if (showCovers && !recent.empty()) coverCache.begin();
  resetUi();
  app.on(SELECT, &CoverGridHomeUi::onAction, this);
  app.setScreen(&CoverGridHomeUi::screenFn, this);
  refreshCoverPaths();
  progress = hasContinueReading && !books->empty() ? loadBookProgress(books->front().path) : -1;
  if (progress >= 0) snprintf(progressText, sizeof(progressText), "%d%%", progress);

  // The queue stays loaded, so it is loaded first: its blocks then sit below
  // the transient ones and do not split the space they free.
  READING_QUEUE.ensureLoaded();
  // The summary and the tag both read the finished-books store. Its JSON parse
  // runs before the stats aggregate is allocated, so the two peaks never add.
  const bool finishedWasLoaded = FINISHED_BOOKS.isLoaded();
  FINISHED_BOOKS.ensureLoaded();
  // Once per entry, not once per repaint, and only the figures the targets are
  // measured against: the full load() also builds the achievement inputs this
  // screen never reads. summaryOnly() still takes a ~22 KB transient
  // aggregate, so it is skipped when no goal is set and nothing would use it.
  summaryLoaded = false;
  summary = goals::anyEnabled(goalTargetsFromSettings()) ? statsService::summaryOnly(&summaryLoaded) : stats::Summary{};
  const bool storesReady = FINISHED_BOOKS.isLoaded();
  for (size_t i = 0; i < coverTags.size(); ++i) {
    coverTags[i] = storesReady && i < recent.size() ? shelfstatus::forPath(recent[i].path) : shelfstatus::Tag::None;
  }
  // Without PSRAM the store (up to 500 records) is not held through the
  // reader: the summary and the tag are copies. With PSRAM it stays loaded so
  // each return to Home does not parse it again.
  if (!finishedWasLoaded && !showCovers) FINISHED_BOOKS.reset();
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
  // The +8 surplus gives the paint clip a little bleed so the art's
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
  // Sides clear the rounded corners like the header, so the content lines up under it.
  const int corner = BoardConfig::ACTIVE.cornerRadius;
  const auto left = static_cast<int16_t>(std::max(safe.x, corner));
  const auto right = static_cast<int16_t>(std::max(renderer.getScreenWidth() - safe.x - safe.width, corner));
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(safe.y), right,
                                                static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height),
                                                left});
  // Horizontal inset sized so content plus the cover cell inset sits on the
  // theme's side-padding line: goals and tabs then align with the clock and
  // battery, which tuck a few px further in (headerStatusInset's optical bias).
  const int16_t hInset = std::max<int16_t>(
      0, static_cast<int16_t>(UITheme::getInstance().getMetrics().headerSidePadding - COVER_CELL_INSET - left));
  // Drop the status band, goals, and book down: touch boards get a full step
  // (no button-hint band to fit), button boards a modest nudge. The tabs stay
  // put (bottom-anchored below).
  const int16_t topInset =
      BoardConfig::hasTouch() ? static_cast<int16_t>(theme.spaceLg) : static_cast<int16_t>(theme.spaceSm + 4);
  screen.insetContent(fui::Insets{topInset, hInset, theme.spaceSm, hInset});
  // Reserve the band's slot in the flow (its content draws at a fixed screen
  // position in drawHeaderBand); the slot doubles as padding above the heading.
  screen.takeTop(UITheme::getInstance().getMetrics().batteryBarHeight, theme.spaceSm);
  // Button boards run tighter above the tabs to make room for the top step.
  const int16_t tabGap = BoardConfig::hasTouch() ? theme.spaceSm : static_cast<int16_t>(4);
  const auto& board = BoardConfig::ACTIVE;
  auto tabRect = screen.takeBottom(UITheme::getInstance().getMetrics().coverGridTabBarHeight, tabGap);
  // The board's tab height sits at the bottom of the theme's slot, then its
  // offset lower; the slot itself is unchanged, so the book keeps its size.
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
    drawGoals(screen, screen.takeTop(goalsStripHeight(screen), theme.spaceSm));
    drawTabs(screen, fitTabWidth(tabRect.inset(fui::Insets{0, COVER_CELL_INSET, 0, COVER_CELL_INSET})));
    drawEmpty(screen);
    drawHeaderBand();
    return;
  }
  // Goals above, book below: the targets are the reason to look, the book is
  // the reason to pick the device up, so it keeps whatever the goals leave.
  drawGoals(screen, screen.takeTop(goalsStripHeight(screen), theme.spaceSm));
  if (showCovers) {
    drawCurrent(screen, screen.body());
  } else {
    drawCurrentText(screen, screen.body());
  }
  drawTabs(screen, fitTabWidth(tabRect));
  drawHeaderBand();
}

void CoverGridHomeUi::drawHeaderBand() {
  // The stock full-width band at the theme's topPadding, exactly like every
  // pushed screen's header: the battery/clock hold one position across the
  // whole UI, and the content is widened to meet them (see the hInset above).
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

void CoverGridHomeUi::drawCurrent(UiScreen& screen, fui::Rect rect) {
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
  // The selected state is a slim accent bar drawn after the card (see
  // drawSelectionBar), not a bookCard indicator: every ring/outline treatment
  // tried here either overwhelmed the large cover or made the heading above
  // read as misaligned.
  card.state = fui::StateNormal;
  card.styles = theme.listRow;
  card.styles.selected.background = fui::Paint::dither(fui::Color::LightGray);
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
  // A 2:3 cover box across at most half the width, inside the cell inset.
  const int maxCoverWidth = rect.width / 2 - 2 * COVER_CELL_INSET;
  card.coverSize.height = std::max(1, std::min<int>(rect.height - 12, maxCoverWidth * 3 / 2));
  card.coverSize.width = std::max(1, card.coverSize.height * 2 / 3);
  noteThumbHeight(card.coverSize.height);
  card.coverPainterUserData = this;
  card.coverPainter = [](fui::DrawTarget& target, fui::Rect cover, const fui::BookCardProps&, void* user) {
    return static_cast<CoverGridHomeUi*>(user)->paintFramedCover(target, cover, 0);
  };
  fui::bookCard(screen.frame(), rect, card);
  drawSelectionBar(screen, rect, card.coverSize.height);
}

void CoverGridHomeUi::drawCurrentText(UiScreen& screen, fui::Rect rect) {
  // No cover cache without PSRAM: the same card, with no cover box, so the
  // title, author, shelf tag and progress take the full width.
  const auto& theme = screen.theme();
  const auto& book = books->front();
  // card is a member to keep it off the render stack; the device's mode never
  // changes, so only this function ever fills it.
  card.title = book.title.c_str();
  card.author = book.author.empty() ? nullptr : book.author.c_str();
  const char* tag = shelfstatus::label(coverTags[0]);
  card.meta = tag != nullptr && *tag != '\0' ? tag : nullptr;
  card.progressLabel = progress >= 0 ? progressText : nullptr;
  card.progress = std::max(0, progress);
  card.progressMax = progress >= 0 ? 100 : 0;
  card.action = SELECT;
  card.state = fui::StateNormal;
  card.styles = theme.listRow;
  card.titleText = theme.titleText;
  card.titleText.bold = true;
  card.titleText.maxLines = 3;
  card.authorText = theme.bodyText;
  card.metaText = theme.smallText;
  card.progressText = theme.smallText;
  card.progressHeight = 6;
  card.padding = fui::Insets{10, 14, 10, 14};
  // Zero width, full height: bookCard clamps the height to the content and
  // anchors the progress row to the cover's bottom, so it lands at the
  // card's bottom instead of its middle.
  card.coverSize = fui::Size{0, INT16_MAX};
  card.gap = 0;
  card.centerTextVertically = true;
  auto& target = screen.target();
  const int textHeight = target.lineHeight(card.titleText.font) * card.titleText.maxLines +
                         target.lineHeight(card.authorText.font) + target.lineHeight(card.metaText.font) +
                         target.lineHeight(card.progressText.font) + card.progressHeight + 4 * theme.spaceSm +
                         card.padding.top + card.padding.bottom;
  constexpr int16_t SHADOW = 4;
  // Leave room below and to the right for the card's hard shadow.
  const auto height = static_cast<int16_t>(std::min<int>(rect.height - SHADOW, textHeight));
  rect.y = static_cast<int16_t>(rect.y + (rect.height - height) / 2);
  rect.height = height;
  rect.width = static_cast<int16_t>(rect.width - SHADOW);
  const auto ink = fui::Paint::solid(fui::Color::Black);
  target.fill(fui::Rect{static_cast<int16_t>(rect.x + SHADOW), static_cast<int16_t>(rect.bottom()), rect.width, SHADOW},
              ink);
  target.fill(fui::Rect{static_cast<int16_t>(rect.right()), static_cast<int16_t>(rect.y + SHADOW), SHADOW, rect.height},
              ink);
  fui::bookCard(screen.frame(), rect, card);
  target.stroke(rect, ink, 2);
  drawSelectionBar(screen, rect, rect.height);
}

void CoverGridHomeUi::drawSelectionBar(UiScreen& screen, const fui::Rect rect, const int16_t height) {
  // Button boards only: a vertical accent bar left of the card marks it as the
  // button cursor without framing the cover. Touch boards tap directly.
  if (selected != 0 || BoardConfig::hasTouch()) return;
  screen.target().fill(
      fui::Rect{static_cast<int16_t>(rect.x - 6), static_cast<int16_t>(rect.y + (rect.height - height) / 2), 4, height},
      fui::Paint::solid(fui::Color::Black));
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

// An 8-bit gauge: a row of blocks, solid when earned and hollow when not.
constexpr int16_t GOAL_BAR_HEIGHT = 10;
constexpr int16_t GOAL_CELL_WIDTH = 10;
constexpr int16_t GOAL_CELL_GAP = 2;
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
  if (rows == 0 || !summaryLoaded) return lineHeight + GOAL_ROW_GAP;
  return rows * (lineHeight + GOAL_LABEL_BAR_GAP + GOAL_BAR_HEIGHT + GOAL_ROW_GAP);
}

void CoverGridHomeUi::drawGoals(UiScreen& screen, const fui::Rect rect) {
  const auto& theme = screen.theme();
  auto& target = screen.target();
  const goals::Targets targets = goalTargetsFromSettings();

  // No goals set, or no memory for the figures: one line instead of bars,
  // which would read zero as real progress.
  if (!goals::anyEnabled(targets) || !summaryLoaded) {
    fui::TextStyle hint = theme.smallText;
    hint.align = fui::TextAlign::Center;
    target.text(rect, goals::anyEnabled(targets) ? tr(STR_MEMORY_ERROR) : tr(STR_GOAL_NONE_SET), hint);
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
    const int cells = std::max(1, (rect.width + GOAL_CELL_GAP) / (GOAL_CELL_WIDTH + GOAL_CELL_GAP));
    // Spread the leftover width over the gaps so the gauge spans the row.
    const int gapSum = rect.width - cells * GOAL_CELL_WIDTH;
    // Any progress shows at least one block, and only 100% fills them all.
    const int filled = progress.percent >= 100 ? cells
                       : progress.percent <= 0 ? 0
                                               : std::clamp((cells * progress.percent + 50) / 100, 1, cells - 1);
    for (int cell = 0; cell < cells; ++cell) {
      const int x0 = rect.x + cell * GOAL_CELL_WIDTH + (cells > 1 ? gapSum * cell / (cells - 1) : 0);
      const fui::Rect block{static_cast<int16_t>(x0), barY, GOAL_CELL_WIDTH, GOAL_BAR_HEIGHT};
      if (cell < filled) {
        target.fill(block, fui::Paint::solid(fui::Color::Black));
      } else {
        target.stroke(block, fui::Paint::solid(fui::Color::Black), 2);
      }
    }
    y = static_cast<int16_t>(barY + GOAL_BAR_HEIGHT + GOAL_ROW_GAP);
  }
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
  static constexpr const uint8_t* ICONS[] = {FolderIcon, LibraryIcon,  StatsIcon,
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
  tabs.selectedUnderline = 4;
  tabs.distributedSlotWidth = 0;
  fui::tabBar(screen.frame(), rect, tabs);
}

bool CoverGridHomeUi::paintFramedCover(fui::DrawTarget& target, fui::Rect rect, size_t index) {
  constexpr int16_t SHADOW_OFFSET = 4;
  const auto ink = fui::Paint::solid(fui::Color::Black);
  target.fill(fui::Rect{rect.right(), static_cast<int16_t>(rect.y + SHADOW_OFFSET), SHADOW_OFFSET, rect.height}, ink);
  target.fill(fui::Rect{static_cast<int16_t>(rect.x + SHADOW_OFFSET), rect.bottom(), rect.width, SHADOW_OFFSET}, ink);
  // The false spine draws inside the cover paint (HomeCoverCache), glued to
  // the art's left edge, so it stays aligned whatever each cover's margin is.
  const bool drawn = index < coverPaths.size() && coverCache.paint(rect, index, coverPaths[index]);
  if (drawn && index < coverTags.size()) drawCoverTag(target, rect, shelfstatus::label(coverTags[index]), tagText);
  // A hard 2 px frame, so the cover reads as a sprite on the page.
  target.stroke(rect, ink, 2);
  return drawn;
}
