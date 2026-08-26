#include "dlna.h"

// Shared scratch. These are big for an .eapp, so they live in .bss once and
// are reused by every call rather than sitting on the stack.
static char g_scratch[6144];
static char g_body[2048];

// ---- small string helpers ------------------------------------------------

static int str_ncase_eq(const char* a, const char* b, int n) {
  for (int i = 0; i < n; ++i) {
    char ca = a[i], cb = b[i];
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
    if (ca != cb) return 0;
    if (!ca) return 1;
  }
  return 1;
}

// Case-insensitive search (SSDP header names vary by vendor).
static const char* find_ci(const char* hay, const char* needle) {
  if (!hay || !needle) return 0;
  const int n = (int)strlen(needle);
  for (const char* p = hay; *p; ++p) {
    if (str_ncase_eq(p, needle, n)) return p + n;
  }
  return 0;
}

// Copy until any of the stop characters.
static void copy_until(const char* src, const char* stops, char* out, int cap) {
  int n = 0;
  while (src && *src && n < cap - 1) {
    for (const char* s = stops; *s; ++s) {
      if (*src == *s) goto done;
    }
    out[n++] = *src++;
  }
done:
  out[n] = 0;
}

// Extract the text of the first <tag>…</tag>.
static int xml_text(const char* xml, const char* tag, char* out, int cap) {
  char open[48];
  cp_snprintf(open, sizeof(open), "<%s>", tag);
  const char* p = find_ci(xml, open);
  if (!p) return 0;
  copy_until(p, "<", out, cap);
  return out[0] != 0;
}

// scheme://host:port from an absolute URL.
static void base_of(const char* url, char* out, int cap) {
  int n = 0, slashes = 0;
  while (url[n] && n < cap - 1) {
    if (url[n] == '/') {
      ++slashes;
      if (slashes == 3) break;
    }
    out[n] = url[n];
    ++n;
  }
  out[n] = 0;
}

// Resolve a (possibly relative) controlURL against the description base.
static void join_url(const char* base, const char* rel, char* out, int cap) {
  if (!rel || !rel[0]) {
    out[0] = 0;
    return;
  }
  if (str_ncase_eq(rel, "http", 4)) {
    cp_snprintf(out, cap, "%s", rel);
    return;
  }
  cp_snprintf(out, cap, "%s%s%s", base, rel[0] == '/' ? "" : "/", rel);
}

// XML-escape into a caller buffer (track titles carry & and quotes).
static void xml_escape(const char* in, char* out, int cap) {
  int n = 0;
  for (const char* p = in; *p && n < cap - 7; ++p) {
    switch (*p) {
      case '&': for (const char* e = "&amp;"; *e; ++e) out[n++] = *e; break;
      case '<': for (const char* e = "&lt;"; *e; ++e) out[n++] = *e; break;
      case '>': for (const char* e = "&gt;"; *e; ++e) out[n++] = *e; break;
      case '"': for (const char* e = "&quot;"; *e; ++e) out[n++] = *e; break;
      default: out[n++] = *p; break;
    }
  }
  out[n] = 0;
}

// ---- SOAP ----------------------------------------------------------------

static int soap(const CpApi* api, const DlnaRenderer* r, const char* service, const char* action, const char* argsXml,
                char* reply, int replyCap) {
  if (!r->controlUrl[0]) return 0;
  char header[160];
  cp_snprintf(header, sizeof(header), "SOAPAction: \"urn:schemas-upnp-org:service:%s:1#%s\"", service, action);
  cp_snprintf(g_body, sizeof(g_body),
              "<?xml version=\"1.0\"?>"
              "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
              "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
              "<s:Body><u:%s xmlns:u=\"urn:schemas-upnp-org:service:%s:1\">"
              "<InstanceID>0</InstanceID>%s</u:%s></s:Body></s:Envelope>",
              action, service, argsXml ? argsXml : "", action);
  const int n = api->http_post(r->controlUrl, "text/xml; charset=\"utf-8\"", header, g_body, reply, (uint32_t)replyCap);
  return n >= 0;
}

// ---- discovery -----------------------------------------------------------

// Fetch a device description and pull out the friendly name + AVTransport URL.
static int resolve(const CpApi* api, const char* location, DlnaRenderer* out) {
  const int n = api->http_get(location, g_scratch, sizeof(g_scratch) - 1);
  if (n <= 0) return 0;
  g_scratch[n] = 0;

  if (!xml_text(g_scratch, "friendlyName", out->name, DLNA_NAME_LEN)) {
    cp_snprintf(out->name, DLNA_NAME_LEN, "%s", "DLNA");
  }
  base_of(location, out->baseUrl, DLNA_URL_LEN);

  // Walk <service> blocks for AVTransport and take its <controlURL>.
  const char* p = g_scratch;
  for (;;) {
    const char* svc = find_ci(p, "AVTransport");
    if (!svc) break;
    const char* ctl = find_ci(svc, "<controlURL>");
    if (!ctl) break;
    char rel[DLNA_URL_LEN];
    copy_until(ctl, "<", rel, sizeof(rel));
    join_url(out->baseUrl, rel, out->controlUrl, DLNA_URL_LEN);
    return out->controlUrl[0] != 0;
  }
  return 0;
}

int dlna_discover(const CpApi* api, DlnaRenderer* out, const int cap, const uint32_t timeout_ms) {
  if (!api->wifi_ensure(15000)) return 0;
  const int n = api->ssdp_discover("urn:schemas-upnp-org:device:MediaRenderer:1", timeout_ms, g_scratch,
                                   sizeof(g_scratch) - 1);
  if (n <= 0) return 0;
  g_scratch[n] = 0;

  // Collect distinct LOCATION values first: resolve() reuses g_scratch, so the
  // discovery text has to be consumed before the first fetch.
  static char locations[DLNA_MAX_RENDERERS][DLNA_URL_LEN];
  int found = 0;
  const char* p = g_scratch;
  while (found < cap && found < DLNA_MAX_RENDERERS) {
    const char* loc = find_ci(p, "LOCATION:");
    if (!loc) break;
    while (*loc == ' ') ++loc;
    char url[DLNA_URL_LEN];
    copy_until(loc, "\r\n", url, sizeof(url));
    p = loc;
    if (!url[0]) continue;
    int dup = 0;
    for (int i = 0; i < found; ++i) {
      if (str_ncase_eq(locations[i], url, (int)strlen(url) + 1)) dup = 1;
    }
    if (dup) continue;
    cp_snprintf(locations[found], DLNA_URL_LEN, "%s", url);
    ++found;
  }

  int resolved = 0;
  for (int i = 0; i < found; ++i) {
    if (resolve(api, locations[i], &out[resolved])) ++resolved;
  }
  return resolved;
}

// ---- actions -------------------------------------------------------------

int dlna_set_uri(const CpApi* api, const DlnaRenderer* r, const char* url, const char* title) {
  static char args[1024];
  char safeUrl[DLNA_URL_LEN * 2];
  char safeTitle[160];
  xml_escape(url, safeUrl, sizeof(safeUrl));
  xml_escape(title ? title : "", safeTitle, sizeof(safeTitle));
  // DIDL-Lite metadata: some renderers refuse a bare URI, and it gives them a
  // title to show. The inner document must be escaped inside the SOAP arg.
  static char didl[640];
  cp_snprintf(didl, sizeof(didl),
              "&lt;DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" "
              "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
              "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\"&gt;"
              "&lt;item id=\"1\" parentID=\"0\" restricted=\"1\"&gt;"
              "&lt;dc:title&gt;%s&lt;/dc:title&gt;"
              "&lt;upnp:class&gt;object.item.audioItem.musicTrack&lt;/upnp:class&gt;"
              "&lt;res protocolInfo=\"http-get:*:audio/mpeg:*\"&gt;%s&lt;/res&gt;"
              "&lt;/item&gt;&lt;/DIDL-Lite&gt;",
              safeTitle, safeUrl);
  cp_snprintf(args, sizeof(args), "<CurrentURI>%s</CurrentURI><CurrentURIMetaData>%s</CurrentURIMetaData>", safeUrl,
              didl);
  return soap(api, r, "AVTransport", "SetAVTransportURI", args, g_scratch, sizeof(g_scratch));
}

int dlna_play(const CpApi* api, const DlnaRenderer* r) {
  return soap(api, r, "AVTransport", "Play", "<Speed>1</Speed>", g_scratch, sizeof(g_scratch));
}

int dlna_pause(const CpApi* api, const DlnaRenderer* r) {
  return soap(api, r, "AVTransport", "Pause", "", g_scratch, sizeof(g_scratch));
}

int dlna_stop(const CpApi* api, const DlnaRenderer* r) {
  return soap(api, r, "AVTransport", "Stop", "", g_scratch, sizeof(g_scratch));
}

int dlna_seek(const CpApi* api, const DlnaRenderer* r, const int seconds) {
  char args[96];
  cp_snprintf(args, sizeof(args), "<Unit>REL_TIME</Unit><Target>%02d:%02d:%02d</Target>", seconds / 3600,
              (seconds / 60) % 60, seconds % 60);
  return soap(api, r, "AVTransport", "Seek", args, g_scratch, sizeof(g_scratch));
}

int dlna_set_volume(const CpApi* api, const DlnaRenderer* r, const int percent) {
  char args[96];
  int v = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
  cp_snprintf(args, sizeof(args), "<Channel>Master</Channel><DesiredVolume>%d</DesiredVolume>", v);
  return soap(api, r, "RenderingControl", "SetVolume", args, g_scratch, sizeof(g_scratch));
}

// "H:MM:SS" or "HH:MM:SS" -> seconds. Returns -1 when unparseable.
static int parse_hms(const char* s) {
  int parts[3] = {0, 0, 0}, idx = 0, cur = 0, any = 0;
  for (const char* p = s; *p; ++p) {
    if (*p >= '0' && *p <= '9') {
      cur = cur * 10 + (*p - '0');
      any = 1;
    } else if (*p == ':') {
      if (idx < 2) parts[idx++] = cur;
      cur = 0;
    } else {
      break;
    }
  }
  if (!any) return -1;
  parts[idx] = cur;
  if (idx == 2) return parts[0] * 3600 + parts[1] * 60 + parts[2];
  if (idx == 1) return parts[0] * 60 + parts[1];
  return parts[0];
}

int dlna_position(const CpApi* api, const DlnaRenderer* r, int* elapsed, int* total) {
  if (!soap(api, r, "AVTransport", "GetPositionInfo", "", g_scratch, sizeof(g_scratch))) return 0;
  char buf[24];
  if (elapsed) {
    *elapsed = xml_text(g_scratch, "RelTime", buf, sizeof(buf)) ? parse_hms(buf) : -1;
  }
  if (total) {
    *total = xml_text(g_scratch, "TrackDuration", buf, sizeof(buf)) ? parse_hms(buf) : -1;
  }
  return 1;
}

int dlna_state(const CpApi* api, const DlnaRenderer* r, char* out, const int cap) {
  if (!soap(api, r, "AVTransport", "GetTransportInfo", "", g_scratch, sizeof(g_scratch))) return 0;
  return xml_text(g_scratch, "CurrentTransportState", out, cap);
}
