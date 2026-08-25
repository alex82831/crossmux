#include "RetroTheme.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <string>

#include "CrossPointSettings.h"
#include "fontIds.h"

namespace {
constexpr int kTitleFontId = UI_12_FONT_ID;
constexpr int kShadow = 3;  // hard drop-shadow offset for beveled chrome
}  // namespace

// ---- shared brushes -----------------------------------------------------

void RetroTheme::drawStripes(const GfxRenderer& renderer, const int x, const int y, const int width,
                             const int height) {
  if (width <= 0 || height <= 0) return;
  // Six System-7 pinstripes fit a 48px bar; scale by drawing every 4px.
  for (int sy = y + 2; sy < y + height - 2; sy += 4) {
    renderer.drawLine(x, sy, x + width - 1, sy, true);
  }
}

void RetroTheme::drawBevelBox(const GfxRenderer& renderer, Rect rect, const bool pressed) {
  if (pressed) {
    // Pressed: the button drops onto its own shadow and inverts.
    renderer.fillRect(rect.x + kShadow, rect.y + kShadow, rect.width - kShadow, rect.height - kShadow, true);
    return;
  }
  const int w = rect.width - kShadow;
  const int h = rect.height - kShadow;
  renderer.fillRect(rect.x, rect.y, w, h, false);
  renderer.drawRect(rect.x, rect.y, w, h, true);
  // Hard drop shadow, Win 3.x style.
  renderer.fillRect(rect.x + kShadow, rect.y + h, w, kShadow, true);
  renderer.fillRect(rect.x + w, rect.y + kShadow, kShadow, h, true);
}

// ---- header -------------------------------------------------------------

void RetroTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  if (title == nullptr) return;
  const auto& m = RetroMetrics::values;
  const int sidePadding = m.headerSidePadding;
  const int barTop = rect.y + 2;
  const int barHeight = rect.height - 10;

  // Frame the whole title bar.
  renderer.fillRect(rect.x, barTop, rect.width, barHeight, false);
  renderer.drawRect(rect.x, barTop, rect.width, barHeight, true);

  // Battery slot on the right, boxed like a window widget.
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  int batteryGroupLeft = rect.x + rect.width - sidePadding - m.batteryWidth;
  if (showBatteryPercentage) {
    batteryGroupLeft -= renderer.getTextWidth(STATUS_NUMERIC_FONT_ID, "100%") + batteryPercentSpacing;
  }
  const int batteryY = barTop + (barHeight - m.batteryHeight) / 2;

  // Title in a cleared box over the stripes; stripes fill both flanks.
  char composed[96];
  const char* shown = title;
  if (subtitle != nullptr && subtitle[0] != '\0') {
    snprintf(composed, sizeof(composed), "%s · %s", title, subtitle);
    shown = composed;
  }
  const int maxTitleWidth = std::max(0, (batteryGroupLeft - rect.x) - 2 * (sidePadding + 14));
  const std::string titleText = renderer.truncatedText(kTitleFontId, shown, maxTitleWidth, EpdFontFamily::BOLD);
  const int titleWidth = renderer.getTextWidth(kTitleFontId, titleText.c_str(), EpdFontFamily::BOLD);
  const int titleBoxWidth = titleWidth + 24;
  const int titleBoxX = rect.x + (rect.width - titleBoxWidth) / 2;
  const int titleY = barTop + (barHeight - renderer.getLineHeight(kTitleFontId)) / 2;

  drawStripes(renderer, rect.x + 6, barTop, titleBoxX - rect.x - 12, barHeight);
  const int rightStripeX = titleBoxX + titleBoxWidth + 6;
  drawStripes(renderer, rightStripeX, barTop, batteryGroupLeft - rightStripeX - 10, barHeight);

  renderer.drawText(kTitleFontId, titleBoxX + 12, titleY, titleText.c_str(), true, EpdFontFamily::BOLD);

  // Clear behind the battery group so stripes never collide with it.
  renderer.fillRect(batteryGroupLeft - 6, barTop + 1, rect.x + rect.width - batteryGroupLeft + 6 - 1, barHeight - 2,
                    false);
  drawBatteryRight(renderer,
                   Rect{rect.x + rect.width - sidePadding - m.batteryWidth, batteryY, m.batteryWidth, m.batteryHeight},
                   showBatteryPercentage);

  // Double rule under the bar: the retro window edge.
  const int ruleY = barTop + barHeight + 2;
  renderer.drawLine(rect.x, ruleY, rect.x + rect.width - 1, ruleY, true);
  renderer.drawLine(rect.x, ruleY + 2, rect.x + rect.width - 1, ruleY + 2, true);
}

void RetroTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                               const char* rightLabel) const {
  const auto& m = RetroMetrics::values;
  const int textY = rect.y + (rect.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
  renderer.drawText(UI_10_FONT_ID, rect.x + m.contentSidePadding, textY, label, true, EpdFontFamily::BOLD);
  if (rightLabel != nullptr && rightLabel[0] != '\0') {
    const int w = renderer.getTextWidth(UI_10_FONT_ID, rightLabel);
    renderer.drawText(UI_10_FONT_ID, rect.x + rect.width - m.contentSidePadding - w, textY, rightLabel);
  }
  const int ruleY = rect.y + rect.height - 2;
  renderer.drawLine(rect.x + m.contentSidePadding, ruleY, rect.x + rect.width - m.contentSidePadding, ruleY, true);
}

// ---- buttons ------------------------------------------------------------

void RetroTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                 const char* btn4) const {
  if (!buttonHintsVisible()) return;

  const GfxRenderer::Orientation origOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 106;
  constexpr int buttonHeight = RetroMetrics::values.buttonHintsHeight;
  constexpr int buttonY = RetroMetrics::values.buttonHintsHeight;
  constexpr int textYOffset = 7;
  constexpr int narrowButtonPositions[] = {25, 130, 245, 350};
  constexpr int wideButtonPositions[] = {38, 154, 268, 384};
  const int* buttonPositions = renderer.getScreenWidth() >= 528 ? wideButtonPositions : narrowButtonPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    const int x = buttonPositions[i];
    drawBevelBox(renderer, Rect{x, pageHeight - buttonY, buttonWidth, buttonHeight}, false);
    drawHintLabel(renderer, UI_10_FONT_ID, labels[i], x, buttonWidth - kShadow, pageHeight - buttonY,
                  buttonHeight - kShadow, textYOffset);
  }

  renderer.setOrientation(origOrientation);
}

void RetroTheme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const {
  // Same geometry as the base; the bevel would collide with the panel edge,
  // so side hints keep plain frames with a doubled inner line for the look.
  BaseTheme::drawSideButtonHints(renderer, topBtn, bottomBtn);
}

void RetroTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, const int buttonCount, const int selectedIndex,
                                const std::function<std::string(int)>& buttonLabel,
                                const std::function<UIIcon(int)>& rowIcon, const int rowSpacing) const {
  (void)rowIcon;
  const auto& m = RetroMetrics::values;
  const int spacing = rowSpacing < 0 ? m.menuSpacing : rowSpacing;
  for (int i = 0; i < buttonCount; ++i) {
    const int tileY = m.verticalSpacing + rect.y + i * (m.menuRowHeight + spacing);
    const bool selected = selectedIndex == i;
    const Rect tile{rect.x + m.contentSidePadding, tileY, rect.width - m.contentSidePadding * 2, m.menuRowHeight};
    drawBevelBox(renderer, tile, selected);
    const std::string label = buttonLabel(i);
    const int inset = selected ? kShadow : 0;
    const int textX =
        tile.x + inset + (tile.width - kShadow - renderer.getTextWidth(UI_10_FONT_ID, label.c_str())) / 2;
    const int textY = tileY + inset + (m.menuRowHeight - kShadow - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, textX, textY, label.c_str(), !selected);
  }
}

void RetroTheme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                            const bool selected) const {
  if (tabs.empty()) return;
  // File-drawer tabs: boxes sharing a common baseline; the active tab opens
  // into the content area (no bottom edge) and reads bold.
  const int slotWidth = rect.width / static_cast<int>(tabs.size());
  const int tabTop = rect.y + 6;
  const int tabHeight = rect.height - 12;
  const int baseline = tabTop + tabHeight;
  renderer.drawLine(rect.x, baseline, rect.x + rect.width - 1, baseline, true);

  for (size_t i = 0; i < tabs.size(); i++) {
    const int slotX = rect.x + static_cast<int>(i) * slotWidth;
    const auto& tab = tabs[i];
    const int tabX = slotX + 3;
    const int tabWidth = slotWidth - 6;
    if (tab.selected) {
      renderer.fillRect(tabX, tabTop, tabWidth, tabHeight, false);
      renderer.drawRect(tabX, tabTop, tabWidth, tabHeight, true);
      // Open the bottom edge into the page and double the top edge.
      renderer.drawLine(tabX + 1, baseline, tabX + tabWidth - 2, baseline, false);
      renderer.drawLine(tabX, tabTop + 2, tabX + tabWidth - 1, tabTop + 2, true);
      if (!selected) {
        // Tab strip unfocused: mark the active tab hollow instead of bold.
        renderer.drawLine(tabX, tabTop, tabX + tabWidth - 1, tabTop, true);
      }
    }
    const auto style = tab.selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int textWidth = renderer.getTextWidth(kTitleFontId, tab.label, style);
    const int textX = slotX + (slotWidth - textWidth) / 2;
    const int textY = tabTop + (tabHeight - renderer.getLineHeight(kTitleFontId)) / 2;
    renderer.drawText(kTitleFontId, textX, textY, tab.label, true, style);
  }
}

// ---- widgets ------------------------------------------------------------

void RetroTheme::fillBatteryIcon(const GfxRenderer& renderer, Rect rect, const uint16_t percentage) const {
  // Segmented cells instead of an analog fill: 4 blocks, each 25%.
  const int cells = 4;
  const int innerX = rect.x + 2;
  const int innerY = rect.y + 2;
  const int innerW = rect.width - 4;
  const int innerH = rect.height - 4;
  const int cellW = innerW / cells;
  const int lit = (static_cast<int>(percentage) * cells + 99) / 100;
  for (int i = 0; i < cells; ++i) {
    const int cx = innerX + i * cellW;
    if (i < lit) {
      renderer.fillRect(cx, innerY, cellW - 1, innerH, true);
    }
  }
}

void RetroTheme::drawTextField(const GfxRenderer& renderer, Rect rect, const int textWidth, const bool cursorMode,
                               const int contentStartX, const int contentWidth) const {
  (void)textWidth;
  (void)contentStartX;
  (void)contentWidth;
  // Sunken field: doubled top/left lines make the well look inset.
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, true);
  renderer.drawLine(rect.x + 1, rect.y + 1, rect.x + rect.width - 2, rect.y + 1, true);
  renderer.drawLine(rect.x + 1, rect.y + 1, rect.x + 1, rect.y + rect.height - 2, true);
  if (cursorMode) {
    renderer.fillRect(rect.x + 3, rect.y + rect.height - 4, rect.width - 6, 2, true);
  }
}
