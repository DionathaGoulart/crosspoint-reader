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

TEST(SkinMetrics, SafeAreaFieldsMatchWhatGoodThemeDraws) {
  for (uint8_t i = 0; i < SkinMetrics::kCount; i++) {
    const ThemeMetrics& m = SkinMetrics::forTheme(i);
    EXPECT_EQ(m.buttonHintsHeight, GoodMetrics::values.buttonHintsHeight) << "skin " << int(i);
    EXPECT_EQ(m.sideButtonHintsWidth, GoodMetrics::values.sideButtonHintsWidth) << "skin " << int(i);
    EXPECT_EQ(m.contentSidePadding, GoodMetrics::values.contentSidePadding) << "skin " << int(i);
  }
}

TEST(SkinMetrics, DefaultSkinHasATable) {
  EXPECT_LT(kHandheldOs, SkinMetrics::kCount);
  EXPECT_NE(&SkinMetrics::forTheme(kHandheldOs), &SkinMetrics::kTable[0]);
}
