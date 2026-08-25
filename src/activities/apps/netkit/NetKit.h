#pragma once

#include <string>

// Shared helpers for the CN network apps (Weather / Daily Poem / RSS).
// Wi-Fi association stays with WifiSelectionActivity; these helpers only
// observe link state, tear the link down on app exit, and run bounded fetches.
namespace netkit {

// True while the STA link is up.
bool wifiConnected();

// Fully power Wi-Fi down (AirPageConnection teardown sequence). Call from
// onExit() when the app's screen brought the link up; harmless when already
// off.
void teardownWifi();

// HTTP(S) GET `url` into `out`, aborting once the body exceeds `maxBytes`.
// Returns false on transport failure or overflow. `out` is reserved once up
// front (bounded by maxBytes) so append never regrows more than once.
bool fetchToString(const std::string& url, std::string& out, size_t maxBytes);

}  // namespace netkit
