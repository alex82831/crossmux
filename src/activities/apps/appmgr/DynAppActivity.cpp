#ifndef SIMULATOR

#include "DynAppActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "DynAppApi.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr uint32_t kForceExitHoldMs = 1500;
}

void DynAppActivity::onEnter() {
  Activity::onEnter();
  exiting_ = false;
  backHeldSinceMs_ = 0;

  dynappapi::bind(renderer, slug_);
  loadError_ = loader_.load(eappPath_);
  if (loadError_ == DynAppLoader::Error::None) {
    const CpApp* app = loader_.app();
    if (app->on_enter != nullptr && app->on_enter(dynappapi::table()) != 0) {
      LOG_ERR("DYNAPP", "%s on_enter refused", slug_.c_str());
      loader_.unload();
      loadError_ = DynAppLoader::Error::BadEntry;
    }
  }
  if (loadError_ != DynAppLoader::Error::None) {
    LOG_ERR("DYNAPP", "load %s failed: %s", eappPath_.c_str(), DynAppLoader::errorName(loadError_));
  }
  requestUpdate();
}

void DynAppActivity::onExit() {
  if (loader_.loaded() && loader_.app()->on_exit != nullptr) {
    loader_.app()->on_exit(dynappapi::table());
  }
  loader_.unload();
  dynappapi::unbind();
  Activity::onExit();
}

uint32_t DynAppActivity::buildReleasedMask() const {
  using Button = MappedInputManager::Button;
  uint32_t mask = 0;
  if (mappedInput.wasReleased(Button::Back)) mask |= CP_BTN_BACK;
  if (mappedInput.wasReleased(Button::Confirm)) mask |= CP_BTN_CONFIRM;
  if (mappedInput.wasReleased(Button::Left)) mask |= CP_BTN_LEFT;
  if (mappedInput.wasReleased(Button::Right)) mask |= CP_BTN_RIGHT;
  if (mappedInput.wasReleased(Button::Up)) mask |= CP_BTN_UP;
  if (mappedInput.wasReleased(Button::Down)) mask |= CP_BTN_DOWN;
  if (mappedInput.wasReleased(Button::PageBack)) mask |= CP_BTN_PAGE_BACK;
  if (mappedInput.wasReleased(Button::PageForward)) mask |= CP_BTN_PAGE_FORWARD;
  return mask;
}

uint32_t DynAppActivity::buildHeldMask() const {
  using Button = MappedInputManager::Button;
  uint32_t mask = 0;
  if (mappedInput.isPressed(Button::Back)) mask |= CP_BTN_BACK;
  if (mappedInput.isPressed(Button::Confirm)) mask |= CP_BTN_CONFIRM;
  if (mappedInput.isPressed(Button::Left)) mask |= CP_BTN_LEFT;
  if (mappedInput.isPressed(Button::Right)) mask |= CP_BTN_RIGHT;
  if (mappedInput.isPressed(Button::Up)) mask |= CP_BTN_UP;
  if (mappedInput.isPressed(Button::Down)) mask |= CP_BTN_DOWN;
  if (mappedInput.isPressed(Button::PageBack)) mask |= CP_BTN_PAGE_BACK;
  if (mappedInput.isPressed(Button::PageForward)) mask |= CP_BTN_PAGE_FORWARD;
  return mask;
}

bool DynAppActivity::preventAutoSleep() { return dynappapi::isServingMedia(); }

void DynAppActivity::loop() {
  if (exiting_) return;

  // Load failed: any release returns to the Apps menu.
  if (!loader_.loaded()) {
    if (mappedInput.wasAnyReleased()) {
      exiting_ = true;
      activityManager.goToApps();
    }
    return;
  }

  // Firmware-owned escape hatch: holding Back force-exits even if the app
  // swallows every event. Once the threshold fires we stop driving the app
  // and wait for the Back *release* before switching, so the release is
  // consumed here and never leaks into the Apps menu (which exits on a Back
  // release) — one gesture, one action across the activity boundary.
  if (forceExitPending_) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      exiting_ = true;
      activityManager.goToApps();
    }
    return;
  }
  if (mappedInput.isPressed(MappedInputManager::Button::Back)) {
    if (backHeldSinceMs_ == 0) backHeldSinceMs_ = millis();
    if (millis() - backHeldSinceMs_ >= kForceExitHoldMs) {
      forceExitPending_ = true;
      backHeldSinceMs_ = 0;
      return;
    }
  } else {
    backHeldSinceMs_ = 0;
  }

  CpInput input = {};
  input.released = buildReleasedMask();
  input.held = buildHeldMask();
  int tx = 0, ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    input.tapped = 1;
    input.touch_x = tx;
    input.touch_y = ty;
  } else {
    input.touch_x = -1;
    input.touch_y = -1;
  }

  // Serve a LAN renderer pulling a track the app published (music player).
  // No-op unless something is published.
  dynappapi::pumpMediaServer();

  const uint32_t flags = loader_.app()->on_loop(dynappapi::table(), &input);
  if (flags & CP_LOOP_EXIT) {
    exiting_ = true;
    activityManager.goToApps();
    return;
  }
  if (flags & CP_LOOP_RENDER) {
    requestUpdate();
  }
}

void DynAppActivity::render(RenderLock&&) {
  if (!loader_.loaded()) {
    // Minimal error screen; the themed chrome stays so it looks intentional.
    const int sw = renderer.getScreenWidth();
    const int sh = renderer.getScreenHeight();
    renderer.clearScreen();
    const Rect fullScreen{0, 0, sw, sh};
    UITheme::drawCenteredText(renderer, fullScreen, UI_12_FONT_ID, sh / 2 - renderer.getLineHeight(UI_12_FONT_ID),
                              tr(STR_DYNAPP_LOAD_FAILED), true, EpdFontFamily::BOLD);
    UITheme::drawCenteredText(renderer, fullScreen, UI_10_FONT_ID, sh / 2 + 8, DynAppLoader::errorName(loadError_));
    renderer.displayBuffer();
    return;
  }
  loader_.app()->on_render(dynappapi::table());
  renderer.displayBuffer();
}

#endif  // !SIMULATOR
