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

const CpApi* table();

}  // namespace dynappapi

#endif  // !SIMULATOR
