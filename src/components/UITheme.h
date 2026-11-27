#pragma once

#include <EpdFontFamily.h>

#include <functional>

#include "CrossPointSettings.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/good/GoodTheme.h"

class UITheme {
  // Static instance
  static UITheme instance;

 public:
  enum class TextVerticalAlignment { TOP, CENTER, BOTTOM };

  UITheme();
  static UITheme& getInstance() { return instance; }

  const ThemeMetrics& getMetrics() const;
  const BaseTheme& getTheme() const { return theme; }
  // The drawable content area: full screen minus the board's viewable insets
  // (bezel / rounded-corner clearance from BoardConfig::ACTIVE.viewableInsets,
  // oriented to the current rotation). THE single place activities should get
  // their content bounds from so every screen clears the bezel identically.
  // Zero-inset on rectangular panels, so no visible change there.
  static Rect getContentArea(const GfxRenderer& renderer);
  // Called by the render task before each screen: reserves the side bar's width on the
  // renderer for a root screen (section >= 0) when the skin has one, and clears it otherwise.
  static void setActiveSideBar(GfxRenderer& renderer, int section);
  // Chrome hook: paints the side bar over the finished frame, just before it is shown.
  static void drawChrome(const GfxRenderer& renderer);
  static void drawSideBar(const GfxRenderer& renderer);
  Rect getScreenSafeArea(const GfxRenderer& renderer, bool hasFrontButtonHints = false,
                         bool hasSideButtonHints = false);
  // Header rect for GUI.drawHeader() on a screen laid out in getScreenSafeArea():
  // clear of the button hints but NOT inset for the bezel, since the header band
  // applies the board's viewable insets itself.
  Rect getHeaderRect(const GfxRenderer& renderer, bool hasFrontButtonHints = false, bool hasSideButtonHints = false);
  static void drawCenteredText(const GfxRenderer& renderer, Rect screen, int fontId, int y, const char* text,
                               bool black = true, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  // Wraps only overflowing text, then aligns the complete line block within bounds.
  static void drawCenteredWrappedText(const GfxRenderer& renderer, Rect bounds, int fontId, const char* text,
                                      int maxLines, bool black = true,
                                      EpdFontFamily::Style style = EpdFontFamily::REGULAR,
                                      TextVerticalAlignment verticalAlignment = TextVerticalAlignment::CENTER);
  // PSRAM holds the cover caches. Without it, Home shows the current book as
  // text and the Library has no cover view.
  static bool supportsCoverGrid();
  // Home draws the current book as a cover: PSRAM present and the user left it on.
  static bool homeShowsCover() { return supportsCoverGrid() && SETTINGS.homeShowCover != 0; }
  static std::string getCoverThumbPath(std::string coverBmpPath, int coverHeight);
  static UIIcon getFileIcon(const std::string& filename);
  static int getStatusBarHeight();
  static int getProgressBarHeight();
  static int getReaderStatusBarHeight();

 private:
  // GoodTheme, the only theme. Held by value: no allocation at static init.
  GoodTheme theme;
  mutable ThemeMetrics adjustedMetrics;
  mutable bool metricsValid = false;
  mutable bool metricsForTouch = false;
  mutable uint8_t metricsSkin = 0;
  static inline int sideBarSection = -1;
};

// Helper macro to access current theme
#define GUI UITheme::getInstance().getTheme()
