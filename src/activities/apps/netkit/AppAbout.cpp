#include "AppAbout.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace appabout {
namespace {
constexpr uint32_t kHoldMs = 900;
constexpr const char* kContact = "alex82831gm@gmail.com";
}  // namespace

void drawOverlay(const GfxRenderer& renderer, const char* appTitle) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  const int lineH12 = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineH10 = renderer.getLineHeight(UI_10_FONT_ID);
  const int lineHS = renderer.getLineHeight(SMALL_FONT_ID);
  const int pad = metrics.verticalSpacing;
  const int panelH = pad * 2 + lineH12 + lineH10 * 3 + lineHS * 2 + pad * 4;
  int panelW = sw * 82 / 100;
  if (panelW > 380) panelW = 380;
  const Rect panel{(sw - panelW) / 2, (sh - panelH) / 2, panelW, panelH};
  const Rect screen{0, 0, sw, sh};

  renderer.fillRect(panel.x, panel.y, panel.width, panel.height, false);
  renderer.drawRect(panel.x, panel.y, panel.width, panel.height, true);
  renderer.drawRect(panel.x + 2, panel.y + 2, panel.width - 4, panel.height - 4, true);

  int y = panel.y + pad * 2;
  UITheme::drawCenteredText(renderer, screen, UI_12_FONT_ID, y, appTitle, true, EpdFontFamily::BOLD);
  y += lineH12 + pad;
  UITheme::drawCenteredText(renderer, screen, UI_10_FONT_ID, y, tr(STR_APP_ABOUT_AUTHOR));
  y += lineH10;
  UITheme::drawCenteredText(renderer, screen, SMALL_FONT_ID, y, kContact);
  y += lineHS + pad;
  UITheme::drawCenteredText(renderer, screen, UI_10_FONT_ID, y, tr(STR_APP_ABOUT_LOCATION));
  y += lineH10;
  UITheme::drawCenteredText(renderer, screen, UI_10_FONT_ID, y, tr(STR_APP_ABOUT_RIGHTS));
  y += lineH10 + pad;
  UITheme::drawCenteredText(renderer, screen, SMALL_FONT_ID, y, "FLASHAPPS " CROSSPOINT_VERSION);
}

bool AboutGate::handle(MappedInputManager& input, bool& repaint) {
  repaint = false;

  if (open) {
    if (consumeRelease) {
      // The Back release that ends the opening hold belongs to the About
      // flow — swallow it so it cannot double as a close or a screen Back.
      if (input.wasReleased(MappedInputManager::Button::Back)) consumeRelease = false;
      return true;
    }
    int tapX = 0;
    int tapY = 0;
    if (input.wasAnyReleased() || input.wasScreenTapped(tapX, tapY)) {
      open = false;
      repaint = true;
    }
    return true;
  }

  if (input.isPressed(MappedInputManager::Button::Back)) {
    if (backDownMs == 0) backDownMs = millis();
    if (millis() - backDownMs >= kHoldMs) {
      open = true;
      consumeRelease = true;
      backDownMs = 0;
      repaint = true;
      return true;
    }
    // Short holds fall through so the app still sees its own Back handling
    // on the release edge.
    return false;
  }
  backDownMs = 0;
  return false;
}

}  // namespace appabout
