#include <gtest/gtest.h>

#include "components/themes/SkinMetrics.h"

namespace {
// Mirrors CrossPointSettings::UI_THEME without pulling in the settings header.
constexpr uint8_t kGood = 0;
constexpr uint8_t kPixelQuest = 1;
constexpr uint8_t kMonoMac = 5;
constexpr uint8_t kHandheldOs = 4;  // CrossPointSettings default
}  // namespace

TEST(SkinMetrics, OneTablePerThemeValue) { EXPECT_EQ(SkinMetrics::kCount, 6u); }

TEST(SkinMetrics, GoodThemeIsUnchanged) {
  EXPECT_EQ(&SkinMetrics::forTheme(kGood), &SkinMetrics::kTable[0]);
  EXPECT_EQ(SkinMetrics::forTheme(kGood).headerHeight, GoodMetrics::values.headerHeight);
}

TEST(SkinMetrics, EachSkinHasItsOwnTable) {
  EXPECT_NE(&SkinMetrics::forTheme(kPixelQuest), &SkinMetrics::forTheme(kMonoMac));
  EXPECT_EQ(SkinMetrics::forTheme(kPixelQuest).listSelectionStyle, 3);
}

TEST(SkinMetrics, OutOfRangeFallsBackToGood) {
  EXPECT_EQ(&SkinMetrics::forTheme(SkinMetrics::kCount), &SkinMetrics::kTable[0]);
  EXPECT_EQ(&SkinMetrics::forTheme(255), &SkinMetrics::kTable[0]);
}

TEST(SkinMetrics, DefaultSkinHasATable) {
  EXPECT_LT(kHandheldOs, SkinMetrics::kCount);
  EXPECT_NE(&SkinMetrics::forTheme(kHandheldOs), &SkinMetrics::kTable[0]);
}

TEST(SkinMetrics, ChromeStylesPerSkin) {
  constexpr HintStyle hints[] = {HintStyle::Boxes, HintStyle::Boxes, HintStyle::InvertedBar,
                                 HintStyle::Boxes, HintStyle::Ruled, HintStyle::Ruled};
  constexpr bool inverted[] = {false, true, true, true, false, false};
  for (uint8_t i = 0; i < SkinMetrics::kCount; i++) {
    EXPECT_EQ(SkinMetrics::forTheme(i).buttonHintsStyle, hints[i]) << "skin " << int(i);
    EXPECT_EQ(SkinMetrics::forTheme(i).headerInverted, inverted[i]) << "skin " << int(i);
  }
}
