#pragma once

#ifndef SIMULATOR

#include <crosspoint_app_abi.h>

#include <string>

class GfxRenderer;
class MappedInputManager;

// Bridges the C `CpApi` table to firmware services for the single running
// dynamic app. CpApi entries are plain function pointers with no context
// argument, so the bridge holds file-scope state; only one dynamic app runs
// at a time (enforced by DynAppActivity being the only caller).
namespace dynappapi {

// slug names the sandbox: files land under /apps/data/<slug>/.
void bind(GfxRenderer& renderer, const std::string& slug);
void unbind();

// Pumped by the host activity each loop so a LAN renderer's pull of the
// published track gets served. Cheap when nothing is published.
void pumpMediaServer();

// True while a track is published to a LAN renderer. The host reports this as
// preventAutoSleep() so the idle timer cannot cut a song off mid-stream.
bool isServingMedia();

// Text-entry handshake, driven by the host between frames: it claims a pending
// request, opens the keyboard, and hands the outcome back.
bool takeTextInputRequest(std::string& title, std::string& initial, uint32_t& maxLen);
void deliverTextInput(const std::string& text, bool cancelled);

const CpApi* table();

}  // namespace dynappapi

#endif  // !SIMULATOR
