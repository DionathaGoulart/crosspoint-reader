#pragma once

#include <FreeInkUICore.h>

#include <algorithm>

// A small black label in a cover's top-left corner ("Rereading", "To read").
// Drawn after the cover art, so it also sits on top of a cached cover.
inline void drawCoverTag(freeink::ui::DrawTarget& target, const freeink::ui::Rect cover, const char* label,
                         freeink::ui::TextStyle style) {
  namespace fui = freeink::ui;
  if (label == nullptr || *label == '\0') return;
  constexpr int16_t kMargin = 4;
  constexpr int16_t kPadX = 5;
  constexpr int16_t kPadY = 1;
  style.color = fui::Color::White;
  style.bold = true;
  style.maxLines = 1;
  style.align = fui::TextAlign::Center;
  const int16_t textWidth = target.measureText(style.font, label, style).width;
  const int16_t height = static_cast<int16_t>(target.lineHeight(style.font) + kPadY * 2);
  const int16_t width = static_cast<int16_t>(std::min<int>(textWidth + kPadX * 2, cover.width - kMargin * 2));
  if (width <= kPadX * 2 || height >= cover.height) return;
  const fui::Rect pill{static_cast<int16_t>(cover.x + kMargin), static_cast<int16_t>(cover.y + kMargin), width, height};
  // A white rim keeps the label readable on a dark cover.
  target.fill(fui::Rect{static_cast<int16_t>(pill.x - 1), static_cast<int16_t>(pill.y - 1),
                        static_cast<int16_t>(pill.width + 2), static_cast<int16_t>(pill.height + 2)},
              fui::Paint::solid(fui::Color::White), 0);
  target.fill(pill, fui::Paint::solid(fui::Color::Black), 0);
  target.text(fui::Rect{static_cast<int16_t>(pill.x + kPadX), static_cast<int16_t>(pill.y + kPadY),
                        static_cast<int16_t>(pill.width - kPadX * 2), static_cast<int16_t>(height - kPadY * 2)},
              label, style);
}
