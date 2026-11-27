#pragma once

#include <cstdint>

#include "components/themes/BaseTheme.h"
#include "components/themes/good/GoodTheme.h"

// Per-skin metrics for the retro themes. Every skin starts from GoodMetrics and
// overrides only the geometry the skin changes, so the tables are constexpr data
// with no heap and no runtime cost. Values follow the "GoodTheme 8-bit Lab" mock.
//
// GoodTheme reads contentSidePadding, buttonHintsHeight and sideButtonHintsWidth
// through UITheme::getMetrics(), the same source as getScreenSafeArea(), so a skin
// may override them.
namespace SkinMetrics {

struct Tweaks {
  int headerHeight;
  int headerUnderlineSize;
  int listRowHeight;
  int listWithSubtitleRowHeight;
  int listRowGap;
  int listInset;
  int listSelectionStyle;  // fui::SelectionStyle order, see ThemeMetrics
  int popupFrameThickness;
  int popupShadowOffset;
  HintStyle buttonHintsStyle;
  bool headerInverted;
};

constexpr ThemeMetrics derive(const Tweaks& t) {
  ThemeMetrics m = GoodMetrics::values;
  m.headerHeight = t.headerHeight;
  m.headerUnderlineSize = t.headerUnderlineSize;
  m.listRowHeight = t.listRowHeight;
  m.listWithSubtitleRowHeight = t.listWithSubtitleRowHeight;
  m.listRowGap = t.listRowGap;
  m.listInset = t.listInset;
  m.listSelectionStyle = t.listSelectionStyle;
  m.popupFrameThickness = t.popupFrameThickness;
  m.popupShadowOffset = t.popupShadowOffset;
  m.buttonHintsStyle = t.buttonHintsStyle;
  m.headerInverted = t.headerInverted;
  return m;
}

// Indexed by CrossPointSettings::UI_THEME. Index 0 is GoodTheme, unchanged.
//                                      hdr  rule row  rowSub gap inset sel frame shadow hints inv
inline constexpr ThemeMetrics kTable[] = {
    GoodMetrics::values,
    derive({72, 4, 44, 64, 4, 16, 3, 3, 6, HintStyle::Boxes, true}),        // Pixel Quest: arrow cursor, hard shadow
    derive({56, 0, 40, 60, 0, 12, 0, 4, 0, HintStyle::InvertedBar, true}),  // Terminal DOS: flat, inverted rows
    derive({76, 4, 48, 68, 6, 16, 0, 4, 6, HintStyle::Pill, true}),         // Arcade HUD: heavy borders, shadow
    derive({64, 3, 44, 64, 0, 12, 0, 3, 0, HintStyle::Ruled, false}),       // Handheld OS: flat, inverted rows
    derive({56, 2, 40, 60, 0, 14, 0, 2, 5, HintStyle::Ruled, false}),       // Mono Mac: thin rules, small shadow
};
inline constexpr uint8_t kCount = sizeof(kTable) / sizeof(kTable[0]);

// Out-of-range values (corrupt file, newer firmware) fall back to GoodTheme.
constexpr const ThemeMetrics& forTheme(const uint8_t uiTheme) { return kTable[uiTheme < kCount ? uiTheme : 0]; }

}  // namespace SkinMetrics
