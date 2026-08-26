#pragma once

// Minimal DLNA/UPnP AVTransport control point.
//
// The device has no audio hardware and the C3 has no Bluetooth Classic (so no
// A2DP headphones either). What it *can* do is act as the controller in the
// classic three-box DLNA model: it finds a renderer on the LAN, publishes the
// track off its own SD card over HTTP, and tells the renderer to fetch and
// play it. Audio never passes through this chip.
//
// Only the parts of UPnP that this needs are implemented: SSDP discovery, one
// XML scrape for the AVTransport control URL, and a handful of SOAP actions.

#include "app.h"

#define DLNA_MAX_RENDERERS 6
#define DLNA_NAME_LEN 48
#define DLNA_URL_LEN 160

typedef struct {
  char name[DLNA_NAME_LEN];       // friendlyName from the device description
  char controlUrl[DLNA_URL_LEN];  // absolute AVTransport control URL
  char baseUrl[DLNA_URL_LEN];     // scheme://host:port of the renderer
} DlnaRenderer;

// Discover MediaRenderers. Returns how many were resolved into `out`.
int dlna_discover(const CpApi* api, DlnaRenderer* out, int cap, uint32_t timeout_ms);

// AVTransport actions. Each returns 1 on success.
int dlna_set_uri(const CpApi* api, const DlnaRenderer* r, const char* url, const char* title);
int dlna_play(const CpApi* api, const DlnaRenderer* r);
int dlna_pause(const CpApi* api, const DlnaRenderer* r);
int dlna_stop(const CpApi* api, const DlnaRenderer* r);
int dlna_seek(const CpApi* api, const DlnaRenderer* r, int seconds);
// Volume is RenderingControl, not AVTransport, but same endpoint shape.
int dlna_set_volume(const CpApi* api, const DlnaRenderer* r, int percent);

// Playback position. Fills seconds elapsed/total and the transport state
// string ("PLAYING" / "PAUSED_PLAYBACK" / "STOPPED"). Returns 1 on success.
int dlna_position(const CpApi* api, const DlnaRenderer* r, int* elapsed, int* total);
int dlna_state(const CpApi* api, const DlnaRenderer* r, char* out, int cap);
