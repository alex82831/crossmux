#include "RssStore.h"

#include <ArduinoJson.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "RssFeedParser.h"
#include "network/HttpDownloader.h"

namespace rssstore {
namespace {

// Transport-level guard: stop pulling a runaway feed once this much raw XML
// has been consumed; 20 items are normally seen well before this.
constexpr size_t kMaxFeedBytes = 256 * 1024;

constexpr const char* kFeedsPath = "/rss-feeds.json";
constexpr const char* kCacheDir = "/.crosspoint/rss";
constexpr const char* kLogTag = "RSS";

constexpr const char* kDefaultFeedsJson =
    "[\n"
    "  {\"name\": \"少数派\", \"url\": \"https://sspai.com/feed\"},\n"
    "  {\"name\": \"阮一峰的网络日志\", \"url\": \"https://www.ruanyifeng.com/blog/atom.xml\"},\n"
    "  {\"name\": \"36氪\", \"url\": \"https://36kr.com/feed\"},\n"
    "  {\"name\": \"Solidot\", \"url\": \"https://www.solidot.org/index.rss\"},\n"
    "  {\"name\": \"BBC 中文\", \"url\": \"https://feeds.bbci.co.uk/zhongwen/simp/rss.xml\"}\n"
    "]\n";

// Buffered sequential reader with an absolute position, so the index pass can
// record body offsets without one-byte SD reads through the HAL mutex.
struct LineReader {
  HalFile& file;
  uint8_t buf[512];
  size_t fill = 0;
  size_t idx = 0;
  uint32_t pos = 0;  // absolute file offset of buf[idx]

  explicit LineReader(HalFile& f) : file(f) {}

  int readByte() {
    if (idx >= fill) {
      const int n = file.read(buf, sizeof(buf));
      if (n <= 0) return -1;
      fill = static_cast<size_t>(n);
      idx = 0;
    }
    ++pos;
    return buf[idx++];
  }

  // Reads a line into out (cap includes the terminator), dropping the
  // trailing newline and truncating overlong lines. False on EOF-at-start.
  bool readLine(char* out, const size_t cap) {
    size_t n = 0;
    int c = readByte();
    if (c < 0) return false;
    while (c >= 0 && c != '\n') {
      if (n + 1 < cap) out[n++] = static_cast<char>(c);
      c = readByte();
    }
    out[n] = '\0';
    return true;
  }

  bool skip(const uint32_t n) {
    const uint32_t target = pos + n;
    if (!file.seekSet(target)) return false;
    pos = target;
    fill = idx = 0;
    return true;
  }
};

}  // namespace

int loadFeeds(FeedInfo* out, const int maxFeeds) {
  String raw = Storage.readFile(kFeedsPath);
  if (raw.length() == 0) {
    Storage.writeFile(kFeedsPath, String(kDefaultFeedsJson));
    raw = kDefaultFeedsJson;
  }
  // Transient parse of a user-edited file; inputs beyond 8KB are rejected
  // rather than parsed into a matching-size document.
  if (raw.length() > 8192) {
    LOG_ERR(kLogTag, "feeds.json too large (%u bytes)", static_cast<unsigned>(raw.length()));
    return 0;
  }
  JsonDocument doc;
  if (deserializeJson(doc, raw.c_str()) != DeserializationError::Ok) {
    LOG_ERR(kLogTag, "feeds.json parse error");
    return 0;
  }
  int count = 0;
  for (JsonObject feed : doc.as<JsonArray>()) {
    if (count >= maxFeeds) break;
    const char* name = feed["name"];
    const char* url = feed["url"];
    if (name == nullptr || url == nullptr || name[0] == '\0' || url[0] == '\0') continue;
    out[count].name = name;
    out[count].url = url;
    ++count;
  }
  return count;
}

void cachePathFor(const int feedIndex, char* out, const size_t outLen) {
  snprintf(out, outLen, "%s/feed%d.cache", kCacheDir, feedIndex);
}

bool readCacheIndex(const char* path, CacheIndex& out) {
  out.count = 0;
  out.epoch = 0;
  HalFile file;
  if (!Storage.openFileForRead(kLogTag, path, file)) return false;
  LineReader reader(file);

  char line[kMaxTitleBytes + 8];
  if (!reader.readLine(line, sizeof(line))) return false;
  unsigned epoch = 0;
  if (sscanf(line, "CRSS1 %u", &epoch) != 1) return false;
  out.epoch = epoch;

  while (out.count < kMaxArticles) {
    if (!reader.readLine(line, sizeof(line)) || strcmp(line, "@") != 0) break;
    char title[kMaxTitleBytes + 8];
    char date[kMaxDateBytes + 8];
    char lenLine[16];
    if (!reader.readLine(title, sizeof(title))) break;
    if (!reader.readLine(date, sizeof(date))) break;
    if (!reader.readLine(lenLine, sizeof(lenLine))) break;
    unsigned bodyLen = 0;
    if (sscanf(lenLine, "%u", &bodyLen) != 1 || bodyLen > kMaxBodyBytes) break;
    const int i = out.count;
    out.titles[i] = title;
    out.dates[i] = date;
    out.bodyOffset[i] = reader.pos;
    out.bodyLen[i] = bodyLen;
    if (!reader.skip(bodyLen + 1)) break;  // +1 for the trailing newline
    ++out.count;
  }
  return out.count > 0;
}

bool readCacheBody(const char* path, const uint32_t offset, const uint32_t len, std::string& out) {
  out.clear();
  if (len == 0 || len > kMaxBodyBytes) return false;
  HalFile file;
  if (!Storage.openFileForRead(kLogTag, path, file)) return false;
  if (!file.seekSet(offset)) return false;
  // One bounded allocation (≤2KB): the body must be contiguous for wrapping.
  out.resize(len);
  const int n = file.read(&out[0], len);
  if (n != static_cast<int>(len)) {
    out.clear();
    return false;
  }
  return true;
}

bool CacheWriter::open(const int feedIndex) {
  Storage.ensureDirectoryExists(kCacheDir);
  cachePathFor(feedIndex, finalPath_, sizeof(finalPath_));
  snprintf(tmpPath_, sizeof(tmpPath_), "%s.tmp", finalPath_);
  count_ = 0;
  if (!Storage.openFileForWrite(kLogTag, tmpPath_, file_)) return false;
  char header[32];
  snprintf(header, sizeof(header), "CRSS1 %u\n", static_cast<unsigned>(time(nullptr)));
  file_.write(header, strlen(header));
  return true;
}

bool CacheWriter::addItem(const char* title, const char* date, const std::string& body) {
  if (!file_.isOpen() || count_ >= kMaxArticles) return false;
  char lenLine[16];
  snprintf(lenLine, sizeof(lenLine), "%u\n", static_cast<unsigned>(body.size()));
  file_.write("@\n", 2);
  file_.write(title, strlen(title));
  file_.write("\n", 1);
  file_.write(date, strlen(date));
  file_.write("\n", 1);
  file_.write(lenLine, strlen(lenLine));
  file_.write(body.data(), body.size());
  file_.write("\n", 1);
  ++count_;
  return true;
}

bool CacheWriter::commit() {
  if (!file_.isOpen()) return false;
  file_.close();  // close before the remove/rename pair below
  if (Storage.exists(finalPath_)) Storage.remove(finalPath_);
  if (!Storage.rename(tmpPath_, finalPath_)) {
    LOG_ERR(kLogTag, "cache rename failed: %s", finalPath_);
    Storage.remove(tmpPath_);
    return false;
  }
  return true;
}

void CacheWriter::abort() {
  if (file_.isOpen()) file_.close();  // close before remove of the same path
  Storage.remove(tmpPath_);
  count_ = 0;
}

bool peekCache(const char* path, int& count, uint32_t& epoch) {
  count = 0;
  epoch = 0;
  HalFile file;
  if (!Storage.openFileForRead(kLogTag, path, file)) return false;
  LineReader reader(file);
  char line[kMaxTitleBytes + 8];
  if (!reader.readLine(line, sizeof(line))) return false;
  unsigned e = 0;
  if (sscanf(line, "CRSS1 %u", &e) != 1) return false;
  epoch = e;
  while (count < kMaxArticles) {
    if (!reader.readLine(line, sizeof(line)) || strcmp(line, "@") != 0) break;
    char lenLine[16];
    if (!reader.readLine(line, sizeof(line))) break;  // title
    if (!reader.readLine(line, sizeof(line))) break;  // date
    if (!reader.readLine(lenLine, sizeof(lenLine))) break;
    unsigned bodyLen = 0;
    if (sscanf(lenLine, "%u", &bodyLen) != 1 || bodyLen > kMaxBodyBytes) break;
    if (!reader.skip(bodyLen + 1)) break;
    ++count;
  }
  return count > 0;
}

int fetchFeedToCache(const int feedIndex, const std::string& url) {
  CacheWriter writer;
  if (!writer.open(feedIndex)) {
    LOG_ERR(kLogTag, "cache open failed for feed %d", feedIndex);
    return 0;
  }
  RssFeedParser parser([&writer](const RssFeedParser::Item& item) {
    if (!writer.addItem(item.title, item.date, item.body)) return false;
    return writer.count() < kMaxArticles;
  });
  size_t consumed = 0;
  HttpDownloader::fetchUrl(url, [&](const uint8_t* data, const size_t len) {
    consumed += len;
    if (consumed > kMaxFeedBytes) return false;
    return parser.write(data, len);
  });
  // The transfer is aborted on purpose once the article cap is hit, so judge
  // by what was parsed, not by the transport result.
  if (writer.count() > 0 && writer.commit()) return writer.count();
  writer.abort();
  LOG_ERR(kLogTag, "no articles parsed from feed %d (%u bytes)", feedIndex, static_cast<unsigned>(consumed));
  return 0;
}

}  // namespace rssstore
