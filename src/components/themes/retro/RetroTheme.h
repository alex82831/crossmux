#pragma once

#include "components/themes/BaseTheme.h"

class GfxRenderer;

// Retro theme — a 1-bit homage to System 7 and Windows 3.x, tuned for e-ink:
// racing-stripe title bars, double window rules, beveled buttons with hard
// drop shadows, segmented battery cells, dotted list rules. All chrome is
// pure black/white geometry (no dither fills on the hot paths), so partial
// refresh stays crisp.
namespace RetroMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 18,
                                 .batteryHeight = 12,
                                 .topPadding = 4,
                                 .batteryBarHeight = 20,
                                 .headerHeight = 48,
                                 .verticalSpacing = 10,
                                 .previewPadding = 12,
                                 .previewHeightPercent = 30,
                                 .contentSidePadding = 18,
                                 .listRowHeight = 34,
                                 .listWithSubtitleRowHeight = 52,
                                 .listRowGap = 0,
                                 .listRowRadius = 0,
                                 .listInset = 0,
                                 .listSidePadding = 18,
                                 .listSelectionStyle = 0,  // invert fill, the classic
                                 .listScrollWidth = 6,
                                 .listScrollSide = 0,
                                 .listTitleBold = false,
                                 .headerSidePadding = 16,
                                 .headerUnderlineSize = 0,  // the theme draws its own double rule
                                 .headerTitleAlign = 1,     // centered, System-7 style
                                 .headerBatterySide = 0,
                                 .headerBatteryDetached = false,
                                 .menuRowHeight = 42,
                                 .menuSpacing = 9,
                                 .tabSpacing = 0,
                                 .tabBarHeight = 46,
                                 .tabPillFullSlot = true,
                                 .scrollBarWidth = 6,
                                 .scrollBarRightOffset = 4,
                                 .homeTopPadding = 40,
                                 .homeCoverHeight = 380,
                                 .homeCoverTileHeight = 380,
                                 .homeRecentBooksCount = 1,
                                 .homeShowRecentBookTitle = false,
                                 .homeContinueReadingInMenu = false,
                                 .homeMenuTopOffset = 12,
                                 .buttonHintsHeight = 42,
                                 .sideButtonHintsWidth = 30,
                                 .progressBarHeight = 14,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 19,
                                 .keyboardKeyHeight = 46,
                                 .keyboardKeySpacing = 2,
                                 .keyboardCenteredText = true,
                                 .keyboardVerticalOffset = -13,
                                 .keyboardTextFieldWidthPercent = 85,
                                 .keyboardWidthPercent = 94,
                                 .popupTopOffsetRatio = 0.075f,
                                 .popupMarginX = 15,
                                 .popupMarginY = 15,
                                 .popupFrameThickness = 2,
                                 .popupCornerRadius = 0,
                                 .popupTextBold = true,
                                 .popupTextInverted = false,  // paper-white dialog, black text
                                 .popupTextBaselineOffsetY = -2,
                                 .popupProgressBarHeight = 6,
                                 .popupProgressDrawOutline = true,
                                 .popupProgressClampPercent = true,
                                 .popupProgressFillInverted = false,
                                 .popupProgressOutlineInverted = false,
                                 .optionPopupItemSpacing = 4,
                                 .optionPopupInnerPadding = 14,
                                 .optionPopupSelectionHPadding = 8,
                                 .optionPopupSelectionVPadding = 4,
                                 .optionPopupTitleGap = 10,
                                 .optionPopupUseSmallFont = true,
                                 .optionPopupOptionFontBold = false,
                                 .optionPopupSelectionRadius = 0,
                                 .optionPopupSelectionLight = false,
                                 .optionPopupDrawAllRows = true,
                                 .optionPopupDialogSideMargin = 22,
                                 .optionPopupTitleSeparator = true,
                                 .textFieldHorizontalPadding = 6,
                                 .textFieldNormalThickness = 1,
                                 .textFieldCursorThickness = 3,
                                 .textFieldLineEndOffset = 0};
}

class RetroTheme final : public BaseTheme {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
  void drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                     const char* rightLabel = nullptr) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  void drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon, int rowSpacing = -1) const override;
  void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                  bool selected) const override;
  void fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const override;
  void drawTextField(const GfxRenderer& renderer, Rect rect, int textWidth, bool cursorMode = false,
                     int contentStartX = 0, int contentWidth = 0) const override;

  // Shared brush: the System-7 racing stripes. Exposed for reuse by other
  // chrome (popup title bands).
  static void drawStripes(const GfxRenderer& renderer, int x, int y, int width, int height);

 private:
  static void drawBevelBox(const GfxRenderer& renderer, Rect rect, bool pressed);
};
