#pragma once

#include <cstdint>

class GfxRenderer;
class MappedInputManager;

// Shared "About" panel for the CN apps: author / contact / copyright, drawn
// as a modal overlay on top of the app's current frame. Apps without a menu
// open it by holding Back (~1s) through AboutGate; the games expose it as a
// menu row and only use drawOverlay().
namespace appabout {

// Draws the centered About panel; call last in render() so it overlays.
void drawOverlay(const GfxRenderer& renderer, const char* appTitle);

// Hold-Back trigger + modal input ownership. Call handle() first in loop();
// while it returns true the frame belongs to the About flow and the app must
// not act on input. The Back release that follows the long-press is consumed
// (release barrier), so a short Back keeps its normal meaning.
struct AboutGate {
  bool open = false;
  uint32_t backDownMs = 0;
  bool consumeRelease = false;

  // Returns true when this frame was consumed (overlay open, just opened, or
  // swallowing the opening release). Sets repaint when a redraw is needed.
  bool handle(MappedInputManager& input, bool& repaint);
};

}  // namespace appabout
