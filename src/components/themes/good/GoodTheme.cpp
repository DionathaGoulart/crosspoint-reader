#include "GoodTheme.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "components/UITheme.h"
#include "fontIds.h"

// Internal constants
namespace {
constexpr int hPaddingInSelection = 8;
constexpr int cornerRadius = 0;
constexpr int topHintButtonY = 345;
constexpr int ruleThickness = 3;
}  // namespace

void GoodTheme::fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const {
  const bool charging = gpio.isUsbConnected();

  if (charging) {
    // Solid fill when charging so lightning bolt is visible
    renderer.fillRect(rect.x + 2, rect.y + 2, rect.width - 5, rect.height - 4);
    drawBatteryLightningBolt(renderer, rect.x + 4, rect.y + 2);
  } else {
    if (percentage > 10) {
      renderer.fillRect(rect.x + 2, rect.y + 2, 3, rect.height - 4);
    }
    if (percentage > 40) {
      renderer.fillRect(rect.x + 6, rect.y + 2, 3, rect.height - 4);
    }
    if (percentage > 70) {
      renderer.fillRect(rect.x + 10, rect.y + 2, 3, rect.height - 4);
    }
  }
}

void GoodTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label, const char* rightLabel) const {
  const int sidePadding = UITheme::getInstance().getMetrics().contentSidePadding;
  const int contentWidth = std::max(0, rect.width - sidePadding * 2);

  int labelWidth = contentWidth;
  if (rightLabel) {
    auto truncatedRightLabel = renderer.truncatedText(SMALL_FONT_ID, rightLabel, contentWidth, EpdFontFamily::REGULAR);
    const int rightLabelWidth = renderer.getTextWidth(SMALL_FONT_ID, truncatedRightLabel.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - sidePadding - rightLabelWidth, rect.y + 7,
                      truncatedRightLabel.c_str());
    labelWidth = std::max(0, contentWidth - rightLabelWidth - hPaddingInSelection);
  }

  if (labelWidth > 0) {
    auto truncatedLabel = renderer.truncatedText(UI_10_FONT_ID, label, labelWidth, EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, rect.x + sidePadding, rect.y + 6, truncatedLabel.c_str(), true,
                      EpdFontFamily::REGULAR);
  }

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

void GoodTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }

  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 80;
  constexpr int smallButtonHeight = 15;
  const HintStyle hintStyle = UITheme::getInstance().getMetrics().buttonHintsStyle;
  const bool ruled = hintStyle == HintStyle::Ruled;
  const int boxRadius =
      hintStyle == HintStyle::Pill ? UITheme::getInstance().getMetrics().buttonHintsHeight / 2 : cornerRadius;
  const int smallRadius = std::min(boxRadius, smallButtonHeight / 2);
  // Same metric getScreenSafeArea() reserves, so the hints always fill the reserved strip.
  const int buttonHeight = UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int buttonY = buttonHeight;  // Distance from bottom
  // Keyed to the portrait panel width: the 528-wide X3 gets more spacing than
  // the 480-wide boards (X4, X4 Pro, and the other 800x480 panels).
  constexpr int narrowButtonPositions[] = {58, 146, 254, 342};
  constexpr int wideButtonPositions[] = {65, 157, 291, 383};
  const int* buttonPositions = renderer.getScreenWidth() >= 528 ? wideButtonPositions : narrowButtonPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const bool grayscale = renderer.getRenderMode() != GfxRenderer::BW && !renderer.grayPlanesAreAbsolute();

  if (ruled) {
    // Ruled strip: one top rule and bare labels over the same button slots.
    // As in BaseTheme, gray planes get zero bits so the BW pass stays intact.
    renderer.fillRect(0, pageHeight - buttonY, renderer.getScreenWidth(), buttonHeight, grayscale);
    if (!grayscale) {
      renderer.fillRect(0, pageHeight - buttonY, renderer.getScreenWidth(), ruleThickness, true);
      for (int i = 0; i < 4; i++) {
        if (labels[i] != nullptr && labels[i][0] != '\0') {
          drawHintLabel(renderer, SMALL_FONT_ID, labels[i], buttonPositions[i], buttonWidth, pageHeight - buttonY,
                        buttonHeight);
        }
      }
    }
    renderer.setOrientation(orig_orientation);
    return;
  }

  if (hintStyle == HintStyle::InvertedBar) {
    // Inverted bar: white "N label" keys on black. True clears gray bits in the gray pass.
    renderer.fillRect(0, pageHeight - buttonY, renderer.getScreenWidth(), buttonHeight, true);
    if (!grayscale) {
      for (int i = 0; i < 4; i++) {
        if (labels[i] != nullptr && labels[i][0] != '\0') {
          char keyed[40];
          snprintf(keyed, sizeof(keyed), "%d %s", i + 1, labels[i]);
          drawHintLabel(renderer, SMALL_FONT_ID, keyed, buttonPositions[i], buttonWidth, pageHeight - buttonY,
                        buttonHeight, false);
        }
      }
    }
    renderer.setOrientation(orig_orientation);
    return;
  }

  for (int i = 0; i < 4; i++) {
    const int x = buttonPositions[i];
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      // Draw the filled background and border for a FULL-sized button
      renderer.fillRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, boxRadius,
                               grayscale ? Color::Black : Color::White);
      if (grayscale) continue;
      renderer.drawRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, 1, boxRadius, true, true, false,
                               false, true);
      drawHintLabel(renderer, SMALL_FONT_ID, labels[i], x, buttonWidth, pageHeight - buttonY, buttonHeight);
    } else {
      // Draw the filled background and border for a SMALL-sized button
      renderer.fillRoundedRect(x, pageHeight - smallButtonHeight, buttonWidth, smallButtonHeight, smallRadius,
                               grayscale ? Color::Black : Color::White);
      if (grayscale) continue;
      renderer.drawRoundedRect(x, pageHeight - smallButtonHeight, buttonWidth, smallButtonHeight, 1, smallRadius, true,
                               true, false, false, true);
    }
  }

  renderer.setOrientation(orig_orientation);
}

void GoodTheme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const {
  if (gpio.hasTouch()) {
    return;
  }

  const int screenWidth = renderer.getScreenWidth();
  const int buttonWidth =
      UITheme::getInstance().getMetrics().sideButtonHintsWidth;  // Width on screen (height when rotated)
  constexpr int buttonHeight = 78;                               // Height on screen (width when rotated)
  constexpr int buttonMargin = 0;

  if (gpio.hasEdgeSideButtons()) {
    // Edge-button layout (X3, X4 Pro): Up on left side, Down on right side, positioned higher
    constexpr int x3ButtonY = 155;

    if (topBtn != nullptr && topBtn[0] != '\0') {
      renderer.drawRoundedRect(buttonMargin, x3ButtonY, buttonWidth, buttonHeight, 1, cornerRadius, false, true, false,
                               true, true);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, topBtn);
      renderer.drawTextRotated90CW(SMALL_FONT_ID, buttonMargin, x3ButtonY + (buttonHeight + textWidth) / 2, topBtn);
    }

    if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
      const int rightX = screenWidth - buttonWidth;
      renderer.drawRoundedRect(rightX, x3ButtonY, buttonWidth, buttonHeight, 1, cornerRadius, true, false, true, false,
                               true);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, bottomBtn);
      renderer.drawTextRotated90CW(SMALL_FONT_ID, rightX, x3ButtonY + (buttonHeight + textWidth) / 2, bottomBtn);
    }
  } else {
    // X4 layout: Both buttons stacked on right side
    const char* labels[] = {topBtn, bottomBtn};
    const int x = screenWidth - buttonWidth;

    if (topBtn != nullptr && topBtn[0] != '\0') {
      renderer.drawRoundedRect(x, topHintButtonY, buttonWidth, buttonHeight, 1, cornerRadius, true, false, true, false,
                               true);
    }

    if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
      renderer.drawRoundedRect(x, topHintButtonY + buttonHeight + 5, buttonWidth, buttonHeight, 1, cornerRadius, true,
                               false, true, false, true);
    }

    for (int i = 0; i < 2; i++) {
      if (labels[i] != nullptr && labels[i][0] != '\0') {
        const int y = topHintButtonY + (i * buttonHeight) + 5;
        const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
        renderer.drawTextRotated90CW(SMALL_FONT_ID, x, y + (buttonHeight + textWidth) / 2, labels[i]);
      }
    }
  }
}
