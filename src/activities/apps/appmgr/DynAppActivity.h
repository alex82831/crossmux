#pragma once

#ifndef SIMULATOR

#include <DynAppLoader.h>

#include <string>

#include "activities/Activity.h"

// Hosts one installed dynamic app (.eapp) inside the normal Activity
// lifecycle: load + on_enter in onEnter(), app on_loop/on_render driven by
// loop()/render(), unload in onExit(). Holding Back ~1.5s always force-exits,
// so a misbehaving app can never trap the user.
class DynAppActivity final : public Activity {
 public:
  DynAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string eappPath, std::string slug)
      : Activity("DynApp", renderer, mappedInput), eappPath_(std::move(eappPath)), slug_(std::move(slug)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  // A renderer streaming a published track keeps the device awake.
  bool preventAutoSleep() override;
  void render(RenderLock&&) override;

 private:
  uint32_t buildReleasedMask() const;
  uint32_t buildHeldMask() const;

  std::string eappPath_;
  std::string slug_;
  DynAppLoader loader_;
  DynAppLoader::Error loadError_ = DynAppLoader::Error::None;
  bool exiting_ = false;
  bool forceExitPending_ = false;  // hold fired; waiting for Back release to consume it
  uint32_t backHeldSinceMs_ = 0;
};

#endif  // !SIMULATOR
