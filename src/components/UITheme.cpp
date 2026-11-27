#include "UITheme.h"

#include <BoardConfig.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/SkinMetrics.h"
#include "components/themes/good/GoodTheme.h"
#include "fontIds.h"

static_assert(SkinMetrics::kCount == CrossPointSettings::UI_THEME_COUNT, "SkinMetrics::kTable must match UI_THEME");

UITheme UITheme::instance;

UITheme::UITheme() = default;

bool UITheme::supportsCoverGrid() { return HalMemory::getPsramHeap().totalBytes > 0; }

const ThemeMetrics& UITheme::getMetrics() const {
  // hasTouch() can flip once touch init completes after static construction, so the
  // cached copy is refreshed when the flag differs instead of copying the struct per call.
  // The cache also keys on the theme setting, so a change in Settings takes effect on the next call.
  const bool touch = gpio.hasTouch();
  const uint8_t skin = SETTINGS.uiTheme;
  if (!metricsValid || touch != metricsForTouch || skin != metricsSkin) {
    adjustedMetrics = SkinMetrics::forTheme(skin);
    metricsSkin = skin;
    if (touch) {
      adjustedMetrics.buttonHintsHeight = 0;
    }
    metricsForTouch = touch;
    metricsValid = true;
  }
  return adjustedMetrics;
}

// Full drawable content area: screen minus the board's viewable insets (bezel /
// rounded-corner clearance), oriented to the current rotation. The single place
// the inset math lives; every screen derives its content bounds from here (or
// from getScreenSafeArea, which builds on it).
Rect UITheme::getContentArea(const GfxRenderer& renderer) {
  int viTop = 0, viRight = 0, viBottom = 0, viLeft = 0;
  renderer.getOrientedViewableTRBL(&viTop, &viRight, &viBottom, &viLeft);
  return Rect{viLeft, viTop, renderer.getScreenWidth() - viLeft - viRight,
              renderer.getScreenHeight() - viTop - viBottom};
}

void UITheme::setActiveSideBar(GfxRenderer& renderer, const int section) {
  const int width = instance.getMetrics().sideBarWidth;
  const bool on = section >= 0 && width > 0;
  sideBarSection = on ? section : -1;
  renderer.setChromeInsetLeft(on ? width : 0);
}

void UITheme::drawChrome(const GfxRenderer& renderer) {
  const int frame = instance.getMetrics().frameThickness;
  if (frame > 0) {
    int top, right, bottom, left;
    renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
    // The frame hugs the panel edge; the side bar sits inside it, so use the bezel-only left.
    const int x = left - renderer.getChromeInsetLeft();
    const int w = renderer.getScreenWidth() - x - right;
    const int h = renderer.getScreenHeight() - top - bottom;
    renderer.fillRect(x, top, w, frame, true);
    renderer.fillRect(x, top + h - frame, w, frame, true);
    renderer.fillRect(x, top, frame, h, true);
    renderer.fillRect(x + w - frame, top, frame, h, true);
  }
  drawSideBar(renderer);
}

void UITheme::drawSideBar(const GfxRenderer& renderer) {
  const int width = renderer.getChromeInsetLeft();
  if (sideBarSection < 0 || width <= 0) return;
  const StrId labels[] = {StrId::STR_SIDEBAR_HOME, StrId::STR_SIDEBAR_BOOKS, StrId::STR_SIDEBAR_FILES,
                          StrId::STR_SIDEBAR_SEND, StrId::STR_SIDEBAR_SETUP};
  constexpr int count = sizeof(labels) / sizeof(labels[0]);
  constexpr int rule = 2;

  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int x = left - width;
  const int height = renderer.getScreenHeight() - top - bottom;
  const int cell = height / count;
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);

  renderer.fillRect(x, top, width - rule, height, false);
  renderer.fillRect(x + width - rule, top, rule, height, true);
  for (int i = 0; i < count; i++) {
    const bool selected = i == sideBarSection;
    const int y = top + i * cell;
    if (selected) renderer.fillRect(x, y, width - rule, cell, true);
    const auto text = renderer.truncatedText(SMALL_FONT_ID, I18N.get(labels[i]), width - rule - 4);
    const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, text.c_str());
    renderer.drawText(SMALL_FONT_ID, x + (width - rule - textWidth) / 2, y + (cell - lineHeight) / 2, text.c_str(),
                      !selected);
  }
}

// Content area excluding the button hints.
Rect UITheme::getScreenSafeArea(const GfxRenderer& renderer, bool hasFrontButtonHints, bool hasSideButtonHints) {
  auto orientation = renderer.getOrientation();
  Rect safeArea = getContentArea(renderer);
  const ThemeMetrics metrics = getMetrics();
  switch (orientation) {
    case GfxRenderer::Orientation::Portrait:
      if (hasFrontButtonHints) {
        safeArea.height -= metrics.buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      if (hasFrontButtonHints) {
        safeArea.x += metrics.buttonHintsHeight;
        safeArea.width -= metrics.buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      if (hasFrontButtonHints) {
        safeArea.y += metrics.buttonHintsHeight;
        safeArea.height -= metrics.buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      if (hasFrontButtonHints) {
        safeArea.width -= metrics.buttonHintsHeight;
      }
      break;
  }
  return safeArea;
}

Rect UITheme::getHeaderRect(const GfxRenderer& renderer, bool hasFrontButtonHints, bool hasSideButtonHints) {
  const Rect safe = getScreenSafeArea(renderer, hasFrontButtonHints, hasSideButtonHints);
  const Rect content = getContentArea(renderer);
  const ThemeMetrics metrics = getMetrics();
  return Rect{safe.x - content.x, safe.y - content.y + metrics.topPadding,
              safe.width + renderer.getScreenWidth() - content.width, metrics.headerHeight};
}

std::string UITheme::getCoverThumbPath(std::string coverBmpPath, int coverHeight) {
  size_t pos = coverBmpPath.find("[HEIGHT]", 0);
  if (pos != std::string::npos) {
    coverBmpPath.replace(pos, 8, std::to_string(coverHeight));
  }
  return coverBmpPath;
}

UIIcon UITheme::getFileIcon(const std::string& filename) {
  if (filename.back() == '/') {
    return Folder;
  }
  if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename)) {
    return Book;
  }
  if (FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename)) {
    return Text;
  }
  if (FsHelpers::hasImageExtension(filename)) {
    return Image;
  }
  return File;
}

int UITheme::getStatusBarHeight() {
  const ThemeMetrics metrics = UITheme::getInstance().getMetrics();
  const auto sb = SETTINGS.statusBarSpec();

  // Layout reservation is hardware-agnostic: pass clockAvailable=true so the
  // reserved height does not depend on whether an RTC is present.
  return (sb.textLaneVisible(true) ? (metrics.statusBarVerticalMargin) : 0) +
         (sb.showsProgressBar() ? (sb.progressBarHeightPx + metrics.progressBarMarginTop) : 0);
}

int UITheme::getProgressBarHeight() {
  const ThemeMetrics metrics = UITheme::getInstance().getMetrics();
  const auto sb = SETTINGS.statusBarSpec();
  return sb.showsProgressBar() ? (sb.progressBarHeightPx + metrics.progressBarMarginTop) : 0;
}

// Height the reader's status bar takes above the bottom inset. The EEGO A4 draws
// its progress bar inside the inset (BaseTheme::drawStatusBar), so the bar takes none.
int UITheme::getReaderStatusBarHeight() {
  return getStatusBarHeight() - (BoardConfig::isEegoA4() ? getProgressBarHeight() : 0);
}

// Centered text implementation that takes the safe area into account
void UITheme::drawCenteredText(const GfxRenderer& renderer, Rect screen, int fontId, int y, const char* text,
                               bool black, EpdFontFamily::Style style) {
  const int x = screen.x + (screen.width - renderer.getTextWidth(fontId, text, style)) / 2;
  renderer.drawText(fontId, x, y, text, black, style);
}

void UITheme::drawCenteredWrappedText(const GfxRenderer& renderer, Rect bounds, int fontId, const char* text,
                                      int maxLines, bool black, EpdFontFamily::Style style,
                                      TextVerticalAlignment verticalAlignment) {
  if (!text || *text == '\0' || bounds.width <= 0 || bounds.height <= 0 || maxLines <= 0) return;

  const int lineHeight = renderer.getLineHeight(fontId);
  if (lineHeight <= 0) return;

  const int lineLimit = std::min(maxLines, bounds.height / lineHeight);
  if (lineLimit <= 0) return;

  const auto alignedTop = [&](const int textHeight) {
    switch (verticalAlignment) {
      case TextVerticalAlignment::CENTER:
        return bounds.y + (bounds.height - textHeight) / 2;
      case TextVerticalAlignment::BOTTOM:
        return bounds.y + bounds.height - textHeight;
      case TextVerticalAlignment::TOP:
      default:
        return bounds.y;
    }
  };

  if (renderer.getTextWidth(fontId, text, style) <= bounds.width) {
    drawCenteredText(renderer, bounds, fontId, alignedTop(lineHeight), text, black, style);
    return;
  }

  const auto lines = renderer.wrappedText(fontId, text, bounds.width, lineLimit, style);
  int y = alignedTop(static_cast<int>(lines.size()) * lineHeight);
  for (const auto& line : lines) {
    drawCenteredText(renderer, bounds, fontId, y, line.c_str(), black, style);
    y += lineHeight;
  }
}
